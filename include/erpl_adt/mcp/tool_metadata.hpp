#pragma once

#include <erpl_adt/mcp/tool_registry.hpp>

#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Tool metadata — the human title and behavioural hints a host needs to
// present a tool well, kept as one reviewable table rather than spread across
// 77 registration call sites.
//
// The classification is a judgement about what a tool does to a live SAP
// system, so it is written down and reviewed here, not inferred from a name
// prefix. `ApplyToolMetadata` is called at the end of tool registration, and a
// test asserts every registered tool has an entry — a new tool cannot quietly
// ship unclassified.
// ---------------------------------------------------------------------------
struct ToolMetadata {
    std::string name;
    std::string title;
    ToolAnnotations annotations;
};

// The table itself, exposed so tests can assert it covers the registry.
[[nodiscard]] const std::vector<ToolMetadata>& ToolMetadataTable();

// Stamp titles and annotations onto every tool the registry knows about that
// has an entry. Returns the number of tools annotated.
size_t ApplyToolMetadata(ToolRegistry& registry);

// Declare outputSchema for the tools whose results a model has to branch on
// (test runs, checks, table definitions, catalog search and lineage) so a
// client can validate structuredContent instead of guessing at a blob.
// Returns the number of schemas applied.
size_t ApplyToolOutputSchemas(ToolRegistry& registry);

}  // namespace erpl_adt
