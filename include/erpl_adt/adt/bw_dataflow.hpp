#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
#include <string>
#include <vector>

namespace erpl_adt {

struct BwDataFlowNode {
    std::string id;
    std::string name;
    std::string type;
    std::map<std::string, std::string> attributes;
};

struct BwDataFlowConnection {
    std::string from;
    std::string to;
    std::string type;
    std::map<std::string, std::string> attributes;
};

struct BwDataFlowDetail {
    std::string name;
    std::string description;
    std::map<std::string, std::string> attributes;
    std::vector<BwDataFlowNode> nodes;
    std::vector<BwDataFlowConnection> connections;
};

[[nodiscard]] Result<BwDataFlowDetail, Error> BwReadDataFlow(
    IAdtSession& session,
    const std::string& name,
    const std::string& version = "a",
    const std::string& content_type = "");

}  // namespace erpl_adt
