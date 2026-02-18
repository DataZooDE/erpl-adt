#include <erpl_adt/adt/bw_dataflow.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace erpl_adt {

namespace {

const char* kBwModelingBase = "/sap/bw/modeling/";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string BuildObjectPath(const std::string& name, const std::string& version) {
    return std::string(kBwModelingBase) + "dmod/" + ToLower(name) + "/" + version;
}

std::string LocalName(const char* name) {
    if (!name) return "";
    std::string n(name);
    const auto pos = n.find(':');
    if (pos != std::string::npos && pos + 1 < n.size()) {
        return n.substr(pos + 1);
    }
    return n;
}

void CollectAttributes(const tinyxml2::XMLElement* element,
                       std::map<std::string, std::string>& out) {
    if (!element) return;
    for (auto* attr = element->FirstAttribute(); attr != nullptr; attr = attr->Next()) {
        if (attr->Name() == nullptr || attr->Value() == nullptr) continue;
        out[attr->Name()] = attr->Value();
    }
}

std::string AttrAny(const tinyxml2::XMLElement* element,
                    const char* a,
                    const char* b,
                    const char* c = nullptr) {
    auto value = xml_utils::Attr(element, a);
    if (!value.empty()) return value;
    value = xml_utils::Attr(element, b);
    if (!value.empty()) return value;
    if (c != nullptr) return xml_utils::Attr(element, c);
    return "";
}

void CollectTopologyRecursive(const tinyxml2::XMLElement* element,
                              BwDataFlowDetail& detail) {
    if (!element) return;

    const auto local = LocalName(element->Name());
    if (local == "node") {
        BwDataFlowNode node;
        node.id = AttrAny(element, "id", "key");
        node.name = AttrAny(element, "name", "txt");
        node.type = AttrAny(element, "type", "nodeType");
        CollectAttributes(element, node.attributes);
        if (!node.id.empty() || !node.name.empty()) {
            detail.nodes.push_back(std::move(node));
        }
    } else if (local == "connection" || local == "edge" || local == "link") {
        BwDataFlowConnection edge;
        edge.from = AttrAny(element, "from", "source");
        edge.to = AttrAny(element, "to", "target");
        edge.type = AttrAny(element, "type", "edgeType");
        CollectAttributes(element, edge.attributes);
        if (!edge.from.empty() || !edge.to.empty()) {
            detail.connections.push_back(std::move(edge));
        }
    }

    for (auto* child = element->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        CollectTopologyRecursive(child, detail);
    }
}

}  // namespace

Result<BwDataFlowDetail, Error> BwReadDataFlow(
    IAdtSession& session,
    const std::string& name,
    const std::string& version,
    const std::string& content_type) {
    if (name.empty()) {
        return Result<BwDataFlowDetail, Error>::Err(Error{
            "BwReadDataFlow", "", std::nullopt,
            "DataFlow name must not be empty", std::nullopt, ErrorCategory::Internal});
    }

    const auto path = BuildObjectPath(name, version);
    HttpHeaders headers;
    headers["Accept"] = content_type.empty()
        ? "application/vnd.sap.bw.modeling.dmod-v1_0_0+xml"
        : content_type;

    auto response = session.Get(path, headers);
    if (response.IsErr()) {
        return Result<BwDataFlowDetail, Error>::Err(std::move(response).Error());
    }
    const auto& http = response.Value();
    if (http.status_code == 404) {
        return Result<BwDataFlowDetail, Error>::Err(Error{
            "BwReadDataFlow", path, 404,
            "BW DataFlow not found: DMOD " + name,
            std::nullopt, ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwReadDataFlow", path, http.status_code, http.body);
        AddBwHint(error);
        return Result<BwDataFlowDetail, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwReadDataFlow", path,
            "Failed to parse BW DataFlow XML")) {
        return Result<BwDataFlowDetail, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwDataFlowDetail, Error>::Err(Error{
            "BwReadDataFlow", path, std::nullopt,
            "Empty DataFlow response", std::nullopt, ErrorCategory::NotFound});
    }

    BwDataFlowDetail detail;
    detail.name = name;
    detail.description = xml_utils::AttrAny(root, "description", "objectDesc");
    CollectAttributes(root, detail.attributes);
    CollectTopologyRecursive(root, detail);

    return Result<BwDataFlowDetail, Error>::Ok(std::move(detail));
}

}  // namespace erpl_adt
