#include <erpl_adt/adt/bw_lineage.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <string>

namespace erpl_adt {

namespace {

const char* kBwModelingBase = "/sap/bw/modeling/";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string BuildObjectPath(const std::string& type, const std::string& name,
                            const std::string& version) {
    return std::string(kBwModelingBase) + ToLower(type) + "/" + ToLower(name) + "/" + version;
}

std::string GetDefaultAcceptType(const std::string& tlogo) {
    auto lower = ToLower(tlogo);
    if (lower == "adso") return "application/vnd.sap.bw.modeling.adso-v1_2_0+xml";
    if (lower == "trfn") return "application/vnd.sap.bw.modeling.trfn-v1_0_0+xml";
    if (lower == "dtpa") return "application/vnd.sap.bw.modeling.dtpa-v1_0_0+xml";
    return "application/vnd.sap.bw.modeling." + lower + "+xml";
}

// Fetch object XML via GET.
Result<std::string, Error> FetchObjectXml(
    IAdtSession& session,
    const std::string& type,
    const std::string& name,
    const std::string& version,
    const char* operation,
    const std::string& content_type_override = "") {
    auto path = BuildObjectPath(type, name, version);
    HttpHeaders headers;
    headers["Accept"] = content_type_override.empty()
        ? GetDefaultAcceptType(type) : content_type_override;

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<std::string, Error>::Err(Error{
            operation, path, 404,
            std::string("BW object not found: ") + type + " " + name,
            std::nullopt, ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus(operation, path, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::string, Error>::Err(std::move(error));
    }

    return Result<std::string, Error>::Ok(http.body);
}

// Parse field elements from a parent element.
std::vector<BwTransformationField> ParseTransformationFields(
    const tinyxml2::XMLElement* parent) {
    std::vector<BwTransformationField> fields;
    if (!parent) return fields;

    for (auto* f = parent->FirstChildElement(); f;
         f = f->NextSiblingElement()) {
        BwTransformationField field;
        field.name = xml_utils::Attr(f, "name");
        field.type = xml_utils::Attr(f, "intType");
        field.aggregation = xml_utils::Attr(f, "aggregation");
        field.key = (xml_utils::Attr(f, "keyFlag") == "X");
        if (!field.name.empty()) {
            fields.push_back(std::move(field));
        }
    }
    return fields;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BwReadTransformation
// ---------------------------------------------------------------------------
Result<BwTransformationDetail, Error> BwReadTransformation(
    IAdtSession& session,
    const std::string& name,
    const std::string& version,
    const std::string& content_type) {
    auto xml_result = FetchObjectXml(session, "TRFN", name, version,
                                      "BwReadTransformation", content_type);
    if (xml_result.IsErr()) {
        return Result<BwTransformationDetail, Error>::Err(
            std::move(xml_result).Error());
    }

    const auto& xml = xml_result.Value();
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwReadTransformation", "",
            "Failed to parse TRFN XML")) {
        return Result<BwTransformationDetail, Error>::Err(
            std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwTransformationDetail, Error>::Err(Error{
            "BwReadTransformation", "", std::nullopt,
            "Empty TRFN response", std::nullopt, ErrorCategory::NotFound});
    }

    BwTransformationDetail detail;
    detail.name = name;
    detail.description = xml_utils::Attr(root, "description");

    // Source and target
    auto* source_el = root->FirstChildElement("source");
    if (source_el) {
        detail.source_name = xml_utils::Attr(source_el, "objectName");
        detail.source_type = xml_utils::Attr(source_el, "objectType");
    }
    auto* target_el = root->FirstChildElement("target");
    if (target_el) {
        detail.target_name = xml_utils::Attr(target_el, "objectName");
        detail.target_type = xml_utils::Attr(target_el, "objectType");
    }

    // Source fields
    detail.source_fields = ParseTransformationFields(
        root->FirstChildElement("sourceFields"));

    // Target fields
    detail.target_fields = ParseTransformationFields(
        root->FirstChildElement("targetFields"));

    // Rules
    auto* rules_el = root->FirstChildElement("rules");
    if (rules_el) {
        // Rules may be in <group> elements or directly under <rules>
        auto parse_rules = [&](const tinyxml2::XMLElement* parent) {
            for (auto* r = parent->FirstChildElement(); r;
                 r = r->NextSiblingElement()) {
                const char* rname = r->Name();
                if (!rname) continue;
                std::string rn(rname);

                if (rn == "rule") {
                    BwTransformationRule rule;
                    rule.source_field = xml_utils::Attr(r, "sourceField");
                    rule.target_field = xml_utils::Attr(r, "targetField");
                    rule.rule_type = xml_utils::Attr(r, "ruleType");
                    rule.formula = xml_utils::Attr(r, "formula");
                    rule.constant = xml_utils::Attr(r, "constant");
                    detail.rules.push_back(std::move(rule));
                } else if (rn == "group") {
                    // Recurse into group
                    for (auto* gr = r->FirstChildElement("rule"); gr;
                         gr = gr->NextSiblingElement("rule")) {
                        BwTransformationRule rule;
                        rule.source_field = xml_utils::Attr(gr, "sourceField");
                        rule.target_field = xml_utils::Attr(gr, "targetField");
                        rule.rule_type = xml_utils::Attr(gr, "ruleType");
                        rule.formula = xml_utils::Attr(gr, "formula");
                        rule.constant = xml_utils::Attr(gr, "constant");
                        detail.rules.push_back(std::move(rule));
                    }
                }
            }
        };
        parse_rules(rules_el);
    }

    return Result<BwTransformationDetail, Error>::Ok(std::move(detail));
}

// ---------------------------------------------------------------------------
// BwReadAdsoDetail
// ---------------------------------------------------------------------------
Result<BwAdsoDetail, Error> BwReadAdsoDetail(
    IAdtSession& session,
    const std::string& name,
    const std::string& version,
    const std::string& content_type) {
    auto xml_result = FetchObjectXml(session, "ADSO", name, version,
                                      "BwReadAdsoDetail", content_type);
    if (xml_result.IsErr()) {
        return Result<BwAdsoDetail, Error>::Err(std::move(xml_result).Error());
    }

    const auto& xml = xml_result.Value();
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwReadAdsoDetail", "",
            "Failed to parse ADSO XML")) {
        return Result<BwAdsoDetail, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwAdsoDetail, Error>::Err(Error{
            "BwReadAdsoDetail", "", std::nullopt,
            "Empty ADSO response", std::nullopt, ErrorCategory::NotFound});
    }

    BwAdsoDetail detail;
    detail.name = name;
    detail.description = xml_utils::Attr(root, "description");
    detail.package_name = xml_utils::Attr(root, "packageName");

    // Parse fields
    auto* fields_el = root->FirstChildElement("fields");
    if (fields_el) {
        for (auto* f = fields_el->FirstChildElement(); f;
             f = f->NextSiblingElement()) {
            BwAdsoField field;
            field.name = xml_utils::Attr(f, "name");
            field.data_type = xml_utils::Attr(f, "type");
            field.info_object = xml_utils::Attr(f, "infoObject");
            field.description = xml_utils::Attr(f, "description");
            field.key = (xml_utils::Attr(f, "keyFlag") == "X");

            field.length = xml_utils::AttrIntOr(f, "length", 0);
            field.decimals = xml_utils::AttrIntOr(f, "decimals", 0);

            if (!field.name.empty()) {
                detail.fields.push_back(std::move(field));
            }
        }
    }

    return Result<BwAdsoDetail, Error>::Ok(std::move(detail));
}

// ---------------------------------------------------------------------------
// BwReadDtpDetail
// ---------------------------------------------------------------------------
Result<BwDtpDetail, Error> BwReadDtpDetail(
    IAdtSession& session,
    const std::string& name,
    const std::string& version,
    const std::string& content_type) {
    auto xml_result = FetchObjectXml(session, "DTPA", name, version,
                                      "BwReadDtpDetail", content_type);
    if (xml_result.IsErr()) {
        return Result<BwDtpDetail, Error>::Err(std::move(xml_result).Error());
    }

    const auto& xml = xml_result.Value();
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwReadDtpDetail", "",
            "Failed to parse DTP XML")) {
        return Result<BwDtpDetail, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwDtpDetail, Error>::Err(Error{
            "BwReadDtpDetail", "", std::nullopt,
            "Empty DTP response", std::nullopt, ErrorCategory::NotFound});
    }

    BwDtpDetail detail;
    detail.name = name;
    detail.description = xml_utils::Attr(root, "description");

    auto* source_el = root->FirstChildElement("source");
    if (source_el) {
        detail.source_name = xml_utils::Attr(source_el, "objectName");
        detail.source_type = xml_utils::Attr(source_el, "objectType");
        detail.source_system = xml_utils::Attr(source_el, "sourceSystem");
    }

    auto* target_el = root->FirstChildElement("target");
    if (target_el) {
        detail.target_name = xml_utils::Attr(target_el, "objectName");
        detail.target_type = xml_utils::Attr(target_el, "objectType");
    }

    return Result<BwDtpDetail, Error>::Ok(std::move(detail));
}

} // namespace erpl_adt
