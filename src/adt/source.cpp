#include <erpl_adt/adt/source.hpp>
#include "adt_utils.hpp"

#include <tinyxml2.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace erpl_adt {

std::optional<std::string> DeriveObjectUriFromSourceUri(
    std::string_view source_uri) {
    // Prefer /source/ so program-include URIs
    // (/sap/bc/adt/programs/includes/<name>/source/main), where /includes/ is
    // part of the object path, strip at the correct boundary. Fall back to
    // /includes/ for class includes (testclasses, definitions, ...).
    auto pos = source_uri.find("/source/");
    if (pos == std::string_view::npos) {
        pos = source_uri.find("/includes/");
    }
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(source_uri.substr(0, pos));
}

namespace {

// Parse "start=42,5" from a URI fragment like "/path#start=42,5"
void ParsePosition(const std::string& uri, int& line, int& offset) {
    line = 0;
    offset = 0;
    auto hash = uri.find('#');
    if (hash == std::string::npos) return;

    auto fragment = uri.substr(hash + 1);
    auto start_pos = fragment.find("start=");
    if (start_pos == std::string::npos) return;

    auto nums = fragment.substr(start_pos + 6); // after "start="
    auto comma = nums.find(',');
    if (comma != std::string::npos) {
        line = std::atoi(nums.substr(0, comma).c_str());
        offset = std::atoi(nums.substr(comma + 1).c_str());
    } else {
        line = std::atoi(nums.c_str());
    }
}

Result<std::vector<SyntaxMessage>, Error> ParseCheckResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "CheckSyntax", "",
            "Failed to parse check response XML")) {
        return Result<std::vector<SyntaxMessage>, Error>::Err(
            std::move(*parse_error));
    }

    std::vector<SyntaxMessage> messages;

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<SyntaxMessage>, Error>::Ok(std::move(messages));
    }

    // Navigate: checkRunReports > checkReport > checkMessageList > checkMessage
    for (auto* report = root->FirstChildElement(); report;
         report = report->NextSiblingElement()) {
        for (auto* msg_list = report->FirstChildElement(); msg_list;
             msg_list = msg_list->NextSiblingElement()) {
            for (auto* msg = msg_list->FirstChildElement(); msg;
                 msg = msg->NextSiblingElement()) {
                SyntaxMessage sm;

                auto* type_attr = msg->Attribute("chkrun:type");
                if (!type_attr) type_attr = msg->Attribute("type");
                sm.type = type_attr ? type_attr : "";

                auto* text_attr = msg->Attribute("chkrun:shortText");
                if (!text_attr) text_attr = msg->Attribute("shortText");
                sm.text = text_attr ? text_attr : "";

                auto* uri_attr = msg->Attribute("chkrun:uri");
                if (!uri_attr) uri_attr = msg->Attribute("uri");
                sm.uri = uri_attr ? uri_attr : "";

                ParsePosition(sm.uri, sm.line, sm.offset);

                messages.push_back(std::move(sm));
            }
        }
    }

    return Result<std::vector<SyntaxMessage>, Error>::Ok(std::move(messages));
}

// A class source section is a class include when its URI carries an
// `/includes/<type>` segment (testclasses, definitions, implementations,
// macros). Returns the include type, or nullopt for /source/main sections.
std::optional<std::string> ClassIncludeType(const std::string& source_uri) {
    const std::string marker = "/includes/";
    auto pos = source_uri.find(marker);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    auto type = source_uri.substr(pos + marker.size());
    // Drop any trailing query/fragment, just in case.
    type = type.substr(0, type.find_first_of("?#/"));
    if (type.empty()) {
        return std::nullopt;
    }
    return type;
}

// CreateClassInclude — POST to materialize a class include so it has an
// inactive version that source can subsequently be written to. ADT requires
// this for includes that do not yet exist; without it SAP rejects the source
// PUT with "<include> does not have any inactive version" (HTTP 500).
//
// Endpoint: POST {classUri}/includes?lockHandle={h}[&corrNr={tr}]
// Body: <class:abapClassInclude .. class:includeType="{type}"/>
Result<void, Error> CreateClassInclude(
    IAdtSession& session,
    const std::string& class_uri,
    const std::string& include_type,
    const LockHandle& lock_handle,
    const std::optional<std::string>& transport_number) {
    auto url = class_uri + "/includes?lockHandle=" + lock_handle.Value();
    if (transport_number) {
        url += "&corrNr=" + *transport_number;
    }

    const std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<class:abapClassInclude"
        " xmlns:class=\"http://www.sap.com/adt/oo/classes\""
        " xmlns:adtcore=\"http://www.sap.com/adt/core\""
        " adtcore:name=\"dummy\" class:includeType=\"" + include_type + "\"/>";

    auto response = session.Post(url, body, "application/*");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }
    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<void, Error>::Err(Error::FromHttpStatus(
            "CreateClassInclude", class_uri, http.status_code, http.body));
    }
    return Result<void, Error>::Ok();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ReadSource
// ---------------------------------------------------------------------------
Result<std::string, Error> ReadSource(
    IAdtSession& session,
    const std::string& source_uri,
    const std::string& version) {
    auto url = source_uri;
    if (!version.empty()) {
        url += (url.find('?') != std::string::npos ? "&version=" : "?version=") + version;
    }

    HttpHeaders headers;
    headers["Accept"] = "text/plain";

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::string, Error>::Err(
            Error::FromHttpStatus("ReadSource", source_uri, http.status_code, http.body));
    }

    return Result<std::string, Error>::Ok(http.body);
}

// ---------------------------------------------------------------------------
// WriteSource
// ---------------------------------------------------------------------------
Result<void, Error> WriteSource(
    IAdtSession& session,
    const std::string& source_uri,
    const std::string& source,
    const LockHandle& lock_handle,
    const std::optional<std::string>& transport_number) {
    auto url = source_uri + "?lockHandle=" + lock_handle.Value();
    if (transport_number) {
        url += "&corrNr=" + *transport_number;
    }

    auto response = session.Put(url, source, "text/plain; charset=utf-8");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    // First-time write to a class include (testclasses, definitions, ...):
    // SAP rejects the PUT with "<include> does not have any inactive version"
    // because the include object does not exist yet. Eclipse ADT POST-creates
    // the include first; mirror that, then retry the write once. Only possible
    // when we hold a lock handle (auto-lock / explicit-handle paths).
    {
        const auto& put = response.Value();
        if (put.status_code == 500 &&
            put.body.find("does not have any inactive version") !=
                std::string::npos) {
            auto include_type = ClassIncludeType(source_uri);
            auto class_uri = DeriveObjectUriFromSourceUri(source_uri);
            if (include_type && class_uri) {
                auto created = CreateClassInclude(
                    session, *class_uri, *include_type, lock_handle,
                    transport_number);
                if (created.IsErr()) {
                    return Result<void, Error>::Err(std::move(created).Error());
                }
                response = session.Put(url, source, "text/plain; charset=utf-8");
                if (response.IsErr()) {
                    return Result<void, Error>::Err(std::move(response).Error());
                }
            }
        }
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        if (http.status_code == 400 &&
            http.body.find("Session not found") != std::string::npos) {
            auto err = Error::FromHttpStatus("WriteSource", source_uri, http.status_code, http.body);
            err.hint =
                "Stateful ADT session is missing/expired. Retry the command. "
                "For multi-step workflows, use --session-file to persist state.";
            return Result<void, Error>::Err(std::move(err));
        }
        if (http.status_code == 423 &&
            http.body.find("invalid lock handle") != std::string::npos) {
            auto err = Error::FromHttpStatus("WriteSource", source_uri, http.status_code, http.body);
            err.hint =
                "Lock handle is invalid or the session context was lost between requests. "
                "On SAP systems older than 7.51, the server may not maintain stateful sessions. "
                "Install the stateful session enhancement on your SAP system: "
                "https://github.com/marcellourbani/abapfs_extensions";
            return Result<void, Error>::Err(std::move(err));
        }
        return Result<void, Error>::Err(
            Error::FromHttpStatus("WriteSource", source_uri, http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// WriteSourceOptimistic
// ---------------------------------------------------------------------------
Result<void, Error> WriteSourceOptimistic(
    IAdtSession& session,
    const std::string& source_uri,
    const std::string& source,
    const std::optional<std::string>& transport_number) {
    std::string url = source_uri;
    if (transport_number) {
        url += "?corrNr=" + *transport_number;
    }

    auto response = session.Put(url, source, "text/plain; charset=utf-8");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("WriteSourceOptimistic", source_uri, http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

// ---------------------------------------------------------------------------
// CheckSyntax
// ---------------------------------------------------------------------------
Result<std::vector<SyntaxMessage>, Error> CheckSyntax(
    IAdtSession& session,
    const std::string& source_uri) {
    std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<chkrun:checkObjectList xmlns:chkrun=\"http://www.sap.com/adt/checkrun\"\n"
        "  xmlns:adtcore=\"http://www.sap.com/adt/core\">\n"
        "  <chkrun:checkObject adtcore:uri=\"" + adt_utils::XmlEscape(source_uri) + "\" chkrun:version=\"active\"/>\n"
        "</chkrun:checkObjectList>\n";

    auto response = session.Post(
        "/sap/bc/adt/checkruns?reporters=abapCheckRun",
        body, "application/*");
    if (response.IsErr()) {
        return Result<std::vector<SyntaxMessage>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<SyntaxMessage>, Error>::Err(
            Error::FromHttpStatus("CheckSyntax", source_uri, http.status_code, http.body));
    }

    return ParseCheckResponse(http.body);
}

} // namespace erpl_adt
