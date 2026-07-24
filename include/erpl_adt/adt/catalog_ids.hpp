#pragma once

#include <erpl_adt/core/catalog_types.hpp>

#include <string_view>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// DeriveEntityId — stable catalog entity ID.
//
// id = sha256_hex(system_sid | domain | object_type | technical_name), with
// '|' as a literal field separator. Same inputs always produce the same ID
// (DM-2 of the catalog data-model contract) — this is what makes incremental
// sync, favorites, and deep links work across runs.
// ---------------------------------------------------------------------------
[[nodiscard]] EntityId DeriveEntityId(std::string_view system_sid,
                                       CatalogDomain domain,
                                       std::string_view object_type,
                                       std::string_view technical_name);

} // namespace erpl_adt
