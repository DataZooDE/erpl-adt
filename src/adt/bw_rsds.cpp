#include <erpl_adt/adt/bw_rsds.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>
#include <tinyxml2.h>

#include <string>

namespace erpl_adt {

namespace {

std::string BuildRsdsPath(const std::string& name, const std::string& source_system,
                          const std::string& version) {
    return "/sap/bw/modeling/rsds/" + UrlEncode(name) + "/" + UrlEncode(source_system) +
           "/" + UrlEncode(version);
}

std::string DefaultContentType() {
    return "application/vnd.sap.bw.modeling.rsds+xml";
}

Result<std::string, Error> FetchRsdsXml(
    IAdtSession& session, const std::string& path,
    const std::string& content_type) {
    HttpHeaders headers;
    headers["Accept"] = content_type.empty() ? DefaultContentType() : content_type;

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<std::string, Error>::Err(Error{
            "BwReadRsdsDetail", path, 404, "BW DataSource not found",
            std::nullopt, ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwReadRsdsDetail", path,
                                           http.status_code, http.body);
        AddBwHint(error);
        return Result<std::string, Error>::Err(std::move(error));
    }

    return Result<std::string, Error>::Ok(http.body);
}

void ParseFieldsRecursive(const tinyxml2::XMLElement* element,
                          const std::string& segment_id,
                          std::vector<BwRsdsField>& out) {
    if (!element) return;

    if (atom_parser::HasLocalName(element, "field") ||
        atom_parser::HasLocalName(element, "element")) {
        BwRsdsField field;
        field.segment_id = segment_id;
        field.name = xml_utils::AttrAny(element, "name", "field");
        field.description = xml_utils::AttrAny(element, "description", "text");
        field.data_type = xml_utils::AttrAny(element, "intType", "type");
        field.length = xml_utils::AttrIntOr(element, "length", 0);
        field.decimals = xml_utils::AttrIntOr(element, "decimals", 0);

        auto key_attr = xml_utils::AttrAny(element, "key", "keyFlag");
        field.key = (key_attr == "X" || key_attr == "true" || key_attr == "1");

        if (!field.name.empty()) {
            out.push_back(std::move(field));
        }
    }

    for (auto* child = element->FirstChildElement(); child;
         child = child->NextSiblingElement()) {
        ParseFieldsRecursive(child, segment_id, out);
    }
}

}  // namespace

Result<BwRsdsDetail, Error> BwReadRsdsDetail(
    IAdtSession& session, const std::string& name,
    const std::string& source_system, const std::string& version,
    const std::string& content_type) {
    if (name.empty()) {
        return Result<BwRsdsDetail, Error>::Err(Error{
            "BwReadRsdsDetail", "", std::nullopt, "name must not be empty",
            std::nullopt, ErrorCategory::Internal});
    }
    if (source_system.empty()) {
        return Result<BwRsdsDetail, Error>::Err(Error{
            "BwReadRsdsDetail", "", std::nullopt,
            "source_system must not be empty", std::nullopt,
            ErrorCategory::Internal});
    }

    auto path = BuildRsdsPath(name, source_system, version);
    auto xml_result = FetchRsdsXml(session, path, content_type);
    if (xml_result.IsErr()) {
        return Result<BwRsdsDetail, Error>::Err(std::move(xml_result).Error());
    }

    tinyxml2::XMLDocument doc;
    const auto& xml = xml_result.Value();
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, "BwReadRsdsDetail", path, "Failed to parse RSDS XML")) {
        return Result<BwRsdsDetail, Error>::Err(std::move(*parse_error));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<BwRsdsDetail, Error>::Err(Error{
            "BwReadRsdsDetail", path, std::nullopt, "Empty RSDS response",
            std::nullopt, ErrorCategory::NotFound});
    }

    BwRsdsDetail detail;
    detail.name = name;
    detail.source_system = source_system;
    detail.description = xml_utils::Attr(root, "description");
    detail.package_name = xml_utils::AttrAny(root, "packageName", "package");

    for (auto* segment = root->FirstChildElement(); segment;
         segment = segment->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(segment, "segment")) {
            continue;
        }
        const auto segment_id = xml_utils::AttrAny(segment, "ID", "id");
        ParseFieldsRecursive(segment, segment_id, detail.fields);
    }

    return Result<BwRsdsDetail, Error>::Ok(std::move(detail));
}

}  // namespace erpl_adt

