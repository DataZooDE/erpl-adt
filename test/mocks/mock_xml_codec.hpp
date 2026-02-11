#pragma once

#include <erpl_adt/adt/i_xml_codec.hpp>

#include <functional>
#include <string>
#include <vector>

namespace erpl_adt::testing {

// ---------------------------------------------------------------------------
// MockXmlCodec — hand-written mock implementing IXmlCodec.
//
// Default behaviour: every method returns an error. Use the Set*Response
// methods to configure canned return values. Call history is tracked for
// verification in tests.
// ---------------------------------------------------------------------------
class MockXmlCodec : public IXmlCodec {
public:
    MockXmlCodec() = default;

    // -- Call history --------------------------------------------------------

    struct CallRecord {
        std::string method;
        std::vector<std::string> args;
    };

    [[nodiscard]] const std::vector<CallRecord>& Calls() const noexcept { return calls_; }

    [[nodiscard]] size_t CallCount(std::string_view method) const {
        size_t count = 0;
        for (const auto& c : calls_) {
            if (c.method == method) {
                ++count;
            }
        }
        return count;
    }

    void ClearCalls() { calls_.clear(); }

    // -- Canned response setters ---------------------------------------------

    void SetBuildPackageCreateXmlResponse(Result<std::string, Error> response) {
        build_package_create_response_ = std::move(response);
    }

    void SetBuildRepoCloneXmlResponse(Result<std::string, Error> response) {
        build_repo_clone_response_ = std::move(response);
    }

    void SetBuildActivationXmlResponse(Result<std::string, Error> response) {
        build_activation_response_ = std::move(response);
    }

    void SetParseDiscoveryResponse(Result<DiscoveryResult, Error> response) {
        parse_discovery_response_ = std::move(response);
    }

    void SetParsePackageResponse(Result<PackageInfo, Error> response) {
        parse_package_response_ = std::move(response);
    }

    void SetParseRepoListResponse(Result<std::vector<RepoInfo>, Error> response) {
        parse_repo_list_response_ = std::move(response);
    }

    void SetParseRepoStatusResponse(Result<RepoStatus, Error> response) {
        parse_repo_status_response_ = std::move(response);
    }

    void SetParseActivationResponse(Result<ActivationResult, Error> response) {
        parse_activation_response_ = std::move(response);
    }

    void SetParseInactiveObjectsResponse(Result<std::vector<InactiveObject>, Error> response) {
        parse_inactive_objects_response_ = std::move(response);
    }

    void SetParsePollResponse(Result<PollStatusInfo, Error> response) {
        parse_poll_response_ = std::move(response);
    }

    // -- IXmlCodec implementation --------------------------------------------

    Result<std::string, Error> BuildPackageCreateXml(
        const PackageName& package_name,
        std::string_view description,
        std::string_view software_component) const override {
        calls_.push_back({"BuildPackageCreateXml",
                          {package_name.Value(),
                           std::string(description),
                           std::string(software_component)}});
        return build_package_create_response_;
    }

    Result<std::string, Error> BuildRepoCloneXml(
        const RepoUrl& repo_url,
        const BranchRef& branch,
        const PackageName& package_name) const override {
        calls_.push_back({"BuildRepoCloneXml",
                          {repo_url.Value(),
                           branch.Value(),
                           package_name.Value()}});
        return build_repo_clone_response_;
    }

    Result<std::string, Error> BuildActivationXml(
        const std::vector<InactiveObject>& objects) const override {
        std::vector<std::string> args;
        args.reserve(objects.size());
        for (const auto& obj : objects) {
            args.push_back(obj.type + ":" + obj.name);
        }
        calls_.push_back({"BuildActivationXml", std::move(args)});
        return build_activation_response_;
    }

    Result<DiscoveryResult, Error> ParseDiscoveryResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParseDiscoveryResponse", {std::string(xml)}});
        return parse_discovery_response_;
    }

    Result<PackageInfo, Error> ParsePackageResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParsePackageResponse", {std::string(xml)}});
        return parse_package_response_;
    }

    Result<std::vector<RepoInfo>, Error> ParseRepoListResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParseRepoListResponse", {std::string(xml)}});
        return parse_repo_list_response_;
    }

    Result<RepoStatus, Error> ParseRepoStatusResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParseRepoStatusResponse", {std::string(xml)}});
        return parse_repo_status_response_;
    }

    Result<ActivationResult, Error> ParseActivationResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParseActivationResponse", {std::string(xml)}});
        return parse_activation_response_;
    }

    Result<std::vector<InactiveObject>, Error> ParseInactiveObjectsResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParseInactiveObjectsResponse", {std::string(xml)}});
        return parse_inactive_objects_response_;
    }

    Result<PollStatusInfo, Error> ParsePollResponse(
        std::string_view xml) const override {
        calls_.push_back({"ParsePollResponse", {std::string(xml)}});
        return parse_poll_response_;
    }

private:
    // Mutable so const methods can track calls (test infrastructure concern).
    mutable std::vector<CallRecord> calls_;

    // Canned responses — default to error so unconfigured calls fail loudly.
    static Error DefaultError(std::string_view method) {
        return Error{"MockXmlCodec", "",
                     std::nullopt,
                     std::string(method) + ": no canned response configured",
                     std::nullopt};
    }

    Result<std::string, Error> build_package_create_response_ =
        Result<std::string, Error>::Err(DefaultError("BuildPackageCreateXml"));

    Result<std::string, Error> build_repo_clone_response_ =
        Result<std::string, Error>::Err(DefaultError("BuildRepoCloneXml"));

    Result<std::string, Error> build_activation_response_ =
        Result<std::string, Error>::Err(DefaultError("BuildActivationXml"));

    Result<DiscoveryResult, Error> parse_discovery_response_ =
        Result<DiscoveryResult, Error>::Err(DefaultError("ParseDiscoveryResponse"));

    Result<PackageInfo, Error> parse_package_response_ =
        Result<PackageInfo, Error>::Err(DefaultError("ParsePackageResponse"));

    Result<std::vector<RepoInfo>, Error> parse_repo_list_response_ =
        Result<std::vector<RepoInfo>, Error>::Err(DefaultError("ParseRepoListResponse"));

    Result<RepoStatus, Error> parse_repo_status_response_ =
        Result<RepoStatus, Error>::Err(DefaultError("ParseRepoStatusResponse"));

    Result<ActivationResult, Error> parse_activation_response_ =
        Result<ActivationResult, Error>::Err(DefaultError("ParseActivationResponse"));

    Result<std::vector<InactiveObject>, Error> parse_inactive_objects_response_ =
        Result<std::vector<InactiveObject>, Error>::Err(DefaultError("ParseInactiveObjectsResponse"));

    Result<PollStatusInfo, Error> parse_poll_response_ =
        Result<PollStatusInfo, Error>::Err(DefaultError("ParsePollResponse"));
};

} // namespace erpl_adt::testing
