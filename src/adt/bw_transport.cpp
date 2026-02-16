#include <erpl_adt/adt/bw_transport.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_context_headers.hpp>
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwCtoPath = "/sap/bw/modeling/cto";
const char* kCtoContentType = "application/vnd.sap.bw.modeling.cto-v1_1_0+xml";

Result<BwTransportCheckResult, Error> ParseCheckResponse(
    std::string_view xml, const HttpHeaders& response_headers) {
    BwTransportCheckResult result;

    // Check Writing-Enabled header
    auto writing_enabled = adt_utils::FindHeaderValueCi(response_headers,
                                                        "Writing-Enabled");
    if (writing_enabled.has_value()) {
        result.writing_enabled =
            (*writing_enabled == "true" || *writing_enabled == "X");
    }

    if (xml.empty()) {
        return Result<BwTransportCheckResult, Error>::Ok(std::move(result));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwTransportCheck", kBwCtoPath,
            "Failed to parse BW transport response XML",
            ErrorCategory::TransportError)) {
        return Result<BwTransportCheckResult, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwTransportCheckResult, Error>::Ok(std::move(result));
    }

    // Parse sections: changeability, requests, objects, messages
    for (auto* section = root->FirstChildElement(); section;
         section = section->NextSiblingElement()) {
        const char* sec_name = section->Name();
        if (!sec_name) continue;
        std::string sn(sec_name);

        if (sn == "changeability") {
            for (auto* setting = section->FirstChildElement(); setting;
                 setting = setting->NextSiblingElement()) {
                BwChangeability chg;
                chg.tlogo = xml_utils::AttrAny(setting, "tlogo", "name");
                chg.transportable = (xml_utils::Attr(setting, "transportable") == "true");
                chg.changeable = (xml_utils::Attr(setting, "changeable") == "X" ||
                                  xml_utils::Attr(setting, "changeable") == "true");
                result.changeability.push_back(std::move(chg));
            }
        } else if (sn == "requests") {
            for (auto* req = section->FirstChildElement(); req;
                 req = req->NextSiblingElement()) {
                BwTransportRequest tr;
                tr.number = xml_utils::Attr(req, "number");
                tr.function_type = xml_utils::Attr(req, "functionType");
                tr.status = xml_utils::Attr(req, "status");
                tr.description = xml_utils::Attr(req, "description");

                for (auto* task = req->FirstChildElement(); task;
                     task = task->NextSiblingElement()) {
                    BwTransportTask tt;
                    tt.number = xml_utils::Attr(task, "number");
                    tt.function_type = xml_utils::Attr(task, "functionType");
                    tt.status = xml_utils::Attr(task, "status");
                    tt.owner = xml_utils::Attr(task, "owner");
                    tr.tasks.push_back(std::move(tt));
                }

                result.requests.push_back(std::move(tr));
            }
        } else if (sn == "objects") {
            for (auto* obj = section->FirstChildElement(); obj;
                 obj = obj->NextSiblingElement()) {
                BwTransportObject to;
                to.name = xml_utils::Attr(obj, "name");
                to.type = xml_utils::Attr(obj, "type");
                to.operation = xml_utils::Attr(obj, "operation");
                to.uri = xml_utils::Attr(obj, "uri");

                // Lock sub-element
                auto* lock = obj->FirstChildElement("lock");
                if (lock) {
                    auto* lock_req = lock->FirstChildElement("request");
                    if (lock_req) {
                        to.lock_request = xml_utils::Attr(lock_req, "number");
                    }
                }

                // TADIR sub-element
                auto* tadir = obj->FirstChildElement("tadir");
                if (tadir) {
                    to.tadir_status = xml_utils::Attr(tadir, "status");
                }

                result.objects.push_back(std::move(to));
            }
        } else if (sn == "messages") {
            for (auto* msg = section->FirstChildElement(); msg;
                 msg = msg->NextSiblingElement()) {
                if (msg->GetText()) {
                    result.messages.push_back(msg->GetText());
                } else {
                    auto text = xml_utils::Attr(msg, "text");
                    if (!text.empty()) result.messages.push_back(std::move(text));
                }
            }
        }
    }

    return Result<BwTransportCheckResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwTransportCheck
// ---------------------------------------------------------------------------
Result<BwTransportCheckResult, Error> BwTransportCheck(
    IAdtSession& session,
    bool own_only) {
    BwTransportCheckOptions options;
    options.own_only = own_only;
    return BwTransportCheck(session, options);
}

Result<BwTransportCheckResult, Error> BwTransportCheck(
    IAdtSession& session,
    const BwTransportCheckOptions& options) {
    std::string url = std::string(kBwCtoPath) + "?rddetails=" +
                      (options.read_details.empty() ? "all" : options.read_details);
    if (options.read_properties) {
        url += "&rdprops=true";
    }
    if (options.own_only) {
        url += "&ownonly=true";
    }
    if (options.all_messages) {
        url += "&allmsgs=true";
    }

    HttpHeaders headers;
    headers["Accept"] = kCtoContentType;

    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<BwTransportCheckResult, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwTransportCheck", url,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<BwTransportCheckResult, Error>::Err(std::move(error));
    }

    return ParseCheckResponse(http.body, http.headers);
}

// ---------------------------------------------------------------------------
// BwTransportWrite
// ---------------------------------------------------------------------------
Result<BwTransportWriteResult, Error> BwTransportWrite(
    IAdtSession& session,
    const BwTransportWriteOptions& options) {
    if (options.transport.empty()) {
        return Result<BwTransportWriteResult, Error>::Err(Error{
            "BwTransportWrite", kBwCtoPath, std::nullopt,
            "Transport number must not be empty", std::nullopt,
            ErrorCategory::TransportError});
    }

    std::string url = std::string(kBwCtoPath) +
        "?corrnum=" + options.transport;
    if (!options.package_name.empty()) {
        url += "&package=" + options.package_name;
    }
    if (options.simulate) {
        url += "&simulate=true";
    }
    if (options.all_messages) {
        url += "&allmsgs=true";
    }

    // Build request body
    std::string body = R"(<bwCTO:transport xmlns:bwCTO="http://www.sap.com/bw/cto">)";
    body += "<objects>";
    body += R"(<object name=")" + adt_utils::XmlEscape(options.object_name) + R"(")";
    body += R"( type=")" + adt_utils::XmlEscape(options.object_type) + R"("/>)";
    body += "</objects></bwCTO:transport>";

    HttpHeaders headers;
    auto context = options.context_headers;
    if (!options.transport.empty() &&
        (!context.transport_lock_holder.has_value() ||
         context.transport_lock_holder->empty())) {
        context.transport_lock_holder = options.transport;
    }
    ApplyBwContextHeaders(context, headers);

    auto response = session.Post(url, body, kCtoContentType, headers);
    if (response.IsErr()) {
        return Result<BwTransportWriteResult, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        auto error = Error::FromHttpStatus("BwTransportWrite", url,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<BwTransportWriteResult, Error>::Err(std::move(error));
    }

    BwTransportWriteResult result;
    result.success = true;

    // Parse response messages if any
    if (!http.body.empty()) {
        tinyxml2::XMLDocument doc;
        if (doc.Parse(http.body.data(), http.body.size()) == tinyxml2::XML_SUCCESS) {
            auto* root = doc.RootElement();
            if (root) {
                auto* messages = root->FirstChildElement("messages");
                if (messages) {
                    for (auto* msg = messages->FirstChildElement(); msg;
                         msg = msg->NextSiblingElement()) {
                        if (msg->GetText()) {
                            result.messages.push_back(msg->GetText());
                        }
                    }
                }
            }
        }
    }

    return Result<BwTransportWriteResult, Error>::Ok(std::move(result));
}

} // namespace erpl_adt
