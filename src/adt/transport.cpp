#include <erpl_adt/adt/transport.hpp>
#include <erpl_adt/adt/object_exists.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

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
Result<void, Error> ReleaseTransport(
    IAdtSession& session,
    const std::string& transport_number) {
    const auto transport_uri =
        "/sap/bc/adt/cts/transportrequests/" + transport_number;

    // Releasing a transport that does not exist answered HTTP 200 with an
    // empty body, and the CLI said "Released transport: X" — a claim about a
    // one-way operation that had not happened. The release is a job the
    // backend accepts without validating the number, so check the transport
    // first.
    if (auto missing = EnsureObjectExists(session, transport_uri,
                                          "ReleaseTransport",
                                          "Transport " + transport_number);
        missing.IsErr()) {
        return Result<void, Error>::Err(std::move(missing).Error());
    }

    auto url = transport_uri + "/newreleasejobs";

    auto response = session.Post(url, "", "application/xml");
    if (response.IsErr()) {
        return Result<void, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<void, Error>::Err(
            Error::FromHttpStatus("ReleaseTransport", url, http.status_code, http.body));
    }

    return Result<void, Error>::Ok();
}

} // namespace erpl_adt
