#include <erpl_adt/adt/bw_reporting.hpp>

#include "adt_utils.hpp"
#include "atom_parser.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <erpl_adt/core/url.hpp>

#include <tinyxml2.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

namespace {

const char* kReportingPath = "/sap/bw/modeling/comp/reporting";
const char* kQueryPropsPath = "/sap/bw/modeling/rules/qprops";

std::vector<BwReportingRecord> ParseGenericRecords(std::string_view xml,
                                                   const char* operation,
                                                   const std::string& endpoint) {
    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, xml, operation, endpoint,
            "Failed to parse BW reporting XML")) {
        return {};
    }

    std::vector<BwReportingRecord> out;
    std::function<void(const tinyxml2::XMLElement*)> visit =
        [&](const tinyxml2::XMLElement* element) {
            if (!element) {
                return;
            }

            BwReportingRecord record;
            record.fields["_element"] = std::string(atom_parser::LocalName(element->Name()));
            for (const auto* attr = element->FirstAttribute(); attr;
                 attr = attr->Next()) {
                record.fields[attr->Name()] = attr->Value() ? attr->Value() : "";
            }
            if (element->GetText() != nullptr) {
                record.fields["_text"] = element->GetText();
            }
            if (record.fields.size() > 1) {
                out.push_back(std::move(record));
            }

            for (auto* child = element->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                visit(child);
            }
        };

    const auto* root = doc.RootElement();
    if (root) {
        visit(root);
    }
    return out;
}

Result<std::string, Error> Fetch(IAdtSession& session,
                                 const std::string& path,
                                 const HttpHeaders& headers,
                                 const char* operation) {
    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<std::string, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus(operation, path, http.status_code, http.body);
        AddBwHint(error);
        return Result<std::string, Error>::Err(std::move(error));
    }

    return Result<std::string, Error>::Ok(http.body);
}

}  // namespace

Result<std::vector<BwReportingRecord>, Error>
BwGetReportingMetadata(IAdtSession& session, const BwReportingOptions& options) {
    if (options.compid.empty()) {
        return Result<std::vector<BwReportingRecord>, Error>::Err(Error{
            "BwGetReportingMetadata", kReportingPath, std::nullopt,
            "compid must not be empty", std::nullopt});
    }

    std::string path = std::string(kReportingPath) +
        "?compid=" + UrlEncode(options.compid) +
        "&dbgmode=" + std::string(options.dbgmode ? "true" : "false");

    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.bw.modeling.bicsresponse-v1_1_0+xml, application/xml";
    if (options.metadata_only) headers["MetadataOnly"] = "true";
    if (options.incl_metadata) headers["InclMetadata"] = "true";
    if (options.incl_object_values) headers["InclObjectValues"] = "true";
    if (options.incl_except_def) headers["InclExceptDef"] = "true";
    if (options.compact_mode) headers["CompactMode"] = "true";
    if (options.from_row.has_value()) headers["FromRow"] = std::to_string(*options.from_row);
    if (options.to_row.has_value()) headers["ToRow"] = std::to_string(*options.to_row);

    auto response = Fetch(session, path, headers, "BwGetReportingMetadata");
    if (response.IsErr()) {
        return Result<std::vector<BwReportingRecord>, Error>::Err(
            std::move(response).Error());
    }

    return Result<std::vector<BwReportingRecord>, Error>::Ok(
        ParseGenericRecords(response.Value(), "BwGetReportingMetadata", path));
}

Result<std::vector<BwReportingRecord>, Error>
BwGetQueryProperties(IAdtSession& session) {
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap-bw-modeling.rulesQueryProperties+xml, application/xml";

    auto response = Fetch(session, kQueryPropsPath, headers, "BwGetQueryProperties");
    if (response.IsErr()) {
        return Result<std::vector<BwReportingRecord>, Error>::Err(
            std::move(response).Error());
    }

    return Result<std::vector<BwReportingRecord>, Error>::Ok(
        ParseGenericRecords(response.Value(), "BwGetQueryProperties", kQueryPropsPath));
}

}  // namespace erpl_adt
