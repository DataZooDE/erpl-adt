#include <erpl_adt/adt/source.hpp>

#include <tinyxml2.h>

#include <cstdlib>
#include <string>

namespace erpl_adt {

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
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<SyntaxMessage>, Error>::Err(Error{
            "CheckSyntax", "", std::nullopt,
            "Failed to parse check response XML", std::nullopt});
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
        url += "?version=" + version;
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

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("WriteSource", source_uri, http.status_code, http.body));
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
        "  <chkrun:checkObject adtcore:uri=\"" + source_uri + "\" chkrun:version=\"active\"/>\n"
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
