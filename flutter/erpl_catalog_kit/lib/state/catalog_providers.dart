import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../client/catalog_client.dart';
import '../client/catalog_models.dart';
import '../client/mcp_http_catalog_client.dart';
import '../config/catalog_config.dart';

/// App-wide config — overridden at app startup via ProviderScope(overrides:).
final catalogConfigProvider = Provider<CatalogConfig>((ref) {
  throw UnimplementedError('catalogConfigProvider must be overridden at app startup');
});

/// The active CatalogClient — hosted mode (MCP-over-HTTP) today; a
/// DuckDbCatalogClient (desktop mode) can be swapped in later behind the
/// same provider without touching any screen.
final catalogClientProvider = Provider<CatalogClient>((ref) {
  final config = ref.watch(catalogConfigProvider);
  return McpHttpCatalogClient(baseUrl: config.mcpBaseUrl);
});

/// Current search query (S1 SearchOmnibox) and mode.
final searchQueryProvider = StateProvider<String>((ref) => '');
final searchModeProvider = StateProvider<String>((ref) => 'fts');

/// Search results for the current query — re-fetches whenever the query or
/// mode changes.
final searchResultsProvider = FutureProvider<List<CatalogSearchHit>>((ref) async {
  final query = ref.watch(searchQueryProvider);
  final mode = ref.watch(searchModeProvider);
  if (query.trim().isEmpty) return const [];
  final client = ref.watch(catalogClientProvider);
  return client.search(query, mode: mode);
});

/// One entity's detail (S3) — family-keyed by entity id so multiple tabs/
/// routes can be open without refetching each other.
final entityDetailProvider = FutureProvider.family<CatalogEntity?, String>((ref, id) async {
  final client = ref.watch(catalogClientProvider);
  return client.getEntity(id);
});

final whereUsedProvider = FutureProvider.family<List<CatalogEdgeRef>, String>((ref, id) async {
  final client = ref.watch(catalogClientProvider);
  return client.whereUsed(id);
});

final lineageProvider = FutureProvider.family<List<CatalogEdgeRef>, String>((ref, id) async {
  final client = ref.watch(catalogClientProvider);
  return client.lineage(id);
});

final driverTreeProvider = FutureProvider.family<List<DriverTreeField>, String>((ref, id) async {
  final client = ref.watch(catalogClientProvider);
  return client.driverTree(id);
});

final syncStatusProvider = FutureProvider<List<CatalogSyncRun>>((ref) async {
  final client = ref.watch(catalogClientProvider);
  return client.syncStatus();
});

final catalogStatsProvider = FutureProvider<CatalogStats>((ref) async {
  final client = ref.watch(catalogClientProvider);
  return client.stats();
});
