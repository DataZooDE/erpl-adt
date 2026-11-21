#include <erpl_adt/adt/activation.hpp>
#include <erpl_adt/adt/function_group.hpp>

#include "atom_parser.hpp"
#include "protocol_kernel.hpp"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <string>

namespace erpl_adt {

namespace {

const char* kInactivePath = "/sap/bc/adt/activation/inactive";
const char* kActivationPath = "/sap/bc/adt/activation";
const char* kActivationContentType = "application/vnd.sap.adt.activation.v1+xml";
const char* kActivateObjectPath = "/sap/bc/adt/activation?method=activate&preauditRequested=true";
constexpr const char* kNsAdtCore = "http://www.sap.com/adt/core";

std::string BuildActivationRequestXml(const std::vector<ActivateObjectParams>& objects) {
    tinyxml2::XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    auto* root = doc.NewElement("adtcore:objectReferences");
    root->SetAttribute("xmlns:adtcore", kNsAdtCore);
    doc.InsertEndChild(root);

    for (const auto& params : objects) {
        auto* ref = doc.NewElement("adtcore:objectReference");
        ref->SetAttribute("adtcore:uri", params.uri.c_str());
        if (!params.type.empty()) {
            ref->SetAttribute("adtcore:type", params.type.c_str());
        }
        if (!params.name.empty()) {
            ref->SetAttribute("adtcore:name", params.name.c_str());
        }
        if (!params.parent_uri.empty()) {
            ref->SetAttribute("adtcore:parentUri", params.parent_uri.c_str());
        }
        root->InsertEndChild(ref);
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr());
}

// Objects of a pre-audit list (ioc:inactiveObjects): what SAP wants activated
// together with the requested object, each once. An entry without an object
// only names the request the objects are recorded on.
std::string LowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct PreauditCompanion {
    std::string label;  // "TYPE NAME"
    std::string uri;
};

std::vector<PreauditCompanion> PreauditCompanions(const tinyxml2::XMLElement* list) {
    std::vector<PreauditCompanion> out;
    for (auto* entry = list->FirstChildElement(); entry;
         entry = entry->NextSiblingElement()) {
        if (!atom_parser::HasLocalName(entry, "entry")) continue;
        const auto* object = atom_parser::FirstChildByLocalName(entry, "object");
        const auto* ref = atom_parser::FirstChildByLocalName(object, "ref");
        if (!ref) continue;
        const char* type = ref->Attribute("adtcore:type");
        const char* name = ref->Attribute("adtcore:name");
        const char* uri = ref->Attribute("adtcore:uri");
        PreauditCompanion item{std::string(type ? type : "") + " " + (name ? name : ""),
                               uri ? uri : ""};
        // Keyed by URI: SAP labels a group and its main program alike
        // ("FUGR/F <group>"), so a label would merge two objects (crew F1).
        const bool seen = std::any_of(out.begin(), out.end(), [&item](const auto& c) {
            return item.uri.empty() ? c.label == item.label
                                    : LowerAscii(c.uri) == LowerAscii(item.uri);
        });
        if (!seen) {
            out.push_back(std::move(item));
        }
    }
    return out;
}

ActivationResult ParseActivationResultXml(std::string_view xml) {
    ActivationResult result;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return result;
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return result;
    }

    // SAP Cloud returns chkl:messages as the root element directly.
    // Older/test format wraps it: <chkl:activationResultList><chkl:messages>...
    // Detect which format we have and resolve the messages container.
    const tinyxml2::XMLElement* messages = nullptr;
    const char* root_name = root->Name();
    if (root_name && std::string_view(root_name).find("messages") != std::string_view::npos) {
        // Root IS the messages container (SAP Cloud format).
        messages = root;
    } else {
        // Wrapped format: look for chkl:messages child.
        messages = root->FirstChildElement("chkl:messages");
    }

    if (messages) {
        // Parse <chkl:properties activationExecuted="true"> if present.
        const auto* props = messages->FirstChildElement("chkl:properties");
        if (props) {
            const char* activated_attr = props->Attribute("activationExecuted");
            if (activated_attr && std::string_view(activated_attr) == "true") {
                result.activation_executed = true;
            }
        }

        // Parse <msg> children.
        for (auto* msg = messages->FirstChildElement("msg"); msg;
             msg = msg->NextSiblingElement("msg")) {

            const char* msg_type = msg->Attribute("type");
            std::string type_str = msg_type ? msg_type : "";

            // Extract text from <shortText><txt> child.
            const auto* short_text = msg->FirstChildElement("shortText");
            if (short_text) {
                const auto* txt = short_text->FirstChildElement("txt");
                if (txt && txt->GetText()) {
                    result.error_messages.push_back(txt->GetText());
                }
            }

            ++result.total;
            if (type_str == "E" || type_str == "A") {
                ++result.failed;
            } else {
                ++result.activated;
            }
        }
    }

    // Inactive objects reported inside an ordinary result: nothing of them was
    // activated; SAP's messages above say why (crew F3).
    if (!atom_parser::HasLocalName(root, "inactiveObjects")) {
        if (const auto* inactive = atom_parser::FirstChildByLocalName(root, "inactiveObjects")) {
            const auto entries = PreauditCompanions(inactive);
            result.failed += static_cast<int>(entries.size());
            result.total += static_cast<int>(entries.size());
        }
    }

    return result;
}

// The entries of an ioc:inactiveObjects document (the pre-audit answer of an
// activation, or GET /activation/inactiveobjects): nullopt when the body is
// not such a document, an empty list when it lists nothing.
std::optional<std::vector<PreauditCompanion>> ParseInactiveObjectsDocument(
    std::string_view xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS || !doc.RootElement() ||
        !atom_parser::HasLocalName(doc.RootElement(), "inactiveObjects")) {
        return std::nullopt;
    }
    return PreauditCompanions(doc.RootElement());
}

// A pre-audit answer (#82): the activation's answer is an inactiveObjects
// document naming objects. An inactiveObjects element inside chkl:messages
// is part of an ordinary result and is read there, with SAP's messages
// (crew F3).
std::optional<std::vector<PreauditCompanion>> ParsePreaudit(std::string_view xml) {
    auto companions = ParseInactiveObjectsDocument(xml);
    if (!companions || companions->empty()) {
        return std::nullopt;
    }
    return companions;
}

constexpr const char* kInactiveObjectsPath = "/sap/bc/adt/activation/inactiveobjects";

// SAP answered a pre-audit list instead of activating. The list looks the
// same whether anything is inactive or not (measured on A4H), so the user's
// inactive objects decide: none of the requested objects or their companions
// inactive is "nothing to activate"; otherwise nothing was activated, and the
// message names what is inactive and the one call that activates it.
ActivationResult ResolvePreaudit(IAdtSession& session,
                                 const std::vector<ActivateObjectParams>& objects,
                                 const std::vector<PreauditCompanion>& companions) {
    std::set<std::string> relevant;
    for (const auto& o : objects) relevant.insert(LowerAscii(o.uri));
    for (const auto& c : companions) {
        if (!c.uri.empty()) relevant.insert(LowerAscii(c.uri));
    }

    std::vector<PreauditCompanion> inactive;
    std::string unknown;  // why the inactive objects could not be read
    HttpHeaders headers;
    headers["Accept"] = "application/vnd.sap.adt.inactivectsobjects.v1+xml, application/xml";
    auto response = session.Get(kInactiveObjectsPath, headers);
    if (response.IsErr()) {
        unknown = response.Error().message;
    } else if (response.Value().status_code != 200) {
        unknown = "HTTP " + std::to_string(response.Value().status_code);
    } else if (auto listed = ParseInactiveObjectsDocument(response.Value().body)) {
        for (auto& entry : *listed) {
            if (relevant.count(LowerAscii(entry.uri)) > 0) inactive.push_back(std::move(entry));
        }
    } else {
        unknown = "the answer is not an inactive-objects list";
    }
    if (!unknown.empty()) {
        inactive = companions;  // cannot tell: report what SAP listed (crew F5)
    }

    ActivationResult result;
    if (inactive.empty()) {
        return result;  // nothing to activate
    }
    std::string labels;
    std::string uris;
    for (const auto& c : inactive) {
        labels += (labels.empty() ? "" : ", ") + c.label;
        uris += " " + c.uri;
    }
    result.total = static_cast<int>(inactive.size());
    result.failed = result.total;
    result.error_messages.push_back(
        "Not activated: SAP activates this only together with what is inactive with it: " +
        labels + ". Activate them in one request (CLI: erpl-adt activate" + uris +
        "; MCP: adt_activate with uris)." +
        (unknown.empty() ? "" : " (SAP listed these; could not read the inactive objects: " +
                                    unknown + ")"));
    return result;
}

} // namespace

Result<std::vector<InactiveObject>, Error> GetInactiveObjects(
    IAdtSession& session,
    const IXmlCodec& codec) {

    auto response = session.Get(kInactivePath);
    if (response.IsErr()) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            std::move(response).Error());
    }

    const auto& http = response.Value();
    if (http.status_code != 200) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            Error::FromHttpStatus("GetInactiveObjects", kInactivePath, http.status_code, http.body));
    }

    return codec.ParseInactiveObjectsResponse(http.body);
}

Result<ActivationResult, Error> ActivateAll(
    IAdtSession& session,
    const IXmlCodec& codec,
    const std::vector<InactiveObject>& objects,
    std::chrono::seconds timeout) {

    // Nothing to activate — return a zero-count success.
    if (objects.empty()) {
        return Result<ActivationResult, Error>::Ok(
            ActivationResult{0, 0, 0, {}});
    }

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(csrf).Error());
    }

    auto xml = codec.BuildActivationXml(objects);
    if (xml.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(xml).Error());
    }

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kActivationPath, xml.Value(), kActivationContentType, headers);
    if (response.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Async: 202 + Location → poll until complete.
    if (http.status_code == 202) {
        auto poll = protocol_kernel::PollAcceptedOperation(
            session,
            http,
            timeout,
            protocol_kernel::AsyncContract{
                "ActivateAll",
                kActivationPath,
                "async activation operation failed",
                "async activation operation did not complete within timeout",
                ErrorCategory::ActivationError});
        if (poll.IsErr()) {
            return Result<ActivationResult, Error>::Err(std::move(poll).Error());
        }

        return codec.ParseActivationResponse(poll.Value().body);
    }

    // Synchronous: 200 with activation result.
    if (http.status_code == 200) {
        return codec.ParseActivationResponse(http.body);
    }

    return Result<ActivationResult, Error>::Err(
        Error::FromHttpStatus("ActivateAll", kActivationPath, http.status_code, http.body));
}

Result<ActivationResult, Error> ActivateObject(
    IAdtSession& session,
    const ActivateObjectParams& params,
    std::chrono::seconds timeout) {
    return ActivateObjects(session, {params}, timeout);
}

Result<ActivationResult, Error> ActivateObjects(
    IAdtSession& session,
    const std::vector<ActivateObjectParams>& objects,
    std::chrono::seconds timeout) {

    if (objects.empty() ||
        std::any_of(objects.begin(), objects.end(),
                    [](const ActivateObjectParams& p) { return p.uri.empty(); })) {
        return Result<ActivationResult, Error>::Err(Error{
            "ActivateObject", "", std::nullopt,
            "URI is required for activation", std::nullopt});
    }

    auto csrf = session.FetchCsrfToken();
    if (csrf.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(csrf).Error());
    }

    auto xml = BuildActivationRequestXml(objects);

    HttpHeaders headers = {{"x-csrf-token", csrf.Value()}};
    auto response = session.Post(kActivateObjectPath, xml, kActivationContentType, headers);
    if (response.IsErr()) {
        return Result<ActivationResult, Error>::Err(std::move(response).Error());
    }

    const auto& http = response.Value();

    // Async: 202 + Location -> poll until complete.
    if (http.status_code == 202) {
        auto poll = protocol_kernel::PollAcceptedOperation(
            session,
            http,
            timeout,
            protocol_kernel::AsyncContract{
                "ActivateObject",
                kActivateObjectPath,
                "async activation operation failed",
                "async activation operation did not complete within timeout",
                ErrorCategory::ActivationError});
        if (poll.IsErr()) {
            return Result<ActivationResult, Error>::Err(std::move(poll).Error());
        }

        if (auto preaudit = ParsePreaudit(poll.Value().body)) {
            return Result<ActivationResult, Error>::Ok(
                ResolvePreaudit(session, objects, *preaudit));
        }
        return Result<ActivationResult, Error>::Ok(ParseActivationResultXml(
            poll.Value().body));
    }

    // Synchronous: 200 with activation result.
    if (http.status_code == 200) {
        if (auto preaudit = ParsePreaudit(http.body)) {
            return Result<ActivationResult, Error>::Ok(
                ResolvePreaudit(session, objects, *preaudit));
        }
        return Result<ActivationResult, Error>::Ok(
            ParseActivationResultXml(http.body));
    }

    return Result<ActivationResult, Error>::Err(
        Error::FromHttpStatus("ActivateObject", kActivateObjectPath, http.status_code, http.body));
}

namespace {

struct UriTypeMapping {
    const char* path_segment;  // after "/sap/bc/adt/"
    const char* type;
};

// Longer (more specific) prefixes first: the parts of a function group
// must win over the group.
constexpr UriTypeMapping kUriTypeMap[] = {
    {"programs/programs/", "PROG/P"},
    {"programs/includes/", "PROG/I"},
    {"oo/classes/", "CLAS/OC"},
    {"oo/interfaces/", "INTF/OI"},
    {"functions/groups/", "FUGR/F"},
    {"packages/", "DEVC/K"},
    {"ddic/ddl/sources/", "DDLS/DF"},
    {"ddic/tables/", "TABL/DT"},
    {"ddic/dataelements/", "DTEL/DE"},
    {"messageclass/", "MSAG/N"},
};

// One URI segment as an ABAP name: "%2fdmo%2ffm" -> "/DMO/FM".
std::string SegmentToName(const std::string& segment) {
    std::string out;
    for (size_t i = 0; i < segment.size(); ++i) {
        if (i + 2 < segment.size() &&
            (segment.compare(i, 3, "%2f") == 0 || segment.compare(i, 3, "%2F") == 0)) {
            out += '/';
            i += 2;
        } else {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(segment[i])));
        }
    }
    return out;
}

std::string FirstSegment(const std::string& path) {
    return path.substr(0, path.find('/'));
}

}  // namespace

ActivateObjectParams ActivationParamsFromObjectUri(const std::string& object_uri) {
    ActivateObjectParams params;
    params.uri = object_uri;

    const std::string base = "/sap/bc/adt/";
    if (object_uri.compare(0, base.size(), base) != 0) {
        return params;
    }
    const std::string tail = object_uri.substr(base.size());

    for (const auto& m : kUriTypeMap) {
        const std::string seg(m.path_segment);
        if (tail.size() <= seg.size() || tail.compare(0, seg.size(), seg) != 0) {
            continue;
        }
        const std::string rest = tail.substr(seg.size());  // "<name>[/...]"
        const std::string first = FirstSegment(rest);
        params.type = m.type;
        params.name = SegmentToName(first);

        if (params.type == "FUGR/F" && first.size() < rest.size()) {
            // ".../groups/<g>/fmodules/<fm>" or ".../groups/<g>/includes/<incl>"
            const std::string below = rest.substr(first.size() + 1);
            const std::string kind = FirstSegment(below);
            const std::string part =
                kind.size() < below.size() ? FirstSegment(below.substr(kind.size() + 1)) : "";
            if (!part.empty() && (kind == "fmodules" || kind == "includes")) {
                params.type = kind == "fmodules" ? "FUGR/FF" : "FUGR/I";
                params.name = SegmentToName(part);
                params.parent_uri = base + seg + first;
            } else if (kind == "source") {
                // ".../groups/<g>/source/main": the main program SAPL<g>
                // (/NS/SAPL<g> for a namespaced group), as ADT lists it.
                params.type = "FUGR/I";
                params.name = FunctionGroupMainProgramName(SegmentToName(first));
                params.parent_uri = base + seg + first;
            }
        }
        break;
    }
    return params;
}

} // namespace erpl_adt
