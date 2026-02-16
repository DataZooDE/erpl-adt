#pragma once

#include <erpl_adt/adt/bw_discovery.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
#include <string>
#include <string_view>

namespace erpl_adt {

using BwTemplateParams = std::map<std::string, std::string>;

// Expand a BW/ADT URI template (RFC6570 subset: {var}, {?a,b,c}).
[[nodiscard]] std::string BwExpandUriTemplate(
    std::string_view uri_template,
    const BwTemplateParams& path_params,
    const BwTemplateParams& query_params);

// Resolve endpoint from already-fetched discovery and expand it.
[[nodiscard]] Result<std::string, Error> BwResolveAndExpandEndpoint(
    const BwDiscoveryResult& discovery,
    const std::string& scheme,
    const std::string& term,
    const BwTemplateParams& path_params,
    const BwTemplateParams& query_params);

// Fetch discovery, resolve endpoint and expand it.
[[nodiscard]] Result<std::string, Error> BwDiscoverResolveAndExpandEndpoint(
    IAdtSession& session,
    const std::string& scheme,
    const std::string& term,
    const BwTemplateParams& path_params,
    const BwTemplateParams& query_params);

} // namespace erpl_adt
