#pragma once

#include <erpl_adt/adt/bw_query.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// Protocol contract: docs/bw-lineage-contract-v3.md
struct BwUpstreamCandidate {
    std::string object_name;
    std::string object_type;
    std::string object_version;
    std::string object_status;
    std::string uri;
    std::string evidence;
};

struct BwUpstreamLineagePlan {
    std::string mode = "auto";
    std::string query_name;
    std::string info_provider;
    std::string info_provider_type;
    std::vector<BwUpstreamCandidate> candidates;
    std::optional<std::string> selected_dtp;
    bool ambiguous = false;
    bool complete = true;
    int steps = 0;
    std::vector<std::string> warnings;
};

struct BwUpstreamLineagePlannerOptions {
    int max_steps = 4;
    int initial_max_results = 200;
    int max_results_cap = 1600;
};

[[nodiscard]] Result<BwUpstreamLineagePlan, Error> BwPlanQueryUpstreamLineage(
    IAdtSession& session,
    const BwQueryComponentDetail& query_detail,
    const BwUpstreamLineagePlannerOptions& options = {});

}  // namespace erpl_adt
