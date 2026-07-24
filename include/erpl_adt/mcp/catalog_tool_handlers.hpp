#pragma once

#include <erpl_adt/mcp/tool_registry.hpp>
#include <erpl_adt/storage/i_catalog_store.hpp>

#include <memory>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// RegisterCatalogStoreTools — the fast, read-only `catalog_*` MCP tool
// group (BRD.md FR-MCP-1): every handler reads the already-open `store`
// directly (opened once at MCP server startup, NFR-1) — zero ADT/SAP calls
// in the hot path, unlike catalog_build/catalog_export (mcp_tool_handlers.cpp)
// which do call ADT. Registered separately from RegisterAdtTools because
// this group has no IAdtSession dependency at all.
// ---------------------------------------------------------------------------
void RegisterCatalogStoreTools(ToolRegistry& registry, std::shared_ptr<ICatalogStore> store);

} // namespace erpl_adt
