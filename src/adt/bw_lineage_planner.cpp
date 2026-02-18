#include <erpl_adt/adt/bw_lineage_planner.hpp>

#include <erpl_adt/adt/bw_lineage.hpp>
#include <erpl_adt/adt/bw_search.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace erpl_adt {

namespace {

constexpr int kDefaultValidationCap = 50;

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string NormalizeDtpReadVersion(const std::string& object_version) {
    if (object_version.size() == 1) {
        const auto v = ToLower(object_version);
        if (v == "a" || v == "m" || v == "d") {
            return v;
        }
    }
    return "a";
}

void SortAndFinalize(BwUpstreamLineagePlan& plan) {
    std::sort(plan.candidates.begin(), plan.candidates.end(),
              [](const BwUpstreamCandidate& a, const BwUpstreamCandidate& b) {
                  if (a.object_name != b.object_name) return a.object_name < b.object_name;
                  if (a.object_version != b.object_version) return a.object_version < b.object_version;
                  return a.object_status < b.object_status;
              });

    if (plan.candidates.empty()) {
        plan.warnings.push_back("No upstream DTP candidates discovered");
        return;
    }
    if (plan.candidates.size() == 1) {
        plan.selected_dtp = plan.candidates.front().object_name;
        return;
    }
    plan.ambiguous = true;
    plan.warnings.push_back(
        "Ambiguous upstream lineage: multiple DTP candidates discovered");
}

Result<void, Error> CollectCandidates(IAdtSession& session, BwUpstreamLineagePlan& plan,
                                      std::set<std::string>& seen,
                                      const BwSearchOptions& base_search,
                                      const std::string& evidence,
                                      const BwUpstreamLineagePlannerOptions& options) {
    int max_results = std::max(1, options.initial_max_results);
    const int cap = std::max(max_results, options.max_results_cap);
    const int max_steps = std::max(1, options.max_steps);
    bool feed_incomplete = false;

    for (int step = 0; step < max_steps; ++step) {
        BwSearchOptions search = base_search;
        search.max_results = max_results;
        auto result = BwSearchObjectsDetailed(session, search);
        if (result.IsErr()) {
            return Result<void, Error>::Err(result.Error());
        }

        ++plan.steps;
        const auto& response = result.Value();
        feed_incomplete = response.feed_incomplete;
        for (const auto& item : response.results) {
            if (item.name.empty()) continue;
            if (seen.count(item.name) > 0) continue;
            seen.insert(item.name);
            plan.candidates.push_back(BwUpstreamCandidate{
                item.name, item.type, item.version, item.status, item.uri, evidence});
        }

        if (!feed_incomplete) {
            return Result<void, Error>::Ok();
        }

        plan.complete = false;
        if (max_results >= cap) {
            plan.warnings.push_back(
                "Search feed remains incomplete at maxSize=" + std::to_string(max_results));
            return Result<void, Error>::Ok();
        }
        max_results = std::min(cap, max_results * 2);
    }

    if (feed_incomplete) {
        plan.complete = false;
        plan.warnings.push_back(
            "Search feed remained incomplete after max planner steps");
    }
    return Result<void, Error>::Ok();
}

void ValidateCandidateTargets(IAdtSession& session, BwUpstreamLineagePlan& plan,
                              const BwQueryComponentDetail& query_detail) {
    if (plan.candidates.empty() || query_detail.info_provider.empty()) {
        return;
    }

    std::vector<BwUpstreamCandidate> valid;
    int validated = 0;
    for (const auto& candidate : plan.candidates) {
        if (validated >= kDefaultValidationCap) {
            plan.complete = false;
            plan.warnings.push_back(
                "Candidate validation truncated at " +
                std::to_string(kDefaultValidationCap) + " entries");
            break;
        }
        ++validated;

        auto dtp_result = BwReadDtpDetail(session, candidate.object_name,
                                          NormalizeDtpReadVersion(candidate.object_version));
        if (dtp_result.IsErr()) {
            plan.warnings.push_back(
                "Could not validate DTP candidate " + candidate.object_name + ": " +
                dtp_result.Error().message);
            continue;
        }

        const auto& dtp = dtp_result.Value();
        if (dtp.target_name != query_detail.info_provider) {
            plan.warnings.push_back(
                "Discarded DTP candidate " + candidate.object_name +
                " due to target mismatch (" + dtp.target_name + ")");
            continue;
        }
        if (!query_detail.info_provider_type.empty() && !dtp.target_type.empty() &&
            dtp.target_type != query_detail.info_provider_type) {
            plan.warnings.push_back(
                "Discarded DTP candidate " + candidate.object_name +
                " due to target type mismatch (" + dtp.target_type + ")");
            continue;
        }
        valid.push_back(candidate);
    }

    if (!valid.empty()) {
        plan.candidates = std::move(valid);
    } else if (!plan.candidates.empty()) {
        plan.candidates.clear();
        plan.warnings.push_back("No structurally valid DTP candidates after validation");
    }
}

}  // namespace

Result<BwUpstreamLineagePlan, Error> BwPlanQueryUpstreamLineage(
    IAdtSession& session,
    const BwQueryComponentDetail& query_detail,
    const BwUpstreamLineagePlannerOptions& options) {
    BwUpstreamLineagePlan plan;
    plan.query_name = query_detail.name;
    plan.info_provider = query_detail.info_provider;
    plan.info_provider_type = query_detail.info_provider_type;

    if (query_detail.info_provider.empty()) {
        plan.warnings.push_back(
            "Query has no info_provider; upstream DTP discovery skipped");
        return Result<BwUpstreamLineagePlan, Error>::Ok(std::move(plan));
    }

    std::set<std::string> seen;
    auto run_search = [&](const std::optional<std::string>& depends_type,
                          const std::string& evidence) -> Result<void, Error> {
        BwSearchOptions search;
        search.query = "*";
        search.object_type = "DTPA";
        // Do not pre-filter by version/status: BW systems often expose
        // landscape-specific values (for example "active"), which would
        // otherwise hide valid candidates before structural validation.
        search.depends_on_name = query_detail.info_provider;
        search.depends_on_type = depends_type;
        return CollectCandidates(session, plan, seen, search, evidence, options);
    };

    if (!query_detail.info_provider_type.empty()) {
        auto with_type = run_search(query_detail.info_provider_type, "bwSearch.depends_on_typed");
        if (with_type.IsErr()) {
            return Result<BwUpstreamLineagePlan, Error>::Err(with_type.Error());
        }
        if (plan.candidates.empty()) {
            auto without_type = run_search(std::nullopt, "bwSearch.depends_on_name");
            if (without_type.IsErr()) {
                return Result<BwUpstreamLineagePlan, Error>::Err(without_type.Error());
            }
        }
    } else {
        auto without_type = run_search(std::nullopt, "bwSearch.depends_on_name");
        if (without_type.IsErr()) {
            return Result<BwUpstreamLineagePlan, Error>::Err(without_type.Error());
        }
    }

    ValidateCandidateTargets(session, plan, query_detail);
    SortAndFinalize(plan);
    return Result<BwUpstreamLineagePlan, Error>::Ok(std::move(plan));
}

}  // namespace erpl_adt
