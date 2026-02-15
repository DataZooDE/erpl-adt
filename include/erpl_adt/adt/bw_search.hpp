#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwSearchResult — a single result from a BW object search.
// ---------------------------------------------------------------------------
struct BwSearchResult {
    std::string name;
    std::string type;           // e.g. "ADSO", "IOBJ"
    std::string subtype;
    std::string description;
    std::string version;        // "A" (active), "M" (modified), "N" (new)
    std::string status;         // "ACT", "INA", "OFF"
    std::string technical_name;
    std::string uri;            // e.g. "/sap/bw/modeling/adso/ZADSO001/a"
    std::string last_changed;
};

// ---------------------------------------------------------------------------
// BwSearchObjects — search the BW repository for modeling objects.
//
// Endpoint: GET /sap/bw/modeling/is/bwsearch?searchTerm=...
// Accept:   application/atom+xml
//
// XML response is parsed directly with tinyxml2.
// ---------------------------------------------------------------------------

struct BwSearchOptions {
    std::string query;                               // Required: search term
    int max_results = 100;                           // Maximum results
    std::optional<std::string> object_type;          // e.g. "ADSO", "IOBJ"
    std::optional<std::string> object_sub_type;      // e.g. "REP", "SOB", "RKF"
    std::optional<std::string> object_status;        // "ACT", "INA", "OFF"
    std::optional<std::string> object_version;       // "A", "M", "N"
    std::optional<std::string> changed_by;           // Last changed by user
    std::optional<std::string> changed_on_from;      // Changed on or after date
    std::optional<std::string> changed_on_to;        // Changed on or before date
    std::optional<std::string> created_by;           // Created by user
    std::optional<std::string> created_on_from;      // Created on or after date
    std::optional<std::string> created_on_to;        // Created on or before date
    std::optional<std::string> depends_on_name;      // Objects depending on name
    std::optional<std::string> depends_on_type;      // Objects depending on type
    bool search_in_description = false;              // Also search descriptions
    bool search_in_name = true;                      // Search in names (default)
};

[[nodiscard]] Result<std::vector<BwSearchResult>, Error> BwSearchObjects(
    IAdtSession& session,
    const BwSearchOptions& options);

} // namespace erpl_adt
