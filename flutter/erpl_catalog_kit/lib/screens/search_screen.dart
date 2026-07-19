import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../state/catalog_providers.dart';
import '../widgets/catalog_widgets.dart';

/// S1 Home/Search — the landing screen: a search omnibox and results list.
class SearchScreen extends ConsumerWidget {
  const SearchScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final query = ref.watch(searchQueryProvider);
    final mode = ref.watch(searchModeProvider);
    final resultsAsync = ref.watch(searchResultsProvider);

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SearchOmnibox(
            initialQuery: query,
            mode: mode,
            onSubmitted: (q) => ref.read(searchQueryProvider.notifier).state = q,
            onModeChanged: (m) => ref.read(searchModeProvider.notifier).state = m,
          ),
          const SizedBox(height: 16),
          Expanded(
            child: resultsAsync.when(
              loading: () => const Center(child: CircularProgressIndicator()),
              error: (err, _) => Center(child: Text('Search failed: $err')),
              data: (hits) {
                if (query.trim().isEmpty) {
                  return const Center(child: Text('Search the catalog to get started.'));
                }
                if (hits.isEmpty) {
                  return const Center(child: Text('No results.'));
                }
                return ListView.builder(
                  itemCount: hits.length,
                  itemBuilder: (context, i) {
                    final hit = hits[i];
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
      ),
    );
  }
}
