import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../state/catalog_providers.dart';
import '../widgets/catalog_widgets.dart';

const _kDomains = ['ABAP', 'DDIC', 'CDS', 'BW'];

/// S1 Discovery — the merged Search+Browse screen. One query box, domain
/// filter chips, and a curated-only toggle all narrow the same
/// server-side-paginated result list; an empty query browses everything
/// instead of showing a dead end. This replaces SearchScreen and
/// BrowseScreen, which used to be two separate screens with Browse having
/// no query of its own.
///
/// Query/domain/curated-only state round-trips through the URL
/// (`/?q=...&domain=...&curated=1`) so a search is a shareable, bookmarkable
/// link and reloading/reopening it reproduces the same results — not just
/// in-memory Riverpod state that vanishes on navigation.
class DiscoveryScreen extends ConsumerStatefulWidget {
  const DiscoveryScreen({super.key});

  @override
  ConsumerState<DiscoveryScreen> createState() => _DiscoveryScreenState();
}

class _DiscoveryScreenState extends ConsumerState<DiscoveryScreen> {
  final _scrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    _scrollController.addListener(_maybeLoadMore);
    // Seed provider state from the URL exactly once on mount — after this,
    // state flows one-way (providers -> URL) via the ref.listen calls in
    // build(), so re-navigating to the same route doesn't fight the user's
    // typing by re-reading the URL on every rebuild.
    final params = GoRouterState.of(context).uri.queryParameters;
    if (params.containsKey('q')) {
      ref.read(searchQueryProvider.notifier).state = params['q']!;
    }
    if (params.containsKey('domain')) {
      ref.read(domainFilterProvider.notifier).state = params['domain'];
    }
    if (params.containsKey('type')) {
      ref.read(objectTypeFilterProvider.notifier).state = params['type'];
    }
    if (params.containsKey('subtype')) {
      ref.read(subtypeFilterProvider.notifier).state = params['subtype'];
    }
    if (params['curated'] == '1') {
      ref.read(curatedOnlyProvider.notifier).state = true;
    }
  }

  @override
  void dispose() {
    _scrollController.removeListener(_maybeLoadMore);
    _scrollController.dispose();
    super.dispose();
  }

  void _maybeLoadMore() {
    if (_scrollController.position.pixels >
        _scrollController.position.maxScrollExtent - 400) {
      ref.read(discoveryResultsProvider.notifier).loadMore();
    }
  }

  void _syncUrl(String query, String? domain, String? objectType, String? subtype,
      bool curatedOnly) {
    final uri = Uri(path: '/', queryParameters: {
      if (query.isNotEmpty) 'q': query,
      if (domain != null) 'domain': domain,
      if (objectType != null) 'type': objectType,
      if (subtype != null) 'subtype': subtype,
      if (curatedOnly) 'curated': '1',
    });
    final target = uri.toString();
    // Replace (not push) — every keystroke/filter toggle shouldn't spam
    // browser history; the URL just needs to always reflect current state.
    if (GoRouterState.of(context).uri.toString() != target) {
      context.go(target);
    }
  }

  @override
  Widget build(BuildContext context) {
    final query = ref.watch(searchQueryProvider);
    final domain = ref.watch(domainFilterProvider);
    final objectType = ref.watch(objectTypeFilterProvider);
    final subtype = ref.watch(subtypeFilterProvider);
    final curatedOnly = ref.watch(curatedOnlyProvider);
    final state = ref.watch(discoveryResultsProvider);

    ref.listen(searchQueryProvider,
        (_, q) => _syncUrl(q, domain, objectType, subtype, curatedOnly));
    ref.listen(domainFilterProvider,
        (_, d) => _syncUrl(query, d, objectType, subtype, curatedOnly));
    ref.listen(objectTypeFilterProvider,
        (_, t) => _syncUrl(query, domain, t, subtype, curatedOnly));
    ref.listen(subtypeFilterProvider,
        (_, s) => _syncUrl(query, domain, objectType, s, curatedOnly));
    ref.listen(curatedOnlyProvider,
        (_, c) => _syncUrl(query, domain, objectType, subtype, c));

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SearchOmnibox(
            initialQuery: query,
            onChanged: (q) => ref.read(searchQueryProvider.notifier).state = q,
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            runSpacing: 4,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              ChoiceChip(
                label: const Text('All domains'),
                selected: domain == null,
                onSelected: (_) {
                  ref.read(domainFilterProvider.notifier).state = null;
                  ref.read(objectTypeFilterProvider.notifier).state = null;
                  ref.read(subtypeFilterProvider.notifier).state = null;
                },
              ),
              for (final d in _kDomains)
                ChoiceChip(
                  label: Text(d),
                  selected: domain == d,
                  onSelected: (_) {
                    ref.read(domainFilterProvider.notifier).state = domain == d ? null : d;
                    // An object-type (and its subtype) filter from a
                    // different domain no longer makes sense once the
                    // domain changes.
                    ref.read(objectTypeFilterProvider.notifier).state = null;
                    ref.read(subtypeFilterProvider.notifier).state = null;
                  },
                ),
              const SizedBox(width: 8),
              FilterChip(
                label: const Text('Curated only'),
                avatar: const Icon(Icons.verified, size: 16),
                selected: curatedOnly,
                onSelected: (v) => ref.read(curatedOnlyProvider.notifier).state = v,
              ),
            ],
          ),
          if (domain != null) ...[
            const SizedBox(height: 8),
            _ObjectTypeFilterRow(domain: domain, selected: objectType),
          ],
          if (domain != null && objectType != null) ...[
            const SizedBox(height: 8),
            _ObjectSubtypeFilterRow(
                domain: domain, objectType: objectType, selected: subtype),
          ],
          const SizedBox(height: 12),
          Expanded(child: _buildResults(context, state, query)),
        ],
      ),
    );
  }

  Widget _buildResults(BuildContext context, DiscoveryState state, String query) {
    if (state.loading && state.hits.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (state.error != null && state.hits.isEmpty) {
      return Center(child: Text('Search failed: ${state.error}'));
    }
    if (state.hits.isEmpty) {
      return Center(
        child: Text(
          query.trim().isEmpty
              ? 'Nothing in the catalog yet — sync a package or InfoArea to get started.'
              : 'No results for "$query".',
        ),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
          child: Text(
            state.hasMore
                ? 'Showing ${state.hits.length}+ results'
                : 'Showing ${state.hits.length} result${state.hits.length == 1 ? '' : 's'}',
            style: Theme.of(context).textTheme.labelMedium,
          ),
        ),
        Expanded(
          child: ListView.builder(
            controller: _scrollController,
            itemCount: state.hits.length + (state.hasMore ? 1 : 0),
            itemBuilder: (context, i) {
              if (i >= state.hits.length) {
                return const Padding(
                  padding: EdgeInsets.symmetric(vertical: 16),
                  child: Center(child: CircularProgressIndicator()),
                );
              }
              final hit = state.hits[i];
              return EntityChip(
                id: hit.entity.id,
                domain: hit.entity.domain,
                objectType: hit.entity.objectType,
                technicalName: hit.entity.technicalName,
                displayName: hit.entity.displayName,
                packageOrInfoarea: hit.entity.packageOrInfoarea,
                isCurated: hit.entity.isCurated,
                // go (not push) so the entity's URL is reflected in the
                // address bar and is copy-pastable; browser back returns to
                // this search (its state is URL-serialized above).
                onTap: () => context.go('/entity/${hit.entity.id}'),
              );
            },
          ),
        ),
      ],
    );
  }
}

/// Object-type chips scoped to the selected domain, e.g. TABL/DT · TABL/DS
/// · DDLS/DF for DDIC, or IOBJ · ADSO · CUBE · ELEM for BW — derived from
/// [objectTypesProvider] (what's actually in the catalog) rather than a
/// hardcoded list, since object types vary a lot per domain and per system.
class _ObjectTypeFilterRow extends ConsumerWidget {
  final String domain;
  final String? selected;
  const _ObjectTypeFilterRow({required this.domain, required this.selected});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final typesAsync = ref.watch(objectTypesProvider);
    return typesAsync.when(
      loading: () => const SizedBox(
        height: 32,
        child: Center(child: SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))),
      ),
      error: (_, _) => const SizedBox.shrink(),
      data: (types) {
        final inDomain = types.where((t) => t.domain == domain).toList()
          ..sort((a, b) => a.objectType.compareTo(b.objectType));
        if (inDomain.isEmpty) return const SizedBox.shrink();

        return Wrap(
          spacing: 6,
          runSpacing: 4,
          children: [
            for (final t in inDomain)
              FilterChip(
                label: Text('${t.objectType} (${t.count})',
                    style: const TextStyle(fontSize: 11)),
                visualDensity: VisualDensity.compact,
                selected: selected == t.objectType,
                onSelected: (v) {
                  ref.read(objectTypeFilterProvider.notifier).state =
                      v ? t.objectType : null;
                  // A subtype filter scoped to the old object type no
                  // longer makes sense once the object type changes.
                  ref.read(subtypeFilterProvider.notifier).state = null;
                },
              ),
          ],
        );
      },
    );
  }
}

/// Object-subtype chips scoped to the selected (domain, object_type), e.g.
/// REP/VAR/CKF/RKF/FILT/STR under BW's ELEM — derived from
/// [objectSubtypesProvider]. Only BW's ELEM has subtype data today; every
/// other (domain, object_type) pair renders nothing, not an empty row.
class _ObjectSubtypeFilterRow extends ConsumerWidget {
  final String domain;
  final String objectType;
  final String? selected;
  const _ObjectSubtypeFilterRow({
    required this.domain,
    required this.objectType,
    required this.selected,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final subtypesAsync = ref.watch(objectSubtypesProvider);
    return subtypesAsync.when(
      loading: () => const SizedBox.shrink(),
      error: (_, _) => const SizedBox.shrink(),
      data: (subtypes) {
        final inScope = subtypes
            .where((t) => t.domain == domain && t.objectType == objectType)
            .toList()
          ..sort((a, b) => a.objectSubtype.compareTo(b.objectSubtype));
        if (inScope.isEmpty) return const SizedBox.shrink();

        return Wrap(
          spacing: 6,
          runSpacing: 4,
          children: [
            for (final t in inScope)
              FilterChip(
                label: Text('${t.objectSubtype} (${t.count})',
                    style: const TextStyle(fontSize: 11)),
                visualDensity: VisualDensity.compact,
                selected: selected == t.objectSubtype,
                onSelected: (v) => ref.read(subtypeFilterProvider.notifier).state =
                    v ? t.objectSubtype : null,
              ),
          ],
        );
      },
    );
  }
}
