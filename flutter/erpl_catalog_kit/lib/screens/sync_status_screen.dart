import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../state/catalog_providers.dart';

/// S7 Sync Status — sync_runs history + cache-wide stats.
class SyncStatusScreen extends ConsumerWidget {
  const SyncStatusScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final statsAsync = ref.watch(catalogStatsProvider);
    final runsAsync = ref.watch(syncStatusProvider);

    return RefreshIndicator(
      onRefresh: () async {
        ref.invalidate(catalogStatsProvider);
        ref.invalidate(syncStatusProvider);
      },
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text('Cache health', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          statsAsync.when(
            loading: () => const LinearProgressIndicator(),
            error: (err, _) => Text('Failed to load stats: $err'),
            data: (stats) => Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                _StatCard(label: 'Entities', value: stats.entityCount),
                _StatCard(label: 'Fields', value: stats.fieldCount),
                _StatCard(label: 'Edges', value: stats.edgeCount),
                _StatCard(label: 'Unresolved edges', value: stats.unresolvedEdgeCount),
                _StatCard(label: 'Curated entities', value: stats.curatedEntityCount),
              ],
            ),
          ),
          const SizedBox(height: 24),
          Text('Recent sync runs', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          runsAsync.when(
            loading: () => const LinearProgressIndicator(),
            error: (err, _) => Text('Failed to load sync runs: $err'),
            data: (runs) {
              if (runs.isEmpty) {
                return const Text('No sync runs recorded yet — run `erpl-adt catalog sync` from the CLI.');
              }
              return Column(
                children: [
                  for (final run in runs)
                    Card(
                      child: ListTile(
                        leading: Icon(run.status == 'ok' ? Icons.check_circle : Icons.error,
                            color: run.status == 'ok' ? Colors.green : Colors.red),
                        title: Text('${run.mode} · ${run.scope}'),
                        subtitle: Text('${run.startedAt} → ${run.finishedAt}'),
                        trailing: Text('+${run.added} ~${run.changed} -${run.removed}'),
                      ),
                    ),
                ],
              );
            },
          ),
        ],
      ),
    );
  }
}

class _StatCard extends StatelessWidget {
  final String label;
  final int value;
  const _StatCard({required this.label, required this.value});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Text('$value', style: Theme.of(context).textTheme.headlineMedium),
            Text(label, style: Theme.of(context).textTheme.labelMedium),
          ],
        ),
      ),
    );
  }
}
