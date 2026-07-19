import 'catalog_models.dart';

/// Abstract catalog backend — mirrors escurel's InstanceBackend trait
/// abstraction. Two implementations are expected: [McpHttpCatalogClient]
/// (hosted/mobile mode, MCP-over-HTTP) and a future DuckDbCatalogClient
/// (desktop mode, direct dart_duckdb attach, no MCP round-trip).
abstract class CatalogClient {
  Future<List<CatalogSearchHit>> search(String query, {String mode = 'fts', int maxResults = 20});
  Future<CatalogEntity?> getEntity(String id);
  Future<List<CatalogEdgeRef>> whereUsed(String id, {int maxResults = 50});
  Future<List<CatalogEdgeRef>> lineage(String id, {int maxDepth = 5});
  Future<List<DriverTreeField>> driverTree(String id);
  Future<List<CatalogSyncRun>> syncStatus({int maxResults = 10});
  Future<CatalogStats> stats();
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
