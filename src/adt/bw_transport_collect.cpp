#include <erpl_adt/adt/bw_transport_collect.hpp>

#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwCtoPath = "/sap/bw/modeling/cto";
const char* kCtoContentType = "application/vnd.sap.bw.modeling.cto-v1_1_0+xml";
const char* kCollectAccept = "application/vnd.sap-bw-modeling.trcollect+xml";

// Get attribute or empty string.
std::string Attr(const tinyxml2::XMLElement* el, const char* name) {
    const char* val = el->Attribute(name);
    return val ? val : "";
}

std::string BuildCollectUrl(const BwTransportCollectOptions& options) {
    std::string url = std::string(kBwCtoPath) +
        "?collect=true&mode=" + options.mode;
    if (options.transport.has_value()) {
        url += "&corrnum=" + *options.transport;
    }
    return url;
}

std::string BuildCollectBody(const BwTransportCollectOptions& options) {
    std::string body = R"(<bwCTO:transport xmlns:bwCTO="http://www.sap.com/bw/cto">)";
    body += "<objects>";
    body += R"(<object name=")" + options.object_name + R"(")";
    body += R"( type=")" + options.object_type + R"("/>)";
    body += "</objects></bwCTO:transport>";
    return body;
}

Result<BwTransportCollectResult, Error> ParseCollectResponse(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwTransportCollectResult, Error>::Err(Error{
            "BwTransportCollect", kBwCtoPath, std::nullopt,
            "Failed to parse BW transport collect response XML", std::nullopt,
            ErrorCategory::TransportError});
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwTransportCollectResult, Error>::Ok(
            BwTransportCollectResult{});
    }

    BwTransportCollectResult result;

    for (auto* section = root->FirstChildElement(); section;
         section = section->NextSiblingElement()) {
        const char* sec_name = section->Name();
        if (!sec_name) continue;
        std::string sn(sec_name);

        if (sn == "details") {
            for (auto* obj = section->FirstChildElement(); obj;
                 obj = obj->NextSiblingElement()) {
                BwCollectObject co;
                co.name = Attr(obj, "name");
                co.type = Attr(obj, "type");
                co.description = Attr(obj, "description");
                co.status = Attr(obj, "status");
                co.uri = Attr(obj, "uri");
                co.last_changed_by = Attr(obj, "lastChangedBy");
                co.last_changed_at = Attr(obj, "lastChangedAt");
                result.details.push_back(std::move(co));
            }
        } else if (sn == "dependencies") {
            for (auto* dep = section->FirstChildElement(); dep;
                 dep = dep->NextSiblingElement()) {
                BwCollectDependency cd;
                cd.name = Attr(dep, "name");
                cd.type = Attr(dep, "type");
                cd.version = Attr(dep, "version");
                cd.author = Attr(dep, "author");
                cd.package_name = Attr(dep, "packageName");
                cd.association_type = Attr(dep, "associationType");
                cd.associated_name = Attr(dep, "associatedName");
                cd.associated_type = Attr(dep, "associatedType");
                result.dependencies.push_back(std::move(cd));
            }
        } else if (sn == "messages") {
            for (auto* msg = section->FirstChildElement(); msg;
                 msg = msg->NextSiblingElement()) {
                if (msg->GetText()) {
                    result.messages.push_back(msg->GetText());
                } else {
                    auto text = Attr(msg, "text");
                    if (!text.empty()) result.messages.push_back(std::move(text));
                }
            }
        }
    }

    return Result<BwTransportCollectResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

Result<BwTransportCollectResult, Error> BwTransportCollect(
    IAdtSession& session,
    const BwTransportCollectOptions& options) {
    if (options.object_type.empty()) {
        return Result<BwTransportCollectResult, Error>::Err(Error{
            "BwTransportCollect", kBwCtoPath, std::nullopt,
            "Object type must not be empty", std::nullopt,
            ErrorCategory::TransportError});
    }
    if (options.object_name.empty()) {
        return Result<BwTransportCollectResult, Error>::Err(Error{
            "BwTransportCollect", kBwCtoPath, std::nullopt,
            "Object name must not be empty", std::nullopt,
            ErrorCategory::TransportError});
    }

    auto url = BuildCollectUrl(options);
    auto body = BuildCollectBody(options);

    HttpHeaders extra_headers;
    extra_headers["Accept"] = kCollectAccept;

    auto response = session.Post(url, body, kCtoContentType, extra_headers);
    if (response.IsErr()) {
        return Result<BwTransportCollectResult, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwTransportCollect", url,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<BwTransportCollectResult, Error>::Err(std::move(error));
    }

    if (http.body.empty()) {
        return Result<BwTransportCollectResult, Error>::Ok(
            BwTransportCollectResult{});
    }

    return ParseCollectResponse(http.body);
}

} // namespace erpl_adt
