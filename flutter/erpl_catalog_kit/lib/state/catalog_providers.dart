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

/// Carries the export scope (a package/InfoArea name, typically) from
/// wherever the user was — an entity's Package/InfoArea, or the currently
/// selected domain filter — into Feed Export, so its scope step doesn't
/// default to a `<pkg>` placeholder the way it used to.
final exportScopeProvider = StateProvider<String?>((ref) => null);

/// Current Discovery query (S1 SearchOmnibox) and mode. An empty query is a
/// valid, common state — it means "browse everything," not "nothing to
/// show" (see [discoveryResultsProvider]).
final searchQueryProvider = StateProvider<String>((ref) => '');
final searchModeProvider = StateProvider<String>((ref) => 'fts');

/// Discovery's domain filter (null = all domains), object-type filter (null
/// = all types within the domain), and curated-only toggle — narrowed
/// server-side via catalog_search's `domain`/`object_type`/`curated_only`
/// params, not filtered client-side over an already-fetched page.
final domainFilterProvider = StateProvider<String?>((ref) => null);
final objectTypeFilterProvider = StateProvider<String?>((ref) => null);
/// Object-subtype filter (null = all subtypes within the object type) —
/// only meaningful today for BW's ELEM object_type, where a subtype
/// (REP/VAR/CKF/RKF/FILT/STR) distinguishes a real query from everything
/// else ELEM also covers.
final subtypeFilterProvider = StateProvider<String?>((ref) => null);
final curatedOnlyProvider = StateProvider<bool>((ref) => false);

/// Distinct (domain, object_type) pairs actually present in the catalog —
/// backs the object-type filter so it only ever offers types that are
/// really there (BW's IOBJ/ADSO/CUBE/... vs ABAP's TABL/CLAS/FUGR/... vary
/// too much to hardcode).
final objectTypesProvider = FutureProvider<List<CatalogObjectTypeCount>>((ref) async {
  final client = ref.watch(catalogClientProvider);
  return client.objectTypes();
});

/// Distinct (domain, object_type, object_subtype) triples actually present —
/// backs the object-subtype filter one level deeper than [objectTypesProvider].
final objectSubtypesProvider = FutureProvider<List<CatalogObjectSubtypeCount>>((ref) async {
  final client = ref.watch(catalogClientProvider);
  return client.objectSubtypes();
});

/// One page (plus everything loaded so far via [loadMore]) of Discovery
/// results for the current query/mode/domain/curated-only combination.
class DiscoveryState {
  final List<CatalogSearchHit> hits;
  final bool hasMore;
  final int? cursor;
  final bool loading;
  final Object? error;

  const DiscoveryState({
    this.hits = const [],
    this.hasMore = false,
    this.cursor = 0,
    this.loading = false,
    this.error,
  });

  DiscoveryState copyWith({
    List<CatalogSearchHit>? hits,
    bool? hasMore,
    int? cursor,
    bool? loading,
    Object? error,
  }) {
    return DiscoveryState(
      hits: hits ?? this.hits,
      hasMore: hasMore ?? this.hasMore,
      cursor: cursor ?? this.cursor,
      loading: loading ?? this.loading,
      error: error,
    );
  }
}

class DiscoveryResultsNotifier extends StateNotifier<DiscoveryState> {
  final CatalogClient _client;
  final String query;
  final String mode;
  final String? domain;
  final String? objectType;
  final String? objectSubtype;
  final bool curatedOnly;

  DiscoveryResultsNotifier(
    this._client, {
    required this.query,
    required this.mode,
    required this.domain,
    required this.objectType,
    required this.objectSubtype,
    required this.curatedOnly,
  }) : super(const DiscoveryState()) {
    _reload();
  }

  Future<void> _reload() async {
    state = const DiscoveryState(loading: true);
    try {
      final page = await _client.search(query,
          mode: mode,
          cursor: 0,
          domain: domain,
          objectType: objectType,
          objectSubtype: objectSubtype,
          curatedOnly: curatedOnly);
      state = DiscoveryState(hits: page.hits, hasMore: page.hasMore, cursor: page.nextCursor);
    } catch (e) {
      state = DiscoveryState(error: e);
    }
  }

  Future<void> loadMore() async {
    if (state.loading || !state.hasMore || state.cursor == null) return;
    state = state.copyWith(loading: true);
    try {
      final page = await _client.search(query,
          mode: mode,
          cursor: state.cursor!,
          domain: domain,
          objectType: objectType,
          objectSubtype: objectSubtype,
          curatedOnly: curatedOnly);
      state = DiscoveryState(
        hits: [...state.hits, ...page.hits],
        hasMore: page.hasMore,
        cursor: page.nextCursor,
      );
    } catch (e) {
      state = state.copyWith(loading: false, error: e);
    }
  }
}

/// Resets (re-fetches page 1) whenever query/mode/domain/object-type/
/// subtype/curated-only change — `autoDispose` so a stale notifier from a
/// prior filter combination doesn't linger.
final discoveryResultsProvider =
    StateNotifierProvider.autoDispose<DiscoveryResultsNotifier, DiscoveryState>((ref) {
  final client = ref.watch(catalogClientProvider);
  final query = ref.watch(searchQueryProvider);
  final mode = ref.watch(searchModeProvider);
  final domain = ref.watch(domainFilterProvider);
  final objectType = ref.watch(objectTypeFilterProvider);
  final objectSubtype = ref.watch(subtypeFilterProvider);
  final curatedOnly = ref.watch(curatedOnlyProvider);
  return DiscoveryResultsNotifier(client,
      query: query,
      mode: mode,
      domain: domain,
      objectType: objectType,
      objectSubtype: objectSubtype,
      curatedOnly: curatedOnly);
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
