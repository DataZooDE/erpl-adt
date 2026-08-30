#include <erpl_adt/mcp/tool_metadata.hpp>

namespace erpl_adt {

namespace {

// Shorthands for the three shapes every tool falls into.
//
//   Reads   — touches nothing. Safe to call speculatively, safe to repeat.
//   Writes  — changes the system but does not destroy anything a user would
//             miss; calling twice is either harmless or converges.
//   Destroys— deletes, overwrites or releases something that is hard to get
//             back. These are the ones a host should confirm.
constexpr ToolAnnotations Reads() { return {true, false, true}; }
constexpr ToolAnnotations Writes(bool idempotent = false) {
    return {false, false, idempotent};
}
constexpr ToolAnnotations Destroys() { return {false, true, false}; }

}  // namespace

const std::vector<ToolMetadata>& ToolMetadataTable() {
    // clang-format off
    static const std::vector<ToolMetadata> table = {
        // -- ADT: read ------------------------------------------------------
        {"adt_search",             "Search ABAP repository",        Reads()},
        {"adt_read_object",        "Read object metadata",          Reads()},
        {"adt_read_source",        "Read source code",              Reads()},
        {"adt_check_syntax",       "Check syntax",                  Reads()},
        {"adt_list_transports",    "List transports",               Reads()},
        {"adt_read_table",         "Read table definition",         Reads()},
        {"adt_read_cds",           "Read CDS view",                 Reads()},
        {"adt_list_package",       "List package contents",         Reads()},
        {"adt_package_tree",       "Walk package tree",             Reads()},
        {"adt_package_exists",     "Check package exists",          Reads()},
        {"adt_discover",           "Discover ADT services",         Reads()},

        // -- ADT: runs code, changes no source ------------------------------
        // Tests and checks execute ABAP on the system; a test class can have
        // side effects of its own, so these are not read-only.
        {"adt_run_tests",          "Run ABAP Unit tests",           Writes(true)},
        {"adt_run_atc",            "Run ATC checks",                Writes(true)},
        {"adt_run_class",          "Run a class",                   Writes()},

        // -- ADT: mutating --------------------------------------------------
        {"adt_lock",               "Lock object for editing",       Writes()},
        {"adt_unlock",             "Release object lock",           Writes(true)},
        {"adt_create_object",      "Create object",                 Writes()},
        {"adt_create_transport",   "Create transport request",      Writes()},
        {"adt_activate",           "Activate object",               Writes(true)},

        // -- ADT: destructive ------------------------------------------------
        // write_source overwrites source that may not exist anywhere else;
        // release_transport cannot be undone.
        {"adt_write_source",       "Overwrite source code",         Destroys()},
        {"adt_delete_object",      "Delete object",                 Destroys()},
        {"adt_release_transport",  "Release transport (final)",     Destroys()},

        // -- BW: read -------------------------------------------------------
        {"bw_discover",            "Discover BW services",          Reads()},
        {"bw_search",              "Search BW objects",             Reads()},
        {"bw_read_object",         "Read BW object",                Reads()},
        {"bw_transport_check",     "Check BW transport",            Reads()},
        {"bw_job_status",          "Read BW job status",            Reads()},
        {"bw_list_locks",          "List BW locks",                 Reads()},
        {"bw_dbinfo",              "Read BW database info",         Reads()},
        {"bw_sysinfo",             "Read BW system info",           Reads()},
        {"bw_changeability",       "Read changeability settings",   Reads()},
        {"bw_adturi",              "Resolve BW object to ADT URI",  Reads()},
        {"bw_xref",                "Cross-reference BW object",     Reads()},
        {"bw_nodes",               "List BW child nodes",           Reads()},
        {"bw_search_metadata",     "Search BW metadata",            Reads()},
        {"bw_list_favorites",      "List BW favorites",             Reads()},
        {"bw_nodepath",            "Read BW node path",             Reads()},
        {"bw_application_log",     "Read BW application log",       Reads()},
        {"bw_message_text",        "Read BW message text",          Reads()},
        {"bw_validate",            "Validate BW object",            Reads()},
        {"bw_valuehelp",           "Read BW value help",            Reads()},
        {"bw_virtualfolders",      "Browse BW virtual folders",     Reads()},
        {"bw_datavolumes",         "Read BW data volumes",          Reads()},
        {"bw_reporting",           "Read BW reporting metadata",    Reads()},
        {"bw_query_properties",    "Read BW query properties",      Reads()},
        {"bw_transport_collect",   "Collect BW transport objects",  Reads()},
        {"bw_read_transformation", "Read BW transformation",        Reads()},
        {"bw_read_adso",           "Read aDSO definition",          Reads()},
        {"bw_read_dtp",            "Read data transfer process",    Reads()},
        {"bw_read_rsds",           "Read BW DataSource",            Reads()},
        {"bw_read_query_component","Read BW query component",       Reads()},
        {"bw_read_dataflow",       "Read BW dataflow",              Reads()},
        {"bw_lineage_graph",       "Build BW lineage graph",        Reads()},

        // -- BW: mutating ---------------------------------------------------
        {"bw_create_object",       "Create BW object",              Writes()},
        {"bw_lock_object",         "Lock BW object",                Writes()},
        {"bw_unlock_object",       "Release BW object lock",        Writes(true)},
        {"bw_activate",            "Activate BW object",            Writes(true)},
        {"bw_job_restart",         "Restart BW job",                Writes()},
        // Assigns an object to a transport request; re-running with the same
        // request is a no-op rather than a second assignment.
        {"bw_transport_write",     "Add BW object to transport",    Writes(true)},
        {"bw_job_cleanup",         "Clean up BW job",               Writes(true)},

        // -- BW: destructive -------------------------------------------------
        // save_object overwrites a definition; delete_lock and clear_favorites
        // discard state belonging to someone else; move_requests rewrites
        // transport assignment.
        {"bw_save_object",         "Overwrite BW object",           Destroys()},
        {"bw_delete_object",       "Delete BW object",              Destroys()},
        {"bw_delete_lock",         "Break another user's BW lock",  Destroys()},
        {"bw_clear_favorites",     "Clear BW favorites",            Destroys()},
        {"bw_move_requests",       "Move transport requests",       Destroys()},

        // -- Catalog --------------------------------------------------------
        // catalog_build and catalog_export read SAP and write local files;
        // they touch no SAP state.
        {"catalog_build",          "Build catalog cache",           Writes()},
        {"catalog_export",         "Export catalog feed",           Writes(true)},
        {"catalog_search",         "Search catalog",                Reads()},
        {"catalog_get",            "Read catalog entity",           Reads()},
        {"catalog_where_used",     "Find catalog usages",           Reads()},
        {"catalog_lineage",        "Read catalog lineage",          Reads()},
        {"catalog_driver_tree",    "Read catalog driver tree",      Reads()},
        {"catalog_sync_status",    "Read catalog sync status",      Reads()},
        {"catalog_stats",          "Read catalog statistics",       Reads()},
        {"catalog_object_types",   "List catalog object types",     Reads()},
        {"catalog_object_subtypes","List catalog object subtypes",  Reads()},
        // Writes curation overlay columns in the local DuckDB cache only —
        // never SAP — and re-annotating replaces the previous value.
        {"catalog_annotate",       "Annotate catalog entity",       Writes(true)},
    };
    // clang-format on
    return table;
}

size_t ApplyToolMetadata(ToolRegistry& registry) {
    size_t annotated = 0;
    for (const auto& entry : ToolMetadataTable()) {
        if (registry.Annotate(entry.name, entry.annotations, entry.title)) {
            ++annotated;
        }
    }
    return annotated;
}

}  // namespace erpl_adt
