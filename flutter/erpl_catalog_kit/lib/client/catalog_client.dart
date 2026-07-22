import 'catalog_models.dart';

/// Abstract catalog backend — mirrors escurel's InstanceBackend trait
/// abstraction. Two implementations are expected: [McpHttpCatalogClient]
/// (hosted/mobile mode, MCP-over-HTTP) and a future DuckDbCatalogClient
/// (desktop mode, direct dart_duckdb attach, no MCP round-trip).
abstract class CatalogClient {
  /// Empty [query] browses all entities (technical-name order) instead of
  /// ranking a match. [cursor] resumes from a prior page's `nextCursor`.
  Future<CatalogSearchPage> search(
    String query, {
    String mode = 'fts',
    int maxResults = 20,
    int cursor = 0,
    String? domain,
    String? objectType,
    String? objectSubtype,
    bool curatedOnly = false,
  });
  Future<CatalogEntity?> getEntity(String id);
  Future<List<CatalogEdgeRef>> whereUsed(String id, {int maxResults = 50});
  Future<List<CatalogEdgeRef>> lineage(String id, {int maxDepth = 5});
  Future<List<DriverTreeField>> driverTree(String id);
  Future<List<CatalogSyncRun>> syncStatus({int maxResults = 10});
  Future<CatalogStats> stats();

  /// Distinct (domain, object_type) pairs actually present in the catalog,
  /// with counts — used to build the object-type filter from what's really
  /// there instead of guessing.
  Future<List<CatalogObjectTypeCount>> objectTypes();

  /// Distinct (domain, object_type, object_subtype) triples actually
  /// present — only meaningful today for BW's ELEM object_type, where a
  /// subtype (REP/VAR/CKF/RKF/FILT/STR) distinguishes a real query from
  /// everything else ELEM also covers.
  Future<List<CatalogObjectSubtypeCount>> objectSubtypes();
  Future<void> annotate(
    String id, {
    String? definition,
    String? owner,
    String? lob,
    String? confidentiality,
  });
}

/// Thrown when a catalog_* MCP tool call returns a JSON-RPC error.
class CatalogClientException implements Exception {
  final String message;
  const CatalogClientException(this.message);

  @override
  String toString() => 'CatalogClientException: $message';
}
