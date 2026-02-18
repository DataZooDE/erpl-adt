#include <erpl_adt/adt/bw_query.hpp>

#include "adt_utils.hpp"
#include "xml_utils.hpp"
#include <erpl_adt/adt/bw_hints.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace erpl_adt {

namespace {

const char* kBwModelingBase = "/sap/bw/modeling/";

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string SanitizeId(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            return static_cast<char>(c);
        }
        return '_';
    });
    return value;
}

std::string NormalizeComponentType(const std::string& value) {
    auto upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

bool IsQueryFamilyType(const std::string& type) {
    static const std::set<std::string> kAllowed = {
        "QUERY", "VARIABLE", "RKF", "CKF", "FILTER", "STRUCTURE"};
    return kAllowed.count(type) > 0;
}

std::string BuildObjectPath(const std::string& component_type,
                            const std::string& name,
                            const std::string& version) {
    // Query-family objects are hosted on the query endpoint.
    const auto endpoint = (component_type == "QUERY") ? "query" : "query";
    return std::string(kBwModelingBase) + endpoint + "/" + ToLower(name) + "/" + version;
}

std::string DefaultAccept(const std::string& component_type) {
    if (component_type == "QUERY") return "application/vnd.sap.bw.modeling.query-v1_11_0+xml";
    if (component_type == "VARIABLE") return "application/vnd.sap.bw.modeling.variable-v1_10_0+xml";
    if (component_type == "RKF") return "application/vnd.sap.bw.modeling.rkf-v1_10_0+xml";
    if (component_type == "CKF") return "application/vnd.sap.bw.modeling.ckf-v1_10_0+xml";
    if (component_type == "FILTER") return "application/vnd.sap.bw.modeling.filter-v1_9_0+xml";
    if (component_type == "STRUCTURE") return "application/vnd.sap.bw.modeling.structure-v1_9_0+xml";
    return "application/xml";
}

std::vector<std::string> AcceptCandidates(const std::string& component_type,
                                          const std::string& override_content_type) {
    if (!override_content_type.empty()) {
        return {override_content_type};
    }

    // Some BW systems lag one minor media version for query-family subcomponents.
    // Retry with a downgraded vendor media type before falling back to generic XML.
    if (component_type == "QUERY") {
        return {
            "application/vnd.sap.bw.modeling.query-v1_11_0+xml",
            "application/vnd.sap.bw.modeling.query-v1_10_0+xml",
            "application/xml",
        };
    }
    if (component_type == "VARIABLE") {
        return {
            "application/vnd.sap.bw.modeling.variable-v1_10_0+xml",
            "application/vnd.sap.bw.modeling.variable-v1_9_0+xml",
            "application/xml",
        };
    }
    if (component_type == "RKF") {
        return {
            "application/vnd.sap.bw.modeling.rkf-v1_10_0+xml",
            "application/vnd.sap.bw.modeling.rkf-v1_9_0+xml",
            "application/xml",
        };
    }
    if (component_type == "CKF") {
        return {
            "application/vnd.sap.bw.modeling.ckf-v1_10_0+xml",
            "application/vnd.sap.bw.modeling.ckf-v1_9_0+xml",
            "application/xml",
        };
    }
    if (component_type == "FILTER") {
        return {
            "application/vnd.sap.bw.modeling.filter-v1_9_0+xml",
            "application/vnd.sap.bw.modeling.filter-v1_8_0+xml",
            "application/xml",
        };
    }
    if (component_type == "STRUCTURE") {
        return {
            "application/vnd.sap.bw.modeling.structure-v1_9_0+xml",
            "application/vnd.sap.bw.modeling.structure-v1_8_0+xml",
            "application/xml",
        };
    }
    return {DefaultAccept(component_type), "application/xml"};
}

std::string LocalName(const char* name) {
    if (name == nullptr) return "";
    std::string n(name);
    auto pos = n.find(':');
    if (pos != std::string::npos && pos + 1 < n.size()) {
        return n.substr(pos + 1);
    }
    return n;
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

const tinyxml2::XMLElement* FirstChildByLocalName(const tinyxml2::XMLElement* element,
                                                  const std::string& local_name) {
    if (element == nullptr) return nullptr;
    for (auto* child = element->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        if (LocalName(child->Name()) == local_name) {
            return child;
        }
    }
    return nullptr;
}

std::string ChildAttrByLocalName(const tinyxml2::XMLElement* element,
                                 const std::string& child_local_name,
                                 const char* attr_name) {
    auto* child = FirstChildByLocalName(element, child_local_name);
    if (!child) return "";
    return xml_utils::Attr(child, attr_name);
}

std::string ChildTextByLocalName(const tinyxml2::XMLElement* element,
                                 const std::string& child_local_name) {
    auto* child = FirstChildByLocalName(element, child_local_name);
    if (!child || child->GetText() == nullptr) return "";
    return child->GetText();
}

void CollectAttributes(const tinyxml2::XMLElement* element,
                       std::map<std::string, std::string>& out) {
    if (element == nullptr) return;
    for (auto* attr = element->FirstAttribute(); attr != nullptr; attr = attr->Next()) {
        if (attr->Name() == nullptr || attr->Value() == nullptr) continue;
        out[attr->Name()] = attr->Value();
    }
}

std::string NormalizeTypeToken(const std::string& raw) {
    auto token = raw;
    const auto pos = token.find(':');
    if (pos != std::string::npos && pos + 1 < token.size()) {
        token = token.substr(pos + 1);
    }
    std::transform(token.begin(), token.end(), token.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return token;
}

void AddReferenceDedup(std::vector<BwQueryComponentRef>& refs,
                       std::set<std::string>& seen,
                       BwQueryComponentRef ref) {
    if (ref.name.empty()) return;
    const auto key = ref.type + "|" + ref.role + "|" + ref.name;
    if (seen.count(key) > 0) return;
    seen.insert(key);
    refs.push_back(std::move(ref));
}

void CollectReferencesRecursive(const tinyxml2::XMLElement* element,
                                std::vector<BwQueryComponentRef>& refs) {
    if (element == nullptr) return;

    const auto local = LocalName(element->Name());
    if (local == "member" || local == "reference" || local == "component" ||
        local == "element") {
        BwQueryComponentRef ref;
        ref.name = AttrAny(element, "name", "objectName", "compid");
        ref.type = AttrAny(element, "type", "objectType", "subType");
        ref.role = AttrAny(element, "role", "usage", "kind");
        CollectAttributes(element, ref.attributes);
        if (!ref.name.empty()) {
            refs.push_back(std::move(ref));
        }
    }

    for (auto* child = element->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        CollectReferencesRecursive(child, refs);
    }
}

void CollectQueryResourceReferences(const tinyxml2::XMLElement* root,
                                    BwQueryComponentDetail& detail) {
    if (root == nullptr) return;
    std::set<std::string> seen;
    for (const auto& ref : detail.references) {
        seen.insert(ref.type + "|" + ref.role + "|" + ref.name);
    }

    // Subcomponents (variables/rkf/ckf/filter/structure etc.).
    for (auto* sub = root->FirstChildElement(); sub != nullptr;
         sub = sub->NextSiblingElement()) {
        if (LocalName(sub->Name()) != "subComponents") continue;
        BwQueryComponentRef ref;
        ref.name = AttrAny(sub, "technicalName", "adtCore:name", "name");
        ref.type = NormalizeTypeToken(AttrAny(sub, "xsi:type", "type"));
        ref.role = "subcomponent";
        CollectAttributes(sub, ref.attributes);
        AddReferenceDedup(detail.references, seen, std::move(ref));
    }

    auto* main = FirstChildByLocalName(root, "mainComponent");
    if (!main) return;

    // Dimension-style nodes on rows/columns/free.
    for (auto* child = main->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        const auto lname = LocalName(child->Name());
        if (lname == "rows" || lname == "columns" || lname == "free") {
            BwQueryComponentRef ref;
            ref.name = AttrAny(child, "infoObjectName", "technicalName", "name");
            ref.type = "DIMENSION";
            ref.role = lname;
            CollectAttributes(child, ref.attributes);
            AddReferenceDedup(detail.references, seen, std::move(ref));
        }
    }

    // Filter selections + member hints.
    std::function<void(const tinyxml2::XMLElement*)> walk =
        [&](const tinyxml2::XMLElement* element) {
            if (!element) return;
            const auto lname = LocalName(element->Name());

            if (lname == "selections") {
                BwQueryComponentRef ref;
                ref.name = AttrAny(element, "infoObject", "infoObjectName", "name");
                ref.type = "FILTER_FIELD";
                ref.role = "filter";
                CollectAttributes(element, ref.attributes);
                AddReferenceDedup(detail.references, seen, std::move(ref));
            } else if (lname == "members") {
                BwQueryComponentRef ref;
                auto* default_hint = FirstChildByLocalName(element, "defaultHint");
                ref.name = ChildTextByLocalName(default_hint, "value");
                ref.type = "MEMBER";
                ref.role = "member";
                CollectAttributes(element, ref.attributes);
                AddReferenceDedup(detail.references, seen, std::move(ref));
            }

            for (auto* child = element->FirstChildElement(); child != nullptr;
                 child = child->NextSiblingElement()) {
                walk(child);
            }
        };
    walk(main);
}

}  // namespace

Result<BwQueryComponentDetail, Error> BwReadQueryComponent(
    IAdtSession& session,
    const std::string& component_type_raw,
    const std::string& name,
    const std::string& version,
    const std::string& content_type) {
    const auto component_type = NormalizeComponentType(component_type_raw);
    if (!IsQueryFamilyType(component_type)) {
        return Result<BwQueryComponentDetail, Error>::Err(Error{
            "BwReadQueryComponent",
            "",
            std::nullopt,
            "Unsupported query component type: " + component_type_raw,
            std::nullopt,
            ErrorCategory::Internal});
    }
    if (name.empty()) {
        return Result<BwQueryComponentDetail, Error>::Err(Error{
            "BwReadQueryComponent",
            "",
            std::nullopt,
            "Component name must not be empty",
            std::nullopt,
            ErrorCategory::Internal});
    }

    const auto path = BuildObjectPath(component_type, name, version);
    auto accepts = AcceptCandidates(component_type, content_type);
    std::vector<std::string> attempted_accepts;
    std::optional<HttpResponse> final_http;
    std::optional<Error> final_transport_error;
    for (size_t i = 0; i < accepts.size(); ++i) {
        HttpHeaders headers;
        headers["Accept"] = accepts[i];
        attempted_accepts.push_back(accepts[i]);

        auto response = session.Get(path, headers);
        if (response.IsErr()) {
            final_transport_error = std::move(response).Error();
            break;
        }
        const auto& http = response.Value();
        if (http.status_code == 415 && i + 1 < accepts.size()) {
            continue;
        }
        final_http = http;
        break;
    }

    if (final_transport_error.has_value()) {
        return Result<BwQueryComponentDetail, Error>::Err(std::move(*final_transport_error));
    }
    if (!final_http.has_value()) {
        return Result<BwQueryComponentDetail, Error>::Err(Error{
            "BwReadQueryComponent", path, std::nullopt,
            "No HTTP response received for BW query component read",
            std::nullopt, ErrorCategory::Internal});
    }
    const auto& http = *final_http;
    if (http.status_code == 404) {
        return Result<BwQueryComponentDetail, Error>::Err(Error{
            "BwReadQueryComponent", path, 404,
            "BW query component not found: " + component_type + " " + name,
            std::nullopt, ErrorCategory::NotFound});
    }
    if (http.status_code != 200) {
        auto error = Error::FromHttpStatus("BwReadQueryComponent", path, http.status_code, http.body);
        if (http.status_code == 415 && attempted_accepts.size() > 1) {
            std::string hint = "Tried Accept fallbacks: ";
            for (size_t i = 0; i < attempted_accepts.size(); ++i) {
                if (i > 0) hint += ", ";
                hint += attempted_accepts[i];
            }
            error.hint = hint;
        }
        AddBwHint(error);
        return Result<BwQueryComponentDetail, Error>::Err(std::move(error));
    }

    tinyxml2::XMLDocument doc;
    if (auto parse_error = adt_utils::ParseXmlOrError(
            doc, http.body, "BwReadQueryComponent", path,
            "Failed to parse BW query component XML")) {
        return Result<BwQueryComponentDetail, Error>::Err(std::move(*parse_error));
    }

    auto* root = doc.RootElement();
    if (!root) {
        return Result<BwQueryComponentDetail, Error>::Err(Error{
            "BwReadQueryComponent", path, std::nullopt,
            "Empty query component response", std::nullopt, ErrorCategory::NotFound});
    }

    BwQueryComponentDetail detail;
    detail.component_type = component_type;
    detail.name = name;
    detail.description = xml_utils::AttrAny(root, "description", "objectDesc");
    detail.info_provider = AttrAny(root, "infoProvider", "provider", "infoprovider");
    detail.info_provider_type = AttrAny(root, "infoProviderType", "providerType", "infoproviderType");
    CollectAttributes(root, detail.attributes);
    CollectReferencesRecursive(root, detail.references);

    // SAP query runtime shape: Qry:queryResource with mainComponent/subComponents.
    if (LocalName(root->Name()) == "queryResource") {
        auto* main = FirstChildByLocalName(root, "mainComponent");
        if (main != nullptr) {
            auto main_name = AttrAny(main, "technicalName", "name", "adtCore:name");
            if (!main_name.empty()) {
                detail.name = main_name;
            }
            auto main_desc = ChildAttrByLocalName(main, "description", "value");
            if (!main_desc.empty()) {
                detail.description = main_desc;
            }
            auto provider = AttrAny(main, "providerName", "provider", "infoProvider");
            if (!provider.empty()) {
                detail.info_provider = provider;
            }
        }
        CollectQueryResourceReferences(root, detail);
    }

    return Result<BwQueryComponentDetail, Error>::Ok(std::move(detail));
}

BwQueryGraph BwBuildQueryGraph(const BwQueryComponentDetail& detail) {
    BwQueryGraph graph;
    graph.root_node_id = "Q_" + SanitizeId(detail.component_type) + "_" + SanitizeId(detail.name);
    graph.provenance.push_back("bw.read-query");
    graph.provenance.push_back("adt.bw.query-component");

    BwQueryGraphNode root;
    root.id = graph.root_node_id;
    root.type = detail.component_type;
    root.name = detail.name;
    root.role = "root";
    root.label = detail.name;
    if (!detail.description.empty()) {
        root.label += "\\n" + detail.description;
    }
    root.attributes = detail.attributes;
    if (!detail.description.empty()) root.attributes["description"] = detail.description;
    if (!detail.info_provider.empty()) root.attributes["info_provider"] = detail.info_provider;
    if (!detail.info_provider_type.empty()) root.attributes["info_provider_type"] = detail.info_provider_type;
    graph.nodes.push_back(std::move(root));

    for (size_t i = 0; i < detail.references.size(); ++i) {
        const auto& ref = detail.references[i];
        const auto ref_id = "R" + std::to_string(i + 1);

        BwQueryGraphNode node;
        node.id = ref_id;
        node.type = ref.type.empty() ? "REFERENCE" : ref.type;
        node.name = ref.name;
        node.role = ref.role;
        node.label = ref.type.empty() ? ref.name : (ref.type + ": " + ref.name);
        node.attributes = ref.attributes;
        graph.nodes.push_back(std::move(node));

        BwQueryGraphEdge edge;
        edge.id = "E" + std::to_string(i + 1);
        edge.from = graph.root_node_id;
        edge.to = ref_id;
        edge.type = "depends_on";
        edge.role = ref.role;
        graph.edges.push_back(std::move(edge));
    }

    if (detail.references.empty()) {
        graph.warnings.push_back("No references discovered");
    }
    return graph;
}

std::string BwRenderQueryGraphMermaid(const BwQueryGraph& graph,
                                      const BwQueryMermaidOptions& options) {
    auto esc = [](std::string s) {
        size_t pos = 0;
        while ((pos = s.find('\"', pos)) != std::string::npos) {
            s.replace(pos, 1, "\\\"");
            pos += 2;
        }
        return s;
    };

    const std::string direction =
        NormalizeComponentType(options.direction) == "LR" ? "LR" : "TD";
    const auto layout = ToLower(options.layout);

    std::vector<BwQueryGraphNode> nodes = graph.nodes;
    std::vector<BwQueryGraphEdge> edges = graph.edges;
    std::sort(nodes.begin(), nodes.end(),
              [](const BwQueryGraphNode& a, const BwQueryGraphNode& b) { return a.id < b.id; });
    std::sort(edges.begin(), edges.end(),
              [](const BwQueryGraphEdge& a, const BwQueryGraphEdge& b) { return a.id < b.id; });

    std::string out = "graph " + direction + "\n";
    if (layout == "detailed") {
        out += "  classDef query fill:#f6f1d3,stroke:#333,stroke-width:2px;\n";
        out += "  classDef row fill:#e8f4ff,stroke:#4a90e2;\n";
        out += "  classDef col fill:#fff0e1,stroke:#e67e22;\n";
        out += "  classDef free fill:#eafaf1,stroke:#27ae60;\n";
        out += "  classDef filt fill:#fdecea,stroke:#c0392b;\n";
        out += "  classDef member fill:#f4ecf7,stroke:#8e44ad;\n";
        out += "  classDef subc fill:#f0f3f4,stroke:#7f8c8d;\n";
    }

    out += "  subgraph Query\n";
    for (const auto& n : nodes) {
        if (n.id != graph.root_node_id) continue;
        out += "    " + n.id + "[\"" + esc(n.label) + "\"]\n";
    }
    out += "  end\n";

    if (layout == "detailed") {
        const auto emit_group = [&](const std::string& title, const std::string& role,
                                    const std::string& class_name) {
            std::vector<std::string> ids;
            out += "  subgraph " + title + "\n";
            for (const auto& n : nodes) {
                if (n.id == graph.root_node_id || ToLower(n.role) != role) continue;
                out += "    " + n.id + "[\"" + esc(n.label) + "\"]\n";
                ids.push_back(n.id);
            }
            out += "  end\n";
            if (!ids.empty()) {
                out += "  class ";
                for (size_t i = 0; i < ids.size(); ++i) {
                    if (i > 0) out += ",";
                    out += ids[i];
                }
                out += " " + class_name + ";\n";
            }
        };

        emit_group("Rows", "rows", "row");
        emit_group("Columns", "columns", "col");
        emit_group("Free", "free", "free");
        emit_group("Filters", "filter", "filt");
        emit_group("Members", "member", "member");
        emit_group("Subcomponents", "subcomponent", "subc");

        std::vector<std::string> other_ids;
        out += "  subgraph References\n";
        for (const auto& n : nodes) {
            if (n.id == graph.root_node_id) continue;
            const auto role = ToLower(n.role);
            if (role == "rows" || role == "columns" || role == "free" ||
                role == "filter" || role == "member" || role == "subcomponent") {
                continue;
            }
            out += "    " + n.id + "[\"" + esc(n.label) + "\"]\n";
            other_ids.push_back(n.id);
        }
        out += "  end\n";
        if (!other_ids.empty()) {
            out += "  class ";
            for (size_t i = 0; i < other_ids.size(); ++i) {
                if (i > 0) out += ",";
                out += other_ids[i];
            }
            out += " subc;\n";
        }
        if (!graph.root_node_id.empty()) {
            out += "  class " + graph.root_node_id + " query;\n";
        }
    } else {
        out += "  subgraph References\n";
        for (const auto& n : nodes) {
            if (n.id == graph.root_node_id) continue;
            out += "    " + n.id + "[\"" + esc(n.label) + "\"]\n";
        }
        out += "  end\n";
    }

    for (const auto& e : edges) {
        if (!e.role.empty()) {
            out += "  " + e.from + " -- \"" + esc(e.role) + "\" --> " + e.to + "\n";
        } else {
            out += "  " + e.from + " --> " + e.to + "\n";
        }
    }
    if (edges.empty() && !graph.root_node_id.empty()) {
        out += "  " + graph.root_node_id + " --> NOREF[\"No references discovered\"]\n";
    }
    out += "\n";
    return out;
}

std::pair<BwQueryGraph, BwQueryGraphReduction> BwReduceQueryGraph(
    const BwQueryGraph& graph,
    const BwQueryGraphReduceOptions& options) {
    BwQueryGraphReduction reduction;
    reduction.focus_role = options.focus_role;
    reduction.max_nodes_per_role = options.max_nodes_per_role;

    if (options.max_nodes_per_role == 0) {
        return {graph, reduction};
    }

    const auto focus_role = options.focus_role.has_value()
        ? std::optional<std::string>(ToLower(*options.focus_role))
        : std::nullopt;

    std::map<std::string, std::vector<std::string>> role_nodes;
    std::set<std::string> existing_node_ids;
    for (const auto& node : graph.nodes) {
        existing_node_ids.insert(node.id);
        if (node.id == graph.root_node_id) continue;
        const auto role = ToLower(node.role);
        if (role.empty()) continue;
        role_nodes[role].push_back(node.id);
    }

    std::map<std::string, std::string> omitted_to_summary_id;
    std::set<std::string> omitted_ids;
    std::vector<BwQueryGraphNode> synthetic_nodes;

    for (auto& [role, ids] : role_nodes) {
        if (focus_role.has_value() && role != *focus_role) continue;
        std::sort(ids.begin(), ids.end());
        if (ids.size() <= options.max_nodes_per_role) continue;

        BwQueryGraphReduceSummary summary;
        summary.role = role;
        summary.kept_node_ids.assign(ids.begin(), ids.begin() + options.max_nodes_per_role);
        summary.omitted_node_ids.assign(ids.begin() + options.max_nodes_per_role, ids.end());

        std::string summary_id = "S_" + SanitizeId(NormalizeComponentType(role)) + "_MORE";
        if (existing_node_ids.count(summary_id) > 0) {
            size_t suffix = 2;
            while (existing_node_ids.count(summary_id + "_" + std::to_string(suffix)) > 0) {
                ++suffix;
            }
            summary_id += "_" + std::to_string(suffix);
        }
        existing_node_ids.insert(summary_id);
        summary.summary_node_id = summary_id;

        const auto omitted_count = summary.omitted_node_ids.size();
        BwQueryGraphNode summary_node;
        summary_node.id = summary_id;
        summary_node.type = "SUMMARY";
        summary_node.name = "+" + std::to_string(omitted_count) + " more " + role;
        summary_node.role = role;
        summary_node.label = "SUMMARY: +" + std::to_string(omitted_count) + " more " + role;
        summary_node.attributes["synthetic"] = "true";
        summary_node.attributes["summary_role"] = role;
        summary_node.attributes["summary_count"] = std::to_string(omitted_count);
        synthetic_nodes.push_back(std::move(summary_node));

        for (const auto& omitted_id : summary.omitted_node_ids) {
            omitted_ids.insert(omitted_id);
            omitted_to_summary_id[omitted_id] = summary_id;
        }
        reduction.summaries.push_back(std::move(summary));
    }

    if (reduction.summaries.empty()) {
        return {graph, reduction};
    }
    reduction.applied = true;

    BwQueryGraph reduced = graph;
    reduced.nodes.clear();
    for (const auto& node : graph.nodes) {
        if (omitted_ids.count(node.id) == 0) {
            reduced.nodes.push_back(node);
        }
    }
    reduced.nodes.insert(reduced.nodes.end(), synthetic_nodes.begin(), synthetic_nodes.end());
    std::sort(reduced.nodes.begin(), reduced.nodes.end(),
              [](const BwQueryGraphNode& a, const BwQueryGraphNode& b) { return a.id < b.id; });

    reduced.edges.clear();
    std::set<std::string> edge_keys;
    for (const auto& edge : graph.edges) {
        const auto from_it = omitted_to_summary_id.find(edge.from);
        const auto to_it = omitted_to_summary_id.find(edge.to);
        const auto from = from_it != omitted_to_summary_id.end() ? from_it->second : edge.from;
        const auto to = to_it != omitted_to_summary_id.end() ? to_it->second : edge.to;
        if (from == to) continue;

        const auto edge_key = from + "|" + to + "|" + edge.type + "|" + edge.role;
        if (edge_keys.count(edge_key) > 0) continue;
        edge_keys.insert(edge_key);

        BwQueryGraphEdge out_edge = edge;
        out_edge.id = "E" + std::to_string(reduced.edges.size() + 1);
        out_edge.from = from;
        out_edge.to = to;
        reduced.edges.push_back(std::move(out_edge));
    }
    return {reduced, reduction};
}

BwQueryGraph BwMergeQueryAndLineageGraphs(
    const BwQueryGraph& query_graph,
    const BwQueryComponentDetail& query_detail,
    const BwLineageGraph& lineage_graph) {
    BwQueryGraph merged = query_graph;
    merged.provenance.push_back("bw.lineage.compose");
    for (const auto& p : lineage_graph.provenance) {
        merged.provenance.push_back(
            "lineage:" + p.operation + ":" + p.status + ":" + p.endpoint);
    }

    std::set<std::string> node_ids;
    std::set<std::string> edge_keys;
    for (const auto& n : merged.nodes) node_ids.insert(n.id);
    for (const auto& e : merged.edges) {
        edge_keys.insert(e.from + "|" + e.to + "|" + e.type + "|" + e.role);
    }

    std::string provider_id;
    if (!query_detail.info_provider.empty()) {
        provider_id = "N_PROVIDER_" + SanitizeId(query_detail.info_provider);
        if (node_ids.count(provider_id) == 0) {
            BwQueryGraphNode provider_node;
            provider_node.id = provider_id;
            provider_node.type = query_detail.info_provider_type.empty()
                ? "INFOPROVIDER"
                : query_detail.info_provider_type;
            provider_node.name = query_detail.info_provider;
            provider_node.role = "provider";
            provider_node.label = provider_node.type + ": " + provider_node.name;
            provider_node.attributes["composed"] = "true";
            merged.nodes.push_back(std::move(provider_node));
            node_ids.insert(provider_id);
        }
        const auto provider_edge_key =
            merged.root_node_id + "|" + provider_id + "|depends_on|provider";
        if (edge_keys.count(provider_edge_key) == 0) {
            BwQueryGraphEdge edge;
            edge.id = "E" + std::to_string(merged.edges.size() + 1);
            edge.from = merged.root_node_id;
            edge.to = provider_id;
            edge.type = "depends_on";
            edge.role = "provider";
            merged.edges.push_back(std::move(edge));
            edge_keys.insert(provider_edge_key);
        }
    }

    std::map<std::string, std::string> lineage_id_map;
    std::string lineage_root_id;
    for (const auto& ln : lineage_graph.nodes) {
        auto mapped_id = "L_" + SanitizeId(ln.id.empty() ? (ln.type + "_" + ln.name) : ln.id);
        if (node_ids.count(mapped_id) > 0) {
            bool reusable = false;
            for (const auto& existing : merged.nodes) {
                if (existing.id == mapped_id && existing.type == ln.type &&
                    existing.name == ln.name) {
                    reusable = true;
                    break;
                }
            }
            if (!reusable) {
                size_t suffix = 2;
                while (node_ids.count(mapped_id + "_" + std::to_string(suffix)) > 0) ++suffix;
                mapped_id += "_" + std::to_string(suffix);
                node_ids.insert(mapped_id);
            }
        } else {
            node_ids.insert(mapped_id);
        }
        lineage_id_map[ln.id] = mapped_id;

        bool exists = false;
        for (const auto& existing : merged.nodes) {
            if (existing.id == mapped_id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            BwQueryGraphNode node;
            node.id = mapped_id;
            node.type = ln.type;
            node.name = ln.name;
            node.role = "upstream_" + (ln.role.empty() ? "lineage" : ln.role);
            node.label = ln.type + ": " + ln.name;
            node.attributes = ln.attributes;
            node.attributes["uri"] = ln.uri;
            node.attributes["version"] = ln.version;
            node.attributes["composed"] = "true";
            merged.nodes.push_back(std::move(node));
        }

        if (NormalizeComponentType(ln.type) == NormalizeComponentType(lineage_graph.root_type) &&
            ln.name == lineage_graph.root_name) {
            lineage_root_id = mapped_id;
        }
    }

    for (const auto& le : lineage_graph.edges) {
        if (lineage_id_map.count(le.from) == 0 || lineage_id_map.count(le.to) == 0) continue;
        const auto from = lineage_id_map[le.from];
        const auto to = lineage_id_map[le.to];
        const auto key = from + "|" + to + "|upstream_lineage|" + le.type;
        if (edge_keys.count(key) > 0) continue;

        BwQueryGraphEdge edge;
        edge.id = "E" + std::to_string(merged.edges.size() + 1);
        edge.from = from;
        edge.to = to;
        edge.type = "upstream_lineage";
        edge.role = le.type;
        edge.attributes = le.attributes;
        edge.attributes["source_lineage_root"] = lineage_graph.root_name;
        merged.edges.push_back(std::move(edge));
        edge_keys.insert(key);
    }

    if (!lineage_root_id.empty()) {
        const auto from = provider_id.empty() ? merged.root_node_id : provider_id;
        const auto key = from + "|" + lineage_root_id + "|upstream_bridge|lineage_root";
        if (edge_keys.count(key) == 0) {
            BwQueryGraphEdge edge;
            edge.id = "E" + std::to_string(merged.edges.size() + 1);
            edge.from = from;
            edge.to = lineage_root_id;
            edge.type = "upstream_bridge";
            edge.role = "lineage_root";
            edge.attributes["source_lineage_root"] = lineage_graph.root_name;
            merged.edges.push_back(std::move(edge));
            edge_keys.insert(key);
        }
    }

    for (const auto& warning : lineage_graph.warnings) {
        merged.warnings.push_back("upstream lineage: " + warning);
    }
    return merged;
}

BwQueryGraphMetrics BwAnalyzeQueryGraph(const BwQueryGraph& graph) {
    BwQueryGraphMetrics metrics;
    metrics.node_count = graph.nodes.size();
    metrics.edge_count = graph.edges.size();

    std::map<std::string, size_t> out_degree;
    for (const auto& edge : graph.edges) {
        out_degree[edge.from] += 1;
    }
    for (const auto& [node_id, degree] : out_degree) {
        if (degree > metrics.max_out_degree) metrics.max_out_degree = degree;
        if (degree > 20) {
            metrics.high_fanout_node_ids.push_back(node_id);
        }
    }
    for (const auto& node : graph.nodes) {
        if (node.type == "SUMMARY") {
            metrics.summary_node_count += 1;
        }
    }
    if (metrics.node_count > 120) {
        metrics.ergonomics_flags.push_back("very_large_graph");
    }
    if (metrics.max_out_degree > 20) {
        metrics.ergonomics_flags.push_back("high_fanout");
    }
    if (metrics.summary_node_count > 0) {
        metrics.ergonomics_flags.push_back("summary_nodes_present");
    }
    if (metrics.ergonomics_flags.empty()) {
        metrics.ergonomics_flags.push_back("ok");
    }
    return metrics;
}

Result<BwQueryGraph, Error> BwAssembleQueryGraph(
    IAdtSession& session,
    const std::string& component_type,
    const std::string& name,
    const std::string& version,
    const std::string& content_type) {
    const auto root_result = BwReadQueryComponent(session, component_type, name, version, content_type);
    if (root_result.IsErr()) {
        return Result<BwQueryGraph, Error>::Err(root_result.Error());
    }
    return BwAssembleQueryGraph(session, root_result.Value(), version);
}

Result<BwQueryGraph, Error> BwAssembleQueryGraph(
    IAdtSession& session,
    const BwQueryComponentDetail& root_detail,
    const std::string& version) {
    const auto node_key = [](const std::string& type, const std::string& obj_name) {
        return NormalizeComponentType(type) + "|" + obj_name;
    };
    const auto node_id = [](const std::string& type, const std::string& obj_name) {
        return "N_" + SanitizeId(NormalizeComponentType(type)) + "_" + SanitizeId(obj_name);
    };

    BwQueryGraph graph;
    graph.schema_version = "1.0";
    graph.root_node_id = node_id(root_detail.component_type, root_detail.name);
    graph.provenance.push_back("bw.read-query");
    graph.provenance.push_back("adt.bw.query-graph-assembler");

    std::map<std::string, size_t> node_index;
    std::set<std::string> visited_components;
    std::set<std::string> edge_seen;

    struct Pending {
        std::string type;
        std::string name;
        std::optional<BwQueryComponentDetail> detail;
    };
    std::vector<Pending> pending;
    pending.push_back(Pending{root_detail.component_type, root_detail.name, root_detail});

    while (!pending.empty()) {
        auto item = pending.back();
        pending.pop_back();
        const auto key = node_key(item.type, item.name);
        if (visited_components.count(key) > 0) continue;
        visited_components.insert(key);

        BwQueryComponentDetail detail;
        if (item.detail.has_value()) {
            detail = *item.detail;
        } else {
            const auto current_result = BwReadQueryComponent(
                session, item.type, item.name, version, "");
            if (current_result.IsErr()) {
                graph.warnings.push_back(
                    "Failed to resolve " + NormalizeComponentType(item.type) + " " + item.name +
                    ": " + current_result.Error().message);
                continue;
            }
            detail = current_result.Value();
        }

        const auto current_node_id = node_id(detail.component_type, detail.name);
        auto it = node_index.find(current_node_id);
        if (it == node_index.end()) {
            BwQueryGraphNode node;
            node.id = current_node_id;
            node.type = detail.component_type;
            node.name = detail.name;
            node.role = (current_node_id == graph.root_node_id) ? "root" : "component";
            node.label = detail.name;
            if (!detail.description.empty()) {
                node.label += "\\n" + detail.description;
            }
            node.attributes = detail.attributes;
            if (!detail.description.empty()) node.attributes["description"] = detail.description;
            if (!detail.info_provider.empty()) node.attributes["info_provider"] = detail.info_provider;
            if (!detail.info_provider_type.empty()) {
                node.attributes["info_provider_type"] = detail.info_provider_type;
            }
            node_index[current_node_id] = graph.nodes.size();
            graph.nodes.push_back(std::move(node));
        } else {
            auto& node = graph.nodes[it->second];
            if (!detail.description.empty()) {
                node.label = detail.name + "\\n" + detail.description;
                node.attributes["description"] = detail.description;
            }
            if (!detail.info_provider.empty()) node.attributes["info_provider"] = detail.info_provider;
            if (!detail.info_provider_type.empty()) {
                node.attributes["info_provider_type"] = detail.info_provider_type;
            }
        }

        graph.provenance.push_back("component:" + NormalizeComponentType(detail.component_type) +
                                   ":" + detail.name);

        for (size_t i = 0; i < detail.references.size(); ++i) {
            const auto& ref = detail.references[i];
            if (ref.name.empty()) continue;
            const auto ref_type = ref.type.empty() ? "REFERENCE" : NormalizeComponentType(ref.type);
            const auto ref_node_id = node_id(ref_type, ref.name);
            if (node_index.count(ref_node_id) == 0) {
                BwQueryGraphNode node;
                node.id = ref_node_id;
                node.type = ref_type;
                node.name = ref.name;
                node.role = ref.role;
                node.label = ref_type + ": " + ref.name;
                node.attributes = ref.attributes;
                node_index[ref_node_id] = graph.nodes.size();
                graph.nodes.push_back(std::move(node));
            }

            const auto edge_key = current_node_id + "|" + ref_node_id + "|" + ref.role + "|" + ref_type;
            if (edge_seen.count(edge_key) == 0) {
                BwQueryGraphEdge edge;
                edge.id = "E" + std::to_string(graph.edges.size() + 1);
                edge.from = current_node_id;
                edge.to = ref_node_id;
                edge.type = "depends_on";
                edge.role = ref.role;
                graph.edges.push_back(std::move(edge));
                edge_seen.insert(edge_key);
            }

            if (IsQueryFamilyType(ref_type)) {
                const auto ref_key = node_key(ref_type, ref.name);
                if (visited_components.count(ref_key) == 0) {
                    pending.push_back(Pending{ref_type, ref.name, std::nullopt});
                }
            }
        }
    }

    if (graph.edges.empty()) {
        graph.warnings.push_back("No references discovered");
    }
    return Result<BwQueryGraph, Error>::Ok(std::move(graph));
}

}  // namespace erpl_adt
