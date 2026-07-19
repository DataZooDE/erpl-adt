#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// DomainFixValue — one entry of a domain's fixed-value list.
// ---------------------------------------------------------------------------
struct DomainFixValue {
    std::string low;
    std::string high;   // set for a range entry, empty for a single value
    std::string text;
};

// ---------------------------------------------------------------------------
// DomainInfo — metadata about a DDIC domain.
// ---------------------------------------------------------------------------
struct DomainInfo {
    std::string name;
    std::string description;
    std::string data_type;          // e.g. "CHAR", "LANG", "NUMC"
    std::optional<int> length;
    std::optional<int> decimals;
    std::optional<int> output_length;
    std::string conversion_exit;    // e.g. "ISOLA" (empty if none)
    std::string value_table;        // check-table reference (empty if none)
    std::vector<DomainFixValue> fix_values;
};

// ---------------------------------------------------------------------------
// GetDomain — fetch DDIC domain definition (type, length, fixed values).
//
// Endpoint: GET /sap/bc/adt/ddic/domains/{domainName}
// ---------------------------------------------------------------------------
[[nodiscard]] Result<DomainInfo, Error> GetDomain(
    IAdtSession& session,
    const std::string& domain_name);

} // namespace erpl_adt
