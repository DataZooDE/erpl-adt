import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../state/catalog_providers.dart';
import '../widgets/catalog_widgets.dart';

/// S2 Browse Catalog — a domain-filtered variant of search (the cache-only
/// tools don't expose a separate "list all" endpoint, so this reuses
/// catalog_search with an empty query facet — a wildcard-style browse is a
/// natural extension of catalog_search once the store supports it).
class BrowseScreen extends ConsumerStatefulWidget {
  const BrowseScreen({super.key});

  @override
  ConsumerState<BrowseScreen> createState() => _BrowseScreenState();
}

class _BrowseScreenState extends ConsumerState<BrowseScreen> {
  String? _domainFilter;

  static const _domains = ['ABAP', 'DDIC', 'CDS', 'BW'];

  @override
  Widget build(BuildContext context) {
    final resultsAsync = ref.watch(searchResultsProvider);

    return Row(
      children: [
        SizedBox(
          width: 200,
          child: ListView(
            padding: const EdgeInsets.all(16),
            children: [
              Text('Domain', style: Theme.of(context).textTheme.titleSmall),
              const SizedBox(height: 8),
              for (final domain in _domains)
                RadioListTile<String?>(
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                  title: Text(domain),
                  value: domain,
                  groupValue: _domainFilter,
                  onChanged: (v) => setState(() => _domainFilter = v == _domainFilter ? null : v),
                ),
            ],
          ),
        ),
        const VerticalDivider(width: 1),
        Expanded(
          child: resultsAsync.when(
            loading: () => const Center(child: CircularProgressIndicator()),
            error: (err, _) => Center(child: Text('Failed: $err')),
            data: (hits) {
              final filtered = _domainFilter == null
                  ? hits
                  : hits.where((h) => h.entity.domain == _domainFilter).toList();
              if (filtered.isEmpty) {
                return const Center(child: Text('Search from the Home screen, then filter by domain here.'));
              }
              return ListView.builder(
                padding: const EdgeInsets.all(16),
                itemCount: filtered.length,
                itemBuilder: (context, i) {
                  final hit = filtered[i];
                  return EntityChip(
                    id: hit.entity.id,
                    domain: hit.entity.domain,
                    objectType: hit.entity.objectType,
                    technicalName: hit.entity.technicalName,
                    displayName: hit.entity.displayName,
                    onTap: () => context.push('/entity/${hit.entity.id}'),
                  );
                },
              );
            },
          ),
        ),
      ],
    );
  }
}
