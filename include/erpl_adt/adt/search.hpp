#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// SearchResult — a single result from an ADT quick search.
// ---------------------------------------------------------------------------
struct SearchResult {
    std::string name;
    std::string type;          // e.g. "CLAS/OC", "PROG/P"
    std::string uri;           // e.g. "/sap/bc/adt/oo/classes/zcl_example"
    std::string description;
    std::string package_name;
};

// ---------------------------------------------------------------------------
// SearchObjects — search the ABAP repository via ADT quick search.
//
// Endpoint: GET /sap/bc/adt/repository/informationsystem/search
//           ?operation=quickSearch&query=...
//
// XML response is parsed directly with tinyxml2 (no IXmlCodec dependency).
// ---------------------------------------------------------------------------

struct SearchOptions {
    std::string query;                       // required: search term
    int max_results = 100;                   // default 100
    std::optional<std::string> object_type;  // e.g. "CLAS", "PROG"
};

[[nodiscard]] Result<std::vector<SearchResult>, Error> SearchObjects(
    IAdtSession& session,
    const SearchOptions& options);

} // namespace erpl_adt
