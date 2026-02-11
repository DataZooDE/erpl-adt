#pragma once

#include <erpl_adt/adt/i_xml_codec.hpp>

#include <memory>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// XmlCodec — concrete IXmlCodec implementation backed by tinyxml2.
//
// All methods are const and stateless. tinyxml2 types do NOT appear in this
// header — the implementation is fully encapsulated in the .cpp file.
// ---------------------------------------------------------------------------
class XmlCodec : public IXmlCodec {
public:
    XmlCodec();
    ~XmlCodec() override;

    // Non-copyable, non-movable (inherited from IXmlCodec).

    // -- Build XML request payloads ------------------------------------------

    [[nodiscard]] Result<std::string, Error> BuildPackageCreateXml(
        const PackageName& package_name,
        std::string_view description,
        std::string_view software_component) const override;

    [[nodiscard]] Result<std::string, Error> BuildRepoCloneXml(
        const RepoUrl& repo_url,
        const BranchRef& branch,
        const PackageName& package_name) const override;

    [[nodiscard]] Result<std::string, Error> BuildActivationXml(
        const std::vector<InactiveObject>& objects) const override;

    // -- Parse XML response payloads -----------------------------------------

    [[nodiscard]] Result<DiscoveryResult, Error> ParseDiscoveryResponse(
        std::string_view xml) const override;

    [[nodiscard]] Result<PackageInfo, Error> ParsePackageResponse(
        std::string_view xml) const override;

    [[nodiscard]] Result<std::vector<RepoInfo>, Error> ParseRepoListResponse(
        std::string_view xml) const override;

    [[nodiscard]] Result<RepoStatus, Error> ParseRepoStatusResponse(
        std::string_view xml) const override;

    [[nodiscard]] Result<ActivationResult, Error> ParseActivationResponse(
        std::string_view xml) const override;

    [[nodiscard]] Result<std::vector<InactiveObject>, Error> ParseInactiveObjectsResponse(
        std::string_view xml) const override;

    [[nodiscard]] Result<PollStatusInfo, Error> ParsePollResponse(
        std::string_view xml) const override;
};

} // namespace erpl_adt
