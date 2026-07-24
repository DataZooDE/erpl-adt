#include <erpl_adt/adt/ddic_domain.hpp>

#include "xml_utils.hpp"
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

std::string GetText(const tinyxml2::XMLElement* el, const char* name) {
    auto* child = el->FirstChildElement(name);
    if (child && child->GetText()) return child->GetText();
    return "";
}

std::optional<int> GetIntText(const tinyxml2::XMLElement* el, const char* name) {
    auto text = GetText(el, name);
    if (text.empty()) return std::nullopt;
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Parses a /sap/bc/adt/ddic/domains/{name} response body.
DomainInfo ParseDomainXml(const std::string& xml) {
    DomainInfo info;
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return info;
    auto* root = doc.RootElement();
    if (!root) return info;

    info.name = xml_utils::AttrAny(root, "adtcore:name", "name");
    info.description = xml_utils::AttrAny(root, "adtcore:description", "description");

    auto* content = root->FirstChildElement("doma:content");
    if (!content) return info;

    if (auto* type_info = content->FirstChildElement("doma:typeInformation")) {
        info.data_type = GetText(type_info, "doma:datatype");
        info.length = GetIntText(type_info, "doma:length");
        info.decimals = GetIntText(type_info, "doma:decimals");
    }
    if (auto* output_info = content->FirstChildElement("doma:outputInformation")) {
        info.output_length = GetIntText(output_info, "doma:length");
        info.conversion_exit = GetText(output_info, "doma:conversionExit");
    }
    if (auto* value_info = content->FirstChildElement("doma:valueInformation")) {
        if (auto* value_table_ref = value_info->FirstChildElement("doma:valueTableRef")) {
            if (value_table_ref->GetText()) info.value_table = value_table_ref->GetText();
        }
        if (auto* fix_values = value_info->FirstChildElement("doma:fixValues")) {
            for (auto* fv = fix_values->FirstChildElement("doma:fixValue"); fv;
                 fv = fv->NextSiblingElement("doma:fixValue")) {
                DomainFixValue value;
                value.low = GetText(fv, "doma:low");
                value.high = GetText(fv, "doma:high");
                value.text = GetText(fv, "doma:text");
                if (!value.low.empty() || !value.high.empty() || !value.text.empty()) {
                    info.fix_values.push_back(std::move(value));
                }
            }
        }
    }

    return info;
}

} // anonymous namespace

Result<DomainInfo, Error> GetDomain(IAdtSession& session, const std::string& domain_name) {
    auto url = "/sap/bc/adt/ddic/domains/" + UrlEncode(domain_name);

    // Accept header is required — this endpoint 400s ("Accept header
    // missing") on a bare request with none. The generic "application/xml"
    // media type is itself rejected with 406 ("not acceptable") — only a
    // wildcard subtype ("application/*") works.
    HttpHeaders headers;
    headers["Accept"] = "application/*";
    auto response = session.Get(url, headers);
    if (response.IsErr()) {
        return Result<DomainInfo, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<DomainInfo, Error>::Err(Error{
            "GetDomain", domain_name, 404,
            "Domain not found", std::nullopt,
            ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        return Result<DomainInfo, Error>::Err(
            Error::FromHttpStatus("GetDomain", domain_name, http.status_code, http.body));
    }

    return Result<DomainInfo, Error>::Ok(ParseDomainXml(http.body));
}

} // namespace erpl_adt
