#include <erpl_adt/adt/transport.hpp>
#include <erpl_adt/adt/object_exists.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/core/types.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <optional>
#include <algorithm>
#include <cctype>
#include <string>

namespace erpl_adt {

namespace {

constexpr const char* kSearchConfigUrl =
    "/sap/bc/adt/cts/transportrequests/searchconfiguration/configurations";

// Status code → human-readable status (matches Eclipse ADT terminology).
std::string TranslateStatus(const std::string& code) {
    if (code == "D") return "modifiable";
    if (code == "R") return "released";
    if (code == "L") return "locked";
    if (code == "O") return "released_pending";
    return code;  // pass through unknown codes
}

// Recursively walk the tree, collecting every element whose local-name is
// "request" — i.e. <tm:request> in either flat or
// <tm:workbench>/<tm:target>/<tm:modifiable>/<tm:request> nested layout.
void CollectRequests(const tinyxml2::XMLElement* el,
                     std::vector<const tinyxml2::XMLElement*>& out) {
    if (!el) {
        return;
    }
    for (auto* child = el->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const char* name = child->Name();
        if (!name) continue;

        // Strip namespace prefix to get local-name.
        std::string_view local = name;
        if (auto colon = local.find(':'); colon != std::string_view::npos) {
            local.remove_prefix(colon + 1);
        }

        if (local == "request") {
            out.push_back(child);
            // Don't descend into <tm:request> — its <tm:task> children are
            // sub-tasks of the same request, not separate transports.
            continue;
        }

        CollectRequests(child, out);
    }
}

// Find the first <atom:link rel="..."> href in any descendant element.
// Used to extract the config-href from the search-configurations response.
std::string FindAtomLinkHref(const tinyxml2::XMLElement* el,
                             std::string_view rel_substring) {
    if (!el) {
        return {};
    }
    for (auto* child = el->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        const char* name = child->Name();
        if (name) {
            std::string_view local = name;
            if (auto colon = local.find(':'); colon != std::string_view::npos) {
                local.remove_prefix(colon + 1);
            }
            if (local == "link") {
                const char* rel = child->Attribute("rel");
                const char* href = child->Attribute("href");
                if (rel && href &&
                    std::string_view(rel).find(rel_substring) != std::string_view::npos) {
                    return href;
                }
            }
        }
        auto inner = FindAtomLinkHref(child, rel_substring);
        if (!inner.empty()) {
            return inner;
        }
    }
    return {};
}

Result<std::vector<TransportInfo>, Error> ParseTransportList(
    std::string_view xml,
    const std::string& filter_user) {
    return adt_utils::ParseXmlWithRoot<std::vector<TransportInfo>>(
        xml, "ListTransports", "",
        "Failed to parse transport list XML",
        [&](const tinyxml2::XMLElement* root) {
            std::vector<TransportInfo> transports;
            std::vector<const tinyxml2::XMLElement*> request_elements;
            CollectRequests(root, request_elements);

            for (const auto* el : request_elements) {
                TransportInfo info;
                info.number = xml_utils::AttrAny(el, "tm:number", "number");
                info.description = xml_utils::AttrAny(el, "tm:desc", "desc");
                info.owner = xml_utils::AttrAny(el, "tm:owner", "owner");

                auto raw_status = xml_utils::AttrAny(el, "tm:status", "status");
                info.status = TranslateStatus(raw_status);

                info.target = xml_utils::AttrAny(el, "tm:target", "target");

                if (info.number.empty()) {
                    continue;
                }
                if (!filter_user.empty() &&
                    !adt_utils::IEquals(info.owner, filter_user)) {
                    continue;
                }
                transports.push_back(std::move(info));
            }

            return Result<std::vector<TransportInfo>, Error>::Ok(
                std::move(transports));
        });
}

// Resolve the user's saved search-configuration href.
// Returns empty string if none exists or on error (caller falls back to
// the legacy ?user=...&targets=true query).
std::string ResolveSearchConfigUri(IAdtSession& session) {
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.adt.configurations.v1+xml";

    auto response = session.Get(kSearchConfigUrl, headers);
    if (response.IsErr()) {
        return {};
    }
    const auto& http = response.Value();
    if (http.status_code != 200 || http.body.empty()) {
        return {};
    }

    tinyxml2::XMLDocument doc;
    if (adt_utils::ParseXmlOrError(doc, http.body, "ListTransports",
                                   kSearchConfigUrl,
                                   "Failed to parse search-configurations")) {
        return {};
    }
    const auto* root = doc.RootElement();
    if (!root) {
        return {};
    }
    return FindAtomLinkHref(root, "configurations");
}

// Direct children of `parent` with the given local name, in document order.
std::vector<const tinyxml2::XMLElement*> ChildrenByLocalName(
    const tinyxml2::XMLElement* parent, std::string_view local_name) {
    std::vector<const tinyxml2::XMLElement*> out;
    for (auto* child = parent->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        if (atom_parser::HasLocalName(child, local_name)) {
            out.push_back(child);
        }
    }
    return out;
}

std::vector<TransportObjectEntry> ParseObjectEntries(
    const tinyxml2::XMLElement* owner) {
    std::vector<TransportObjectEntry> objects;
    for (const auto* el : ChildrenByLocalName(owner, "abap_object")) {
        TransportObjectEntry entry;
        entry.pgmid = xml_utils::AttrAny(el, "tm:pgmid", "pgmid");
        entry.type = xml_utils::AttrAny(el, "tm:type", "type");
        entry.name = xml_utils::AttrAny(el, "tm:name", "name");
        entry.description = xml_utils::AttrAny(el, "tm:obj_info", "obj_info");
        entry.lock_status = xml_utils::AttrAny(el, "tm:lock_status", "lock_status");
        objects.push_back(std::move(entry));
    }
    return objects;
}

TransportTask ParseTask(const tinyxml2::XMLElement* el) {
    TransportTask task;
    task.number = xml_utils::AttrAny(el, "tm:number", "number");
    task.description = xml_utils::AttrAny(el, "tm:desc", "desc");
    task.owner = xml_utils::AttrAny(el, "tm:owner", "owner");
    task.status = TranslateStatus(xml_utils::AttrAny(el, "tm:status", "status"));
    task.type = xml_utils::AttrAny(el, "tm:type", "type");
    task.objects = ParseObjectEntries(el);
    return task;
}

TransportDetail ParseRequest(const tinyxml2::XMLElement* el) {
    TransportDetail detail;
    detail.number = xml_utils::AttrAny(el, "tm:number", "number");
    detail.description = xml_utils::AttrAny(el, "tm:desc", "desc");
    detail.owner = xml_utils::AttrAny(el, "tm:owner", "owner");
    detail.status = TranslateStatus(xml_utils::AttrAny(el, "tm:status", "status"));
    detail.target = xml_utils::AttrAny(el, "tm:target", "target");
    detail.type = xml_utils::AttrAny(el, "tm:type", "type");
    for (const auto* task : ChildrenByLocalName(el, "task")) {
        detail.tasks.push_back(ParseTask(task));
    }
    detail.objects = ParseObjectEntries(el);
    return detail;
}

// A task answered on its own: no sub-tasks; the target is the request's.
TransportDetail DetailFromTask(TransportTask task, const std::string& target) {
    TransportDetail detail;
    detail.number = std::move(task.number);
    detail.description = std::move(task.description);
    detail.owner = std::move(task.owner);
    detail.status = std::move(task.status);
    detail.target = target;
    detail.type = std::move(task.type);
    detail.objects = std::move(task.objects);
    return detail;
}

// The detail document holds the request; asked for a task number, the task
// is answered on its own (no sub-tasks).
Result<TransportDetail, Error> ParseTransportDetail(std::string_view xml,
                                                     const std::string& number,
                                                     const std::string& url) {
    return adt_utils::ParseXmlWithRoot<TransportDetail>(
        xml, "GetTransport", url, "Failed to parse transport request XML",
        [&](const tinyxml2::XMLElement* root) {
            std::vector<const tinyxml2::XMLElement*> requests;
            CollectRequests(root, requests);
            for (const auto* req : requests) {
                auto detail = ParseRequest(req);
                if (detail.number == number) {
                    return Result<TransportDetail, Error>::Ok(std::move(detail));
                }
                for (auto& task : detail.tasks) {
                    if (task.number != number) continue;
                    return Result<TransportDetail, Error>::Ok(
                        DetailFromTask(std::move(task), detail.target));
                }
            }
            // Asked for a task, SAP puts the task beside its parent request
            // under tm:root (tm:object_type="T"), not inside it.
            for (const auto* el : ChildrenByLocalName(root, "task")) {
                auto task = ParseTask(el);
                if (task.number != number) continue;
                return Result<TransportDetail, Error>::Ok(
                    DetailFromTask(std::move(task), ""));
            }
            return Result<TransportDetail, Error>::Err(Error{
                "GetTransport", url, std::nullopt,
                "Transport " + number + " is not in the response",
                std::nullopt, ErrorCategory::NotFound});
        });
}

// Every chkrun:checkReport anywhere below `el`, in document order.
void CollectCheckReports(const tinyxml2::XMLElement* el,
                         std::vector<const tinyxml2::XMLElement*>& out) {
    for (auto* child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (atom_parser::HasLocalName(child, "checkReport")) {
            out.push_back(child);
        }
        CollectCheckReports(child, out);
    }
}

void CollectErrorMessages(const tinyxml2::XMLElement* el, std::vector<std::string>& out) {
    for (auto* child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (atom_parser::HasLocalName(child, "checkMessage")) {
            const auto type = xml_utils::AttrAny(child, "chkrun:type", "type");
            const auto text = xml_utils::AttrAny(child, "chkrun:shortText", "shortText");
            if ((type == "E" || type == "A") && !text.empty()) {
                out.push_back(text);
            }
        }
        CollectErrorMessages(child, out);
    }
}

// The release report SAP returns with HTTP 200 whether the release worked or
// not: success is a report with status "released" and no error message.
// Anything else - "abortrelapifail" (an open task), "abortrelinacobj" (an
// inactive object), a pre-export error - is the release failing. An empty
// body (older releases) carries no report and stays accepted.
std::optional<std::string> ReleaseFailure(std::string_view body,
                                          const std::string& number) {
    if (body.empty()) {
        return std::nullopt;
    }
    // A non-empty answer without a readable report proves nothing - and the
    // release cannot be undone (crew F6).
    const std::string unreadable =
        "SAP's answer to the release could not be read as a release report; "
        "check whether it was released with: erpl-adt transport show " + number;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(body.data(), body.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement()) {
        return unreadable;
    }
    std::vector<const tinyxml2::XMLElement*> reports;
    CollectCheckReports(doc.RootElement(), reports);
    if (reports.empty()) {
        return unreadable;
    }
    std::string failure;
    for (const auto* report : reports) {
        const auto status = xml_utils::AttrAny(report, "chkrun:status", "status");
        std::vector<std::string> errors;
        CollectErrorMessages(report, errors);
        if (status == "released" && errors.empty()) {
            continue;
        }
        auto text = xml_utils::AttrAny(report, "chkrun:statusText", "statusText");
        if (text.empty()) text = "Release failed (" + status + ")";
        for (const auto& e : errors) {
            text += "; " + e;
        }
        failure += (failure.empty() ? "" : " | ") + text;
    }
    if (failure.empty()) {
        return std::nullopt;
    }
    return failure;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ListTransports
//
// Modern S/4HANA and ABAP Cloud systems require querying via a saved
// search-configuration URI. The legacy ?user=USER&targets=true form returns
// an empty <tm:root/> on these systems. Strategy:
//   1. Look up the user's saved search-configuration (one is created by the
//      ABAP backend on first login).
//   2. If found → GET ?configUri=<href>&targets=true and filter results by
//      owner == user.
//   3. If not → fall back to the legacy ?user=USER&targets=true call.
// ---------------------------------------------------------------------------
Result<std::vector<TransportInfo>, Error> ListTransports(
    IAdtSession& session,
    const std::string& user) {
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.adt.transportorganizertree.v1+xml";

    auto config_uri = ResolveSearchConfigUri(session);

    std::string url;
    bool filter_by_owner = false;
    if (!config_uri.empty()) {
        url = "/sap/bc/adt/cts/transportrequests?configUri=" +
              UrlEncode(config_uri) + "&targets=true";
        filter_by_owner = true;
    } else {
        url = "/sap/bc/adt/cts/transportrequests?user=" + UrlEncode(user) +
              "&targets=true";
    }

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<std::vector<TransportInfo>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<TransportInfo>, Error>::Err(
            Error::FromHttpStatus("ListTransports", url, http.status_code, http.body));
    }

    return ParseTransportList(http.body, filter_by_owner ? user : std::string{});
}

// ---------------------------------------------------------------------------
// CreateTransport
// ---------------------------------------------------------------------------
Result<std::string, Error> CreateTransport(
    IAdtSession& session,
    const std::string& description,
    const std::string& target_package) {
    std::string body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<asx:abap xmlns:asx=\"http://www.sap.com/abapxml\" version=\"1.0\">\n"
        "  <asx:values>\n"
        "    <DATA>\n"
        "      <OPERATION>I</OPERATION>\n"
        "      <DEVCLASS>" + adt_utils::XmlEscape(target_package) + "</DEVCLASS>\n"
        "      <REQUEST_TEXT>" + adt_utils::XmlEscape(description) + "</REQUEST_TEXT>\n"
        "    </DATA>\n"
        "  </asx:values>\n"
        "</asx:abap>\n";

    HttpHeaders headers;
    headers["Accept"] = "text/plain";

    auto response = session.Post(
        "/sap/bc/adt/cts/transports", body,
        "application/vnd.sap.as+xml; charset=UTF-8; dataname=com.sap.adt.CreateCorrectionRequest",
        headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 201) {
        return Result<std::string, Error>::Err(
            Error::FromHttpStatus("CreateTransport", "/sap/bc/adt/cts/transports", http.status_code, http.body));
    }

    // Response body contains the transport number.
    auto number = http.body;
    while (!number.empty() && (number.back() == '\n' || number.back() == '\r' || number.back() == ' ')) {
        number.pop_back();
    }
    // Extract last path segment if it's a URI.
    auto last_slash = number.rfind('/');
    if (last_slash != std::string::npos) {
        number = number.substr(last_slash + 1);
    }

    if (number.empty()) {
        return Result<std::string, Error>::Err(Error{
            "CreateTransport", "/sap/bc/adt/cts/transports", std::nullopt,
            "Empty transport number in response", std::nullopt,
            ErrorCategory::TransportError});
    }

    return Result<std::string, Error>::Ok(std::move(number));
}

// ---------------------------------------------------------------------------
// ReleaseTransport
// ---------------------------------------------------------------------------
namespace {

// One release job, its report checked: HTTP 200 either way, the outcome is
// the report (see ReleaseFailure).
Result<void, Error> ReleaseOne(IAdtSession& session, const std::string& number) {
    const auto url = "/sap/bc/adt/cts/transportrequests/" + number + "/newreleasejobs";
    auto response = session.Post(url, "", "application/xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }
    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("ReleaseTransport", url, http.status_code, http.body));
    }
    if (auto failure = ReleaseFailure(http.body, number)) {
        return Result<void, Error>::Err(Error{
            "ReleaseTransport", url, http.status_code, *failure, std::nullopt,
            ErrorCategory::TransportError});
    }
    return Result<void, Error>::Ok();
}

std::string UpperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

} // namespace

Result<ReleaseOutcome, Error> ReleaseTransport(
    IAdtSession& session,
    const std::string& transport_number) {
    // Read the request first: it must exist (a release job for an unknown
    // number was accepted and reported as released), and SAP releases a
    // request only after its tasks.
    auto detail = GetTransport(session, transport_number);
    if (detail.IsErr()) {
        return Result<ReleaseOutcome, Error>::Err(std::move(detail).Error());
    }
    const auto& tr = detail.Value();

    const auto me = UpperAscii(session.LogonUserName());
    std::vector<std::string> own;
    for (const auto& task : tr.tasks) {
        if (task.status != "modifiable") continue;
        // An empty task is unclassified and cannot be released ("Task ... is
        // unclassified (it cannot be released)"); SAP drops it when the
        // request is released. Neither release it first nor let it block.
        if (task.objects.empty()) continue;
        if (UpperAscii(task.owner) != me) {
            return Result<ReleaseOutcome, Error>::Err(Error{
                "ReleaseTransport", "/sap/bc/adt/cts/transportrequests/" + tr.number,
                std::nullopt,
                "Task " + task.number + " of " + task.owner + " is still open; only its "
                "owner can release it. Nothing was released.",
                std::nullopt, ErrorCategory::TransportError});
        }
        own.push_back(task.number);
    }

    ReleaseOutcome outcome;
    // A failure after a task was released says so: that step is done.
    const auto failed = [&outcome](Error err) {
        if (!outcome.released_tasks.empty()) {
            std::string done;
            for (const auto& t : outcome.released_tasks) done += (done.empty() ? "" : ", ") + t;
            err.message += " (tasks already released: " + done + ")";
        }
        return Result<ReleaseOutcome, Error>::Err(std::move(err));
    };
    for (const auto& task : own) {
        if (auto released = ReleaseOne(session, task); released.IsErr()) {
            return failed(std::move(released).Error());
        }
        outcome.released_tasks.push_back(task);
    }
    if (auto released = ReleaseOne(session, tr.number); released.IsErr()) {
        return failed(std::move(released).Error());
    }
    return Result<ReleaseOutcome, Error>::Ok(std::move(outcome));
}

// ---------------------------------------------------------------------------
// GetTransport
// ---------------------------------------------------------------------------
Result<TransportDetail, Error> GetTransport(
    IAdtSession& session,
    const std::string& transport_number) {
    // The number goes into the URL path: refuse anything that is not one.
    auto id = TransportId::Create(transport_number);
    if (id.IsErr()) {
        return Result<TransportDetail, Error>::Err(Error{
            "GetTransport", "", std::nullopt, id.Error(), std::nullopt,
            ErrorCategory::Internal});
    }
    const auto url = "/sap/bc/adt/cts/transportrequests/" + id.Value().Value();

    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.adt.transportorganizer.v1+xml";
    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<TransportDetail, Error>::Err(std::move(response).Error());
    }
    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<TransportDetail, Error>::Err(
            Error::FromHttpStatus("GetTransport", url, http.status_code, http.body));
    }
    return ParseTransportDetail(http.body, id.Value().Value(), url);
}

// ---------------------------------------------------------------------------
// TransportDetailToJson
// ---------------------------------------------------------------------------
namespace {

nlohmann::json TransportObjectsJson(const std::vector<TransportObjectEntry>& objects) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& o : objects) {
        j.push_back({{"pgmid", o.pgmid},
                     {"type", o.type},
                     {"name", o.name},
                     {"description", o.description},
                     {"lock_status", o.lock_status}});
    }
    return j;
}

} // namespace

nlohmann::json TransportDetailToJson(const TransportDetail& detail) {
    nlohmann::json tasks = nlohmann::json::array();
    for (const auto& t : detail.tasks) {
        tasks.push_back({{"number", t.number},
                         {"description", t.description},
                         {"owner", t.owner},
                         {"status", t.status},
                         {"type", t.type},
                         {"objects", TransportObjectsJson(t.objects)}});
    }
    return {{"number", detail.number},
            {"description", detail.description},
            {"owner", detail.owner},
            {"status", detail.status},
            {"target", detail.target},
            {"type", detail.type},
            {"tasks", tasks},
            {"objects", TransportObjectsJson(detail.objects)}};
}

} // namespace erpl_adt
