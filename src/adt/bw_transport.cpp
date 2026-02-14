#include <erpl_adt/adt/bw_transport.hpp>

#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

const char* kBwCtoPath = "/sap/bw/modeling/cto";
const char* kCtoContentType = "application/vnd.sap.bw.modeling.cto-v1_1_0+xml";

// Get attribute or empty string.
std::string Attr(const tinyxml2::XMLElement* el, const char* name) {
    const char* val = el->Attribute(name);
    return val ? val : "";
}

Result<BwTransportCheckResult, Error> ParseCheckResponse(
    std::string_view xml, const HttpHeaders& response_headers) {
    BwTransportCheckResult result;

    // Check Writing-Enabled header
    auto we_it = response_headers.find("Writing-Enabled");
    if (we_it == response_headers.end()) {
        we_it = response_headers.find("writing-enabled");
    }
    if (we_it != response_headers.end()) {
        result.writing_enabled = (we_it->second == "true" || we_it->second == "X");
    }

    if (xml.empty()) {
        return Result<BwTransportCheckResult, Error>::Ok(std::move(result));
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return Result<BwTransportCheckResult, Error>::Err(Error{
            "BwTransportCheck", kBwCtoPath, std::nullopt,
            "Failed to parse BW transport response XML", std::nullopt,
            ErrorCategory::TransportError});
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
                chg.tlogo = Attr(setting, "tlogo");
                if (chg.tlogo.empty()) chg.tlogo = Attr(setting, "name");
                chg.transportable = (Attr(setting, "transportable") == "true");
                chg.changeable = (Attr(setting, "changeable") == "X" ||
                                  Attr(setting, "changeable") == "true");
                result.changeability.push_back(std::move(chg));
            }
        } else if (sn == "requests") {
            for (auto* req = section->FirstChildElement(); req;
                 req = req->NextSiblingElement()) {
                BwTransportRequest tr;
                tr.number = Attr(req, "number");
                tr.function_type = Attr(req, "functionType");
                tr.status = Attr(req, "status");
                tr.description = Attr(req, "description");

                for (auto* task = req->FirstChildElement(); task;
                     task = task->NextSiblingElement()) {
                    BwTransportTask tt;
                    tt.number = Attr(task, "number");
                    tt.function_type = Attr(task, "functionType");
                    tt.status = Attr(task, "status");
                    tt.owner = Attr(task, "owner");
                    tr.tasks.push_back(std::move(tt));
                }

                result.requests.push_back(std::move(tr));
            }
        } else if (sn == "objects") {
            for (auto* obj = section->FirstChildElement(); obj;
                 obj = obj->NextSiblingElement()) {
                BwTransportObject to;
                to.name = Attr(obj, "name");
                to.type = Attr(obj, "type");
                to.operation = Attr(obj, "operation");
                to.uri = Attr(obj, "uri");

                // Lock sub-element
                auto* lock = obj->FirstChildElement("lock");
                if (lock) {
                    auto* lock_req = lock->FirstChildElement("request");
                    if (lock_req) {
                        to.lock_request = Attr(lock_req, "number");
                    }
                }

                // TADIR sub-element
                auto* tadir = obj->FirstChildElement("tadir");
                if (tadir) {
                    to.tadir_status = Attr(tadir, "status");
                }

                result.objects.push_back(std::move(to));
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

    return Result<BwTransportCheckResult, Error>::Ok(std::move(result));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwTransportCheck
// ---------------------------------------------------------------------------
Result<BwTransportCheckResult, Error> BwTransportCheck(
    IAdtSession& session,
    bool own_only) {
    std::string url = std::string(kBwCtoPath) + "?rddetails=all";
    if (own_only) {
        url += "&ownonly=true";
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
        return Result<BwTransportCheckResult, Error>::Err(
            Error::FromHttpStatus("BwTransportCheck", url,
                                  http.status_code, http.body));
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

    // Build request body
    std::string body = R"(<bwCTO:transport xmlns:bwCTO="http://www.sap.com/bw/cto">)";
    body += "<objects>";
    body += R"(<object name=")" + options.object_name + R"(")";
    body += R"( type=")" + options.object_type + R"("/>)";
    body += "</objects></bwCTO:transport>";

    auto response = session.Post(url, body, kCtoContentType);
    if (response.IsErr()) {
        return Result<BwTransportWriteResult, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200 && http.status_code != 204) {
        return Result<BwTransportWriteResult, Error>::Err(
            Error::FromHttpStatus("BwTransportWrite", url,
                                  http.status_code, http.body));
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
