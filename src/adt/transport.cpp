#include <erpl_adt/adt/transport.hpp>

#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

// Get attribute value from element, trying multiple names.
std::string GetAttr(const tinyxml2::XMLElement* el,
                    const char* name1, const char* name2 = nullptr) {
    const char* val = el->Attribute(name1);
    if (!val && name2) val = el->Attribute(name2);
    return val ? val : "";
}

Result<std::vector<TransportInfo>, Error> ParseTransportList(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<std::vector<TransportInfo>, Error>::Err(Error{
            "ListTransports", "", std::nullopt,
            "Failed to parse transport list XML", std::nullopt,
            ErrorCategory::TransportError});
    }

    std::vector<TransportInfo> transports;

    auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<TransportInfo>, Error>::Ok(std::move(transports));
    }

    // Navigate through transport request elements.
    for (auto* el = root->FirstChildElement(); el; el = el->NextSiblingElement()) {
        TransportInfo info;
        info.number = GetAttr(el, "tm:number", "number");
        info.description = GetAttr(el, "tm:desc", "desc");
        info.owner = GetAttr(el, "tm:owner", "owner");
        info.status = GetAttr(el, "tm:status", "status");
        info.target = GetAttr(el, "tm:target", "target");

        if (!info.number.empty()) {
            transports.push_back(std::move(info));
        }
    }

    return Result<std::vector<TransportInfo>, Error>::Ok(std::move(transports));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ListTransports
// ---------------------------------------------------------------------------
Result<std::vector<TransportInfo>, Error> ListTransports(
    IAdtSession& session,
    const std::string& user) {
    auto url = "/sap/bc/adt/cts/transportrequests?user=" + user + "&targets=true";

    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.adt.transportorganizertree.v1+xml";

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

    return ParseTransportList(http.body);
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
        "      <DEVCLASS>" + target_package + "</DEVCLASS>\n"
        "      <REQUEST_TEXT>" + description + "</REQUEST_TEXT>\n"
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
    auto url = "/sap/bc/adt/cts/transportrequests/" + transport_number + "/newreleasejobs";

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
