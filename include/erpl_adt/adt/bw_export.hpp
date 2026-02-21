#pragma once

#include <erpl_adt/adt/bw_lineage_graph.hpp>
#include <erpl_adt/adt/bw_query.hpp>
#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// BwQueryIobjRef — one InfoObject reference from a query (dimension, filter,
// variable, or key figure).
// ---------------------------------------------------------------------------
struct BwQueryIobjRef {
    std::string name;
    std::string role;  // "dimension" | "filter" | "variable" | "key_figure"
};

// ---------------------------------------------------------------------------
// BwExportedField — a single field from an exported object.
// ---------------------------------------------------------------------------
struct BwExportedField {
    std::string name;
    std::string description;
    std::string data_type;
    std::string info_object;
    std::string segment_id;
    int length = 0;
    int decimals = 0;
    bool key = false;
};

// ---------------------------------------------------------------------------
// BwExportedObject — one BW object (ADSO, RSDS, TRFN, DTPA, QUERY, …) with
// all collected metadata and optional lineage / query graphs.
// ---------------------------------------------------------------------------
struct BwExportedObject {
    std::string name;
    std::string type;
    std::string subtype;
    std::string status;
    std::string description;
    std::string package_name;
    std::string uri;

    std::vector<BwExportedField> fields;

    // DTP-specific
    std::string dtp_source_name;
    std::string dtp_source_type;
    std::string dtp_target_name;
    std::string dtp_target_type;
    std::optional<BwLineageGraph> lineage;

    // TRFN-specific
    std::string trfn_source_name;
    std::string trfn_source_type;
    std::string trfn_target_name;
    std::string trfn_target_type;

    // QUERY-specific
    std::string query_info_provider;
    std::optional<BwQueryGraph> query_graph;
    std::vector<BwQueryIobjRef> iobj_refs;  // dimensions, filters, variables used by this query
};

// ---------------------------------------------------------------------------
// BwExportOptions — options for a full infoarea export.
// ---------------------------------------------------------------------------
struct BwExportOptions {
    std::string infoarea_name;
    std::string version = "a";
    int max_depth = 10;
    bool include_lineage = true;
    bool include_queries = true;
    bool include_search_supplement = true;  // Supplement BFS with BwSearch(infoArea=...) to find IOBJ/ELEM
    bool include_xref_edges = true;         // Derive INFOPROVIDER→QUERY edges via xref API
    bool include_elem_provider_edges = true;  // Parse orphan ELEM XMLs to recover missing provider edges
    bool include_iobj_refs = false;           // Also harvest iobj_refs from ELEM XMLs (--iobj-edges)
    std::vector<std::string> types_filter;  // empty = all
};

// ---------------------------------------------------------------------------
// BwInfoareaExport — result of exporting an entire infoarea.
// ---------------------------------------------------------------------------
struct BwInfoareaExport {
    std::string schema_version = "1.0";
    std::string contract = "bw.infoarea.export";
    std::string infoarea;
    std::string exported_at;
    std::vector<BwExportedObject> objects;
    std::vector<BwLineageNode> dataflow_nodes;   // merged / deduped from all DTP lineages
    std::vector<BwLineageEdge> dataflow_edges;
    std::vector<std::string> warnings;
    std::vector<BwLineageProvenance> provenance;
};

// ---------------------------------------------------------------------------
// BwExportInfoarea — enumerate and export all objects in an infoarea.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwInfoareaExport, Error> BwExportInfoarea(
    IAdtSession& session,
    const BwExportOptions& options);

// ---------------------------------------------------------------------------
// BwExportQuery — export a single BW query (ELEM) and its connected graph.
// options.infoarea_name is ignored; name is the query technical name.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwInfoareaExport, Error> BwExportQuery(
    IAdtSession& session,
    const std::string& name,
    const BwExportOptions& options);

// ---------------------------------------------------------------------------
// BwExportCube — export a single BW infoprovider (ADSO/CUBE/MPRO) and its
// connected graph. options.infoarea_name is ignored; name is the object name.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<BwInfoareaExport, Error> BwExportCube(
    IAdtSession& session,
    const std::string& name,
    const BwExportOptions& options);

// ---------------------------------------------------------------------------
// Serializers
// ---------------------------------------------------------------------------
[[nodiscard]] std::string BwRenderExportCatalogJson(const BwInfoareaExport&);

[[nodiscard]] std::string BwRenderExportOpenMetadataJson(
    const BwInfoareaExport&,
    const std::string& service_name = "erpl_adt",
    const std::string& system_id = "");

struct BwMermaidOptions {
    bool iobj_edges = false;  // Show InfoObject nodes (dimensions, filters, variables)
};

[[nodiscard]] std::string BwRenderExportMermaid(const BwInfoareaExport&,
                                                const BwMermaidOptions& = {});

} // namespace erpl_adt
