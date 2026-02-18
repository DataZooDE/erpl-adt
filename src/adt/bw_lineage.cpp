#include <erpl_adt/adt/bw_lineage.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
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

bool IsTruthy(const std::string& value) {
    if (value.empty()) return false;
    std::string lower = ToLower(value);
    return lower == "x" || lower == "true" || lower == "1";
}

std::string AttrAny(const tinyxml2::XMLElement* element,
                    const char* a,
                    const char* b,
                    const char* c = nullptr,
                    const char* d = nullptr) {
    auto value = xml_utils::Attr(element, a);
    if (!value.empty()) return value;
    value = xml_utils::Attr(element, b);
    if (!value.empty()) return value;
    if (c != nullptr) {
        value = xml_utils::Attr(element, c);
        if (!value.empty()) return value;
    }
    if (d != nullptr) {
        value = xml_utils::Attr(element, d);
        if (!value.empty()) return value;
    }
    return "";
}

std::string ExtractFieldRef(const tinyxml2::XMLElement* element) {
    if (!element) return "";
    auto field = AttrAny(element, "field", "name", "sourceField", "targetField");
    if (!field.empty()) return field;
    if (element->GetText() != nullptr) {
        return element->GetText();
    }
    return "";
}

void ParseTrfnRule(const tinyxml2::XMLElement* rule_el,
                   const std::string& group_id,
                   const std::string& group_description,
                   const std::string& group_type,
                   std::vector<BwTransformationRule>& out) {
    BwTransformationRule rule;
    rule.id = xml_utils::Attr(rule_el, "id");
    rule.group_id = group_id;
    rule.group_description = group_description;
    rule.group_type = group_type;

    // Legacy flat-rule shape.
    auto source_attr = xml_utils::Attr(rule_el, "sourceField");
    auto target_attr = xml_utils::Attr(rule_el, "targetField");
    if (!source_attr.empty()) rule.source_fields.push_back(source_attr);
    if (!target_attr.empty()) rule.target_fields.push_back(target_attr);

    // Polymorphic rule shape: nested source/target references.
    for (auto* source_el = rule_el->FirstChildElement("source"); source_el;
         source_el = source_el->NextSiblingElement("source")) {
        const auto value = ExtractFieldRef(source_el);
        if (!value.empty()) {
            rule.source_fields.push_back(value);
        }
    }
    for (auto* target_el = rule_el->FirstChildElement("target"); target_el;
         target_el = target_el->NextSiblingElement("target")) {
        const auto value = ExtractFieldRef(target_el);
        if (!value.empty()) {
            rule.target_fields.push_back(value);
        }
    }

    auto* step_el = rule_el->FirstChildElement("step");
    if (step_el != nullptr) {
        rule.rule_type = xml_utils::Attr(step_el, "type");
        rule.formula = xml_utils::Attr(step_el, "formula");
        rule.constant = xml_utils::Attr(step_el, "constant");
        for (auto* attr = step_el->FirstAttribute(); attr != nullptr; attr = attr->Next()) {
            if (attr->Name() == nullptr || attr->Value() == nullptr) continue;
            rule.step_attributes[attr->Name()] = attr->Value();
        }
    } else {
        rule.rule_type = xml_utils::Attr(rule_el, "ruleType");
        rule.formula = xml_utils::Attr(rule_el, "formula");
        rule.constant = xml_utils::Attr(rule_el, "constant");
    }

    if (!rule.source_fields.empty()) {
        rule.source_field = rule.source_fields.front();
    }
    if (!rule.target_fields.empty()) {
        rule.target_field = rule.target_fields.front();
    }

    out.push_back(std::move(rule));
}

void ParseTrfnRuleContainer(const tinyxml2::XMLElement* container,
                            std::vector<BwTransformationRule>& out,
                            const std::string& group_id = "",
                            const std::string& group_description = "",
                            const std::string& group_type = "") {
    if (!container) return;

    for (auto* node = container->FirstChildElement(); node != nullptr;
         node = node->NextSiblingElement()) {
        const char* name = node->Name();
        if (!name) continue;
        std::string node_name(name);
        if (node_name == "rule") {
            ParseTrfnRule(node, group_id, group_description, group_type, out);
        } else if (node_name == "group") {
            ParseTrfnRuleContainer(
                node,
                out,
                xml_utils::Attr(node, "id"),
                xml_utils::Attr(node, "description"),
                xml_utils::Attr(node, "type"));
        }
    }
}

std::map<std::string, std::string> ParseElementAttributes(
    const tinyxml2::XMLElement* element) {
    std::map<std::string, std::string> attributes;
    if (!element) return attributes;
    for (auto* attr = element->FirstAttribute(); attr != nullptr; attr = attr->Next()) {
        if (attr->Name() == nullptr || attr->Value() == nullptr) continue;
        attributes[attr->Name()] = attr->Value();
    }
    return attributes;
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
    detail.start_routine = xml_utils::Attr(root, "startRoutine");
    detail.end_routine = xml_utils::Attr(root, "endRoutine");
    detail.expert_routine = xml_utils::Attr(root, "expertRoutine");
    detail.hana_runtime = IsTruthy(AttrAny(root, "HANARuntime", "hanaRuntime"));

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

    // Rules may be:
    // - under <rules><group>/<rule> (legacy fixture shape)
    // - direct <group> children of <transformation> (spec shape)
    ParseTrfnRuleContainer(root->FirstChildElement("rules"), detail.rules);
    ParseTrfnRuleContainer(root, detail.rules);

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
    detail.type = xml_utils::Attr(root, "type");

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

    auto* request_selection_el = root->FirstChildElement("requestSelection");
    if (request_selection_el) {
        detail.request_selection_mode = xml_utils::Attr(request_selection_el, "mode");
    }

    detail.extraction_settings =
        ParseElementAttributes(root->FirstChildElement("extractionSettings"));
    detail.execution_settings = ParseElementAttributes(root->FirstChildElement("execution"));
    detail.runtime_properties =
        ParseElementAttributes(root->FirstChildElement("runtimeProperties"));
    detail.error_handling = ParseElementAttributes(root->FirstChildElement("errorHandling"));
    detail.dtp_execution = ParseElementAttributes(root->FirstChildElement("dtpExecution"));

    auto* semantic_group_el = root->FirstChildElement("semanticGroup");
    if (semantic_group_el) {
        for (auto* field_el = semantic_group_el->FirstChildElement("field");
             field_el != nullptr; field_el = field_el->NextSiblingElement("field")) {
            auto field_name = xml_utils::Attr(field_el, "name");
            if (!field_name.empty()) {
                detail.semantic_group_fields.push_back(std::move(field_name));
            }
        }
    }

    auto* filter_el = root->FirstChildElement("filter");
    if (filter_el) {
        for (auto* field_el = filter_el->FirstChildElement("fields");
             field_el != nullptr; field_el = field_el->NextSiblingElement("fields")) {
            BwDtpDetail::FilterField field;
            field.name = xml_utils::Attr(field_el, "name");
            field.field = xml_utils::Attr(field_el, "field");
            field.selected = IsTruthy(xml_utils::Attr(field_el, "selected"));
            field.filter_selection = xml_utils::Attr(field_el, "filterSelection");
            field.selection_type = xml_utils::Attr(field_el, "selectionType");

            for (auto* sel_el = field_el->FirstChildElement("selection");
                 sel_el != nullptr; sel_el = sel_el->NextSiblingElement("selection")) {
                BwDtpDetail::FilterSelection selection;
                selection.low = xml_utils::Attr(sel_el, "low");
                selection.high = xml_utils::Attr(sel_el, "high");
                selection.op = AttrAny(sel_el, "operator", "op");
                selection.excluding = IsTruthy(xml_utils::Attr(sel_el, "excluding"));
                field.selections.push_back(std::move(selection));
            }
            detail.filter_fields.push_back(std::move(field));
        }
    }

    auto* program_flow_el = root->FirstChildElement("programFlow");
    if (program_flow_el) {
        for (auto* node_el = program_flow_el->FirstChildElement("node");
             node_el != nullptr; node_el = node_el->NextSiblingElement("node")) {
            BwDtpDetail::ProgramFlowNode node;
            node.id = xml_utils::Attr(node_el, "id");
            node.type = xml_utils::Attr(node_el, "type");
            node.name = xml_utils::Attr(node_el, "name");
            node.next = xml_utils::Attr(node_el, "next");
            detail.program_flow.push_back(std::move(node));
        }
    }

    return Result<BwDtpDetail, Error>::Ok(std::move(detail));
}

} // namespace erpl_adt
