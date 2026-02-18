#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

struct BwLineageNode {
    std::string id;
    std::string type;
    std::string name;
    std::string role;
    std::string uri;
    std::string version;
    std::map<std::string, std::string> attributes;
};

struct BwLineageEdge {
    std::string id;
    std::string from;
    std::string to;
    std::string type;
    std::map<std::string, std::string> attributes;
};

struct BwLineageProvenance {
    std::string operation;
    std::string endpoint;
    std::string status;  // ok, skipped, partial
};

struct BwLineageGraph {
    std::string schema_version = "1.0";
    std::string root_type = "DTPA";
    std::string root_name;
    std::vector<BwLineageNode> nodes;
    std::vector<BwLineageEdge> edges;
    std::vector<BwLineageProvenance> provenance;
    std::vector<std::string> warnings;
};

struct BwLineageGraphOptions {
    std::string dtp_name;
    std::string version = "a";
    std::optional<std::string> trfn_name;
    bool include_xref = true;
    int max_xref = 100;
};

[[nodiscard]] Result<BwLineageGraph, Error> BwBuildLineageGraph(
    IAdtSession& session,
    const BwLineageGraphOptions& options);

}  // namespace erpl_adt

