#include <erpl_adt/adt/xml_codec.hpp>

#include <tinyxml2.h>

#include <sstream>
#include <string>

namespace erpl_adt {

namespace {

// ADT XML namespace URIs used in build methods.
constexpr const char* kNsAdtCore = "http://www.sap.com/adt/core";
constexpr const char* kNsPackages = "http://www.sap.com/adt/packages";
constexpr const char* kNsAbapGitRepo = "http://www.sap.com/adt/abapgit/repositories";

Error XmlError(std::string_view operation, std::string_view message) {
    return Error{std::string(operation), "", std::nullopt, std::string(message), std::nullopt};
}

// Serialize a tinyxml2 document to a string.
std::string DocumentToString(const tinyxml2::XMLDocument& doc) {
    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    return std::string(printer.CStr());
}

// Parse XML from a string_view into the provided document.
// Returns nullopt on success, or an Error on parse failure.
std::optional<Error> ParseXml(tinyxml2::XMLDocument& doc,
                              std::string_view xml,
                              std::string_view operation) {
    auto err = doc.Parse(xml.data(), xml.size());
    if (err != tinyxml2::XML_SUCCESS) {
        return XmlError(operation, std::string("XML parse error: ") + doc.ErrorStr());
    }
    return std::nullopt;
}

// Safe text extraction from a child element (returns empty string if missing).
std::string ChildText(const tinyxml2::XMLElement* parent, const char* child_name) {
    if (!parent) return "";
    const auto* child = parent->FirstChildElement(child_name);
    if (!child) return "";
    const char* text = child->GetText();
    return text ? text : "";
}

// Safe attribute extraction (returns empty string if missing).
std::string Attr(const tinyxml2::XMLElement* elem, const char* attr_name) {
    if (!elem) return "";
    const char* val = elem->Attribute(attr_name);
    return val ? val : "";
}

// Map single-letter status code to RepoStatusEnum.
RepoStatusEnum ParseRepoStatusCode(std::string_view code) {
    if (code == "A") return RepoStatusEnum::Active;
    if (code == "E") return RepoStatusEnum::Error;
    // Default: Inactive covers "I", "C" (Cloned), and unknown codes.
    return RepoStatusEnum::Inactive;
}

// Parse a single <abapgitrepo:repository> element into RepoInfo.
RepoInfo ParseSingleRepoElement(const tinyxml2::XMLElement* repo_elem) {
    RepoInfo info;
    info.key = ChildText(repo_elem, "abapgitrepo:key");
    info.package = ChildText(repo_elem, "abapgitrepo:package");
    info.url = ChildText(repo_elem, "abapgitrepo:url");
    info.branch = ChildText(repo_elem, "abapgitrepo:branchName");

    auto status_code = ChildText(repo_elem, "abapgitrepo:status");
    info.status = ParseRepoStatusCode(status_code);
    info.status_text = ChildText(repo_elem, "abapgitrepo:statusText");

    return info;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

XmlCodec::XmlCodec() = default;
XmlCodec::~XmlCodec() = default;

// ---------------------------------------------------------------------------
// Build methods
// ---------------------------------------------------------------------------

Result<std::string, Error> XmlCodec::BuildPackageCreateXml(
    const PackageName& package_name,
    std::string_view description,
    std::string_view software_component) const {

    tinyxml2::XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    auto* root = doc.NewElement("pak:package");
    root->SetAttribute("xmlns:pak", kNsPackages);
    root->SetAttribute("xmlns:adtcore", kNsAdtCore);
    root->SetAttribute("adtcore:description", std::string(description).c_str());
    root->SetAttribute("adtcore:name", package_name.Value().c_str());
    root->SetAttribute("adtcore:type", "DEVC/K");
    root->SetAttribute("adtcore:version", "active");
    root->SetAttribute("adtcore:responsible", "DEVELOPER");
    doc.InsertEndChild(root);

    auto* pkg_ref = doc.NewElement("adtcore:packageRef");
    pkg_ref->SetAttribute("adtcore:name", "$TMP");
    root->InsertEndChild(pkg_ref);

    auto* attrs = doc.NewElement("pak:attributes");
    attrs->SetAttribute("pak:packageType", "development");
    root->InsertEndChild(attrs);

    auto* super_pkg = doc.NewElement("pak:superPackage");
    super_pkg->SetAttribute("adtcore:name", "$TMP");
    root->InsertEndChild(super_pkg);

    root->InsertEndChild(doc.NewElement("pak:applicationComponent"));

    auto* transport = doc.NewElement("pak:transport");
    auto* sw_comp = doc.NewElement("pak:softwareComponent");
    sw_comp->SetAttribute("pak:name",
        software_component.empty() ? "LOCAL" : std::string(software_component).c_str());
    transport->InsertEndChild(sw_comp);
    auto* layer = doc.NewElement("pak:transportLayer");
    layer->SetAttribute("pak:name", "");
    transport->InsertEndChild(layer);
    root->InsertEndChild(transport);

    root->InsertEndChild(doc.NewElement("pak:translation"));
    root->InsertEndChild(doc.NewElement("pak:useAccesses"));
    root->InsertEndChild(doc.NewElement("pak:packageInterfaces"));
    root->InsertEndChild(doc.NewElement("pak:subPackages"));

    return Result<std::string, Error>::Ok(DocumentToString(doc));
}

Result<std::string, Error> XmlCodec::BuildRepoCloneXml(
    const RepoUrl& repo_url,
    const BranchRef& branch,
    const PackageName& package_name) const {

    tinyxml2::XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    auto* root = doc.NewElement("abapgitrepo:repository");
    root->SetAttribute("xmlns:abapgitrepo", kNsAbapGitRepo);
    doc.InsertEndChild(root);

    auto add_child = [&](const char* name, const char* text) {
        auto* elem = doc.NewElement(name);
        elem->SetText(text);
        root->InsertEndChild(elem);
    };

    add_child("abapgitrepo:package", package_name.Value().c_str());
    add_child("abapgitrepo:url", repo_url.Value().c_str());
    add_child("abapgitrepo:branchName", branch.Value().c_str());

    // Empty optional fields required by the schema.
    auto add_empty = [&](const char* name) {
        root->InsertEndChild(doc.NewElement(name));
    };

    add_empty("abapgitrepo:transportRequest");
    add_empty("abapgitrepo:remoteUser");
    add_empty("abapgitrepo:remotePassword");

    return Result<std::string, Error>::Ok(DocumentToString(doc));
}

Result<std::string, Error> XmlCodec::BuildActivationXml(
    const std::vector<InactiveObject>& objects) const {

    tinyxml2::XMLDocument doc;
    doc.InsertFirstChild(doc.NewDeclaration());

    auto* root = doc.NewElement("adtcore:objectReferences");
    root->SetAttribute("xmlns:adtcore", kNsAdtCore);
    doc.InsertEndChild(root);

    for (const auto& obj : objects) {
        auto* ref = doc.NewElement("adtcore:objectReference");
        ref->SetAttribute("adtcore:uri", obj.uri.c_str());
        ref->SetAttribute("adtcore:type", obj.type.c_str());
        ref->SetAttribute("adtcore:name", obj.name.c_str());
        root->InsertEndChild(ref);
    }

    return Result<std::string, Error>::Ok(DocumentToString(doc));
}

// ---------------------------------------------------------------------------
// Parse methods
// ---------------------------------------------------------------------------

Result<DiscoveryResult, Error> XmlCodec::ParseDiscoveryResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParseDiscoveryResponse")) {
        return Result<DiscoveryResult, Error>::Err(std::move(*err));
    }

    DiscoveryResult result;

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<DiscoveryResult, Error>::Err(
            XmlError("ParseDiscoveryResponse", "Empty document"));
    }

    // Iterate <app:workspace> elements.
    for (auto* ws = root->FirstChildElement("app:workspace"); ws;
         ws = ws->NextSiblingElement("app:workspace")) {

        // Iterate <app:collection> elements within each workspace.
        for (auto* coll = ws->FirstChildElement("app:collection"); coll;
             coll = coll->NextSiblingElement("app:collection")) {

            ServiceInfo service;
            service.href = Attr(coll, "href");

            // Title is in <atom:title> child.
            const auto* title_elem = coll->FirstChildElement("atom:title");
            service.title = title_elem && title_elem->GetText()
                                ? title_elem->GetText()
                                : "";

            // Try to get type from templateLinks if present.
            const auto* tmpl_links = coll->FirstChildElement("adtcomp:templateLinks");
            if (tmpl_links) {
                const auto* tmpl_link = tmpl_links->FirstChildElement("adtcomp:templateLink");
                if (tmpl_link) {
                    service.type = Attr(tmpl_link, "type");
                }
            }

            // Detect capability flags based on collection href (before move).
            if (service.href.find("/abapgit/repos") != std::string::npos) {
                result.has_abapgit_support = true;
            }
            if (service.href.find("/packages") != std::string::npos) {
                result.has_packages_support = true;
            }
            if (service.href == "/sap/bc/adt/activation") {
                result.has_activation_support = true;
            }

            result.services.push_back(std::move(service));
        }
    }

    return Result<DiscoveryResult, Error>::Ok(std::move(result));
}

Result<PackageInfo, Error> XmlCodec::ParsePackageResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParsePackageResponse")) {
        return Result<PackageInfo, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<PackageInfo, Error>::Err(
            XmlError("ParsePackageResponse", "Empty document"));
    }

    PackageInfo info;
    info.name = Attr(root, "adtcore:name");
    info.description = Attr(root, "adtcore:description");
    info.uri = Attr(root, "adtcore:uri");

    // Super package.
    const auto* super_pkg = root->FirstChildElement("pak:superPackage");
    if (super_pkg) {
        info.super_package = Attr(super_pkg, "adtcore:name");
    }

    // Software component from transport section.
    const auto* transport = root->FirstChildElement("pak:transport");
    if (transport) {
        const auto* sw_comp = transport->FirstChildElement("pak:softwareComponent");
        if (sw_comp) {
            info.software_component = Attr(sw_comp, "pak:name");
        }
    }

    return Result<PackageInfo, Error>::Ok(std::move(info));
}

Result<std::vector<RepoInfo>, Error> XmlCodec::ParseRepoListResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParseRepoListResponse")) {
        return Result<std::vector<RepoInfo>, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<RepoInfo>, Error>::Err(
            XmlError("ParseRepoListResponse", "Empty document"));
    }

    std::vector<RepoInfo> repos;

    for (auto* repo_elem = root->FirstChildElement("abapgitrepo:repository"); repo_elem;
         repo_elem = repo_elem->NextSiblingElement("abapgitrepo:repository")) {
        repos.push_back(ParseSingleRepoElement(repo_elem));
    }

    return Result<std::vector<RepoInfo>, Error>::Ok(std::move(repos));
}

Result<RepoStatus, Error> XmlCodec::ParseRepoStatusResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParseRepoStatusResponse")) {
        return Result<RepoStatus, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<RepoStatus, Error>::Err(
            XmlError("ParseRepoStatusResponse", "Empty document"));
    }

    RepoStatus status;
    status.key = ChildText(root, "abapgitrepo:key");

    auto status_code = ChildText(root, "abapgitrepo:status");
    status.status = ParseRepoStatusCode(status_code);
    status.message = ChildText(root, "abapgitrepo:statusText");

    return Result<RepoStatus, Error>::Ok(std::move(status));
}

Result<ActivationResult, Error> XmlCodec::ParseActivationResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParseActivationResponse")) {
        return Result<ActivationResult, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<ActivationResult, Error>::Err(
            XmlError("ParseActivationResponse", "Empty document"));
    }

    ActivationResult result;

    // Parse messages.
    const auto* messages = root->FirstChildElement("chkl:messages");
    if (messages) {
        for (auto* msg = messages->FirstChildElement("msg"); msg;
             msg = msg->NextSiblingElement("msg")) {

            std::string msg_type = Attr(msg, "type");

            // Extract text from <shortText><txt> child.
            const auto* short_text = msg->FirstChildElement("shortText");
            if (short_text) {
                const auto* txt = short_text->FirstChildElement("txt");
                if (txt && txt->GetText()) {
                    result.error_messages.push_back(txt->GetText());
                }
            }

            // Count errors vs warnings for totals.
            ++result.total;
            if (msg_type == "E" || msg_type == "A") {
                ++result.failed;
            } else {
                ++result.activated;
            }
        }
    }

    // Check if there are remaining inactive objects (indicates partial failure).
    const auto* inactive = root->FirstChildElement("ioc:inactiveObjects");
    if (inactive && inactive->FirstChildElement("ioc:entry")) {
        // Count remaining inactive objects as failures.
        for (auto* entry = inactive->FirstChildElement("ioc:entry"); entry;
             entry = entry->NextSiblingElement("ioc:entry")) {
            ++result.failed;
            ++result.total;
        }
    }

    return Result<ActivationResult, Error>::Ok(std::move(result));
}

Result<std::vector<InactiveObject>, Error> XmlCodec::ParseInactiveObjectsResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParseInactiveObjectsResponse")) {
        return Result<std::vector<InactiveObject>, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<std::vector<InactiveObject>, Error>::Err(
            XmlError("ParseInactiveObjectsResponse", "Empty document"));
    }

    std::vector<InactiveObject> objects;

    for (auto* entry = root->FirstChildElement("ioc:entry"); entry;
         entry = entry->NextSiblingElement("ioc:entry")) {

        const auto* obj = entry->FirstChildElement("ioc:object");
        if (!obj) continue;

        const auto* ref = obj->FirstChildElement("ioc:ref");
        if (!ref) continue;

        InactiveObject inactive;
        inactive.type = Attr(ref, "adtcore:type");
        inactive.name = Attr(ref, "adtcore:name");
        inactive.uri = Attr(ref, "adtcore:uri");

        objects.push_back(std::move(inactive));
    }

    return Result<std::vector<InactiveObject>, Error>::Ok(std::move(objects));
}

Result<PollStatusInfo, Error> XmlCodec::ParsePollResponse(
    std::string_view xml) const {

    tinyxml2::XMLDocument doc;
    if (auto err = ParseXml(doc, xml, "ParsePollResponse")) {
        return Result<PollStatusInfo, Error>::Err(std::move(*err));
    }

    const auto* root = doc.RootElement();
    if (!root) {
        return Result<PollStatusInfo, Error>::Err(
            XmlError("ParsePollResponse", "Empty document"));
    }

    PollStatusInfo info;

    std::string status_str = Attr(root, "adtcore:status");
    if (status_str == "completed") {
        info.state = XmlPollState::Completed;
    } else if (status_str == "failed") {
        info.state = XmlPollState::Failed;
    } else {
        info.state = XmlPollState::Running;
    }

    // Description text.
    const auto* desc = root->FirstChildElement("adtcore:description");
    if (desc && desc->GetText()) {
        info.message = desc->GetText();
    }

    // If failed, append progress text which may contain error details.
    if (info.state == XmlPollState::Failed) {
        const auto* progress = root->FirstChildElement("adtcore:progress");
        if (progress) {
            std::string progress_text = Attr(progress, "adtcore:text");
            if (!progress_text.empty()) {
                if (!info.message.empty()) info.message += ": ";
                info.message += progress_text;
            }
        }
    }

    return Result<PollStatusInfo, Error>::Ok(std::move(info));
}

} // namespace erpl_adt
