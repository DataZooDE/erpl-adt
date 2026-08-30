#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

struct BwReportingOptions {
    std::string compid;
    bool dbgmode = false;
    bool metadata_only = false;
    bool incl_metadata = false;
    bool incl_object_values = false;
    bool incl_except_def = false;
    bool compact_mode = false;
    std::optional<int> from_row;
    std::optional<int> to_row;
};

struct BwReportingRecord {
    std::map<std::string, std::string> fields;
};

[[nodiscard]] Result<std::vector<BwReportingRecord>, Error>
BwGetReportingMetadata(IAdtSession& session, const BwReportingOptions& options);

[[nodiscard]] Result<std::vector<BwReportingRecord>, Error>
// Query properties of one InfoProvider. `infoprovider` is required: the route
// answers "Operation could not be carried out for" without it, and the media
// type it serves is infoprov_query_props, not the rulesQueryProperties the
// discovery document advertises.
BwGetQueryProperties(IAdtSession& session, const std::string& infoprovider,
                     const std::string& object_type = "",
                     const std::string& version = "");

}  // namespace erpl_adt
