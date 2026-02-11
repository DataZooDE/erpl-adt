#pragma once

#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// XML codec result types — structured data parsed from ADT XML responses.
//
// These are pure data types with no tinyxml2 dependency. They live here
// rather than in a separate header because they are part of the IXmlCodec
// contract and have no consumers outside the xml_codec boundary.
// ---------------------------------------------------------------------------

// Discovery: parsed from Atom Service Document (/sap/bc/adt/discovery).
struct ServiceInfo {
    std::string title;
    std::string href;
    std::string type;
};

struct DiscoveryResult {
    std::vector<ServiceInfo> services;
    bool has_abapgit_support = false;
    bool has_packages_support = false;
    bool has_activation_support = false;
};

// Package: parsed from /sap/bc/adt/packages/{name}.
struct PackageInfo {
    std::string name;
    std::string description;
    std::string software_component;
    std::string uri;
    std::string super_package;
};

// Repo: parsed from /sap/bc/adt/abapgit/repos.
enum class RepoStatusEnum {
    Active,
    Inactive,
    Error,
};

struct RepoInfo {
    std::string key;
    std::string url;
    std::string branch;
    std::string package;
    RepoStatusEnum status = RepoStatusEnum::Inactive;
    std::string status_text;
};

struct RepoStatus {
    std::string key;
    RepoStatusEnum status = RepoStatusEnum::Inactive;
    std::string message;
};

// Inactive objects: parsed from /sap/bc/adt/activation/inactiveobjects.
struct InactiveObject {
    std::string type;
    std::string name;
    std::string uri;
};

// Activation: parsed from activation response.
struct ActivationResult {
    int total = 0;
    int activated = 0;
    int failed = 0;
    std::vector<std::string> error_messages;
};

// Poll: parsed from async operation poll responses (202 follow-up).
enum class XmlPollState {
    Running,
    Completed,
    Failed,
};

struct PollStatusInfo {
    XmlPollState state = XmlPollState::Running;
    std::string message;
};

// ---------------------------------------------------------------------------
// IXmlCodec — abstract interface for building and parsing ADT XML payloads.
//
// Separates XML concerns from HTTP concerns. The concrete implementation
// uses tinyxml2 but no tinyxml2 types appear in this interface.
//
// All methods are const — the codec is stateless.
// All methods return Result<T, Error> — never throw on expected failures.
// ---------------------------------------------------------------------------
class IXmlCodec {
public:
    virtual ~IXmlCodec() = default;

    // Non-copyable, non-movable (polymorphic base).
    IXmlCodec(const IXmlCodec&) = delete;
    IXmlCodec& operator=(const IXmlCodec&) = delete;
    IXmlCodec(IXmlCodec&&) = delete;
    IXmlCodec& operator=(IXmlCodec&&) = delete;

    // -- Build XML request payloads ------------------------------------------

    [[nodiscard]] virtual Result<std::string, Error> BuildPackageCreateXml(
        const PackageName& package_name,
        std::string_view description,
        std::string_view software_component) const = 0;

    [[nodiscard]] virtual Result<std::string, Error> BuildRepoCloneXml(
        const RepoUrl& repo_url,
        const BranchRef& branch,
        const PackageName& package_name) const = 0;

    [[nodiscard]] virtual Result<std::string, Error> BuildActivationXml(
        const std::vector<InactiveObject>& objects) const = 0;

    // -- Parse XML response payloads -----------------------------------------

    [[nodiscard]] virtual Result<DiscoveryResult, Error> ParseDiscoveryResponse(
        std::string_view xml) const = 0;

    [[nodiscard]] virtual Result<PackageInfo, Error> ParsePackageResponse(
        std::string_view xml) const = 0;

    [[nodiscard]] virtual Result<std::vector<RepoInfo>, Error> ParseRepoListResponse(
        std::string_view xml) const = 0;

    [[nodiscard]] virtual Result<RepoStatus, Error> ParseRepoStatusResponse(
        std::string_view xml) const = 0;

    [[nodiscard]] virtual Result<ActivationResult, Error> ParseActivationResponse(
        std::string_view xml) const = 0;

    [[nodiscard]] virtual Result<std::vector<InactiveObject>, Error> ParseInactiveObjectsResponse(
        std::string_view xml) const = 0;

    [[nodiscard]] virtual Result<PollStatusInfo, Error> ParsePollResponse(
        std::string_view xml) const = 0;

protected:
    IXmlCodec() = default;
};

} // namespace erpl_adt
