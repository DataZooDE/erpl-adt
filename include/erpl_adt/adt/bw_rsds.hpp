#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

struct BwRsdsField {
    std::string segment_id;
    std::string name;
    std::string description;
    std::string data_type;
    int length = 0;
    int decimals = 0;
    bool key = false;
};

struct BwRsdsDetail {
    std::string name;
    std::string description;
    std::string source_system;
    std::string package_name;
    std::vector<BwRsdsField> fields;
};

[[nodiscard]] Result<BwRsdsDetail, Error> BwReadRsdsDetail(
    IAdtSession& session,
    const std::string& name,
    const std::string& source_system,
    const std::string& version = "a",
    const std::string& content_type = "");

}  // namespace erpl_adt

