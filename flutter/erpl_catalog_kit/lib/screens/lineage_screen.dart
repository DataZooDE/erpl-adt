import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';

/// S4 Data Lineage (full-screen) — the expanded view of the entity detail's
/// Lineage tab. Renders the same hop chain as a left-to-right flow list;
/// a true graph-canvas layout (per HLD §4's `graphview` recommendation) is
/// a follow-on once usage shows pan/click-to-navigate is actually needed.
class LineageScreen extends ConsumerWidget {
  final String entityId;
  const LineageScreen({super.key, required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final lineageAsync = ref.watch(lineageProvider(entityId));

    return Scaffold(
      appBar: AppBar(
        title: Text('Lineage — $entityId', style: CatalogTheme.monoStyle(context, fontSize: 14)),
        leading: IconButton(icon: const Icon(Icons.close), onPressed: () => context.pop()),
      ),
      body: lineageAsync.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (err, _) => Center(child: Text('Failed to load lineage: $err')),
        data: (chain) {
          final nodes = [entityId, ...chain.map((e) => e.toId)];
          if (chain.isEmpty) {
            return const Center(child: Text('No downstream lineage recorded for this entity.'));
          }
          return SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.all(32),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                for (var i = 0; i < nodes.length; i++) ...[
                  _LineageNode(
                    id: nodes[i],
                    isRoot: i == 0,
                    onTap: () => context.push('/entity/${nodes[i]}'),
                  ),
                  if (i < nodes.length - 1)
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 8),
                      child: Column(
                        children: [
                          const Icon(Icons.arrow_forward),
                          Text(chain[i].kind, style: Theme.of(context).textTheme.labelSmall),
                        ],
                      ),
                    ),
                ],
              ],
            ),
          );
        },
      ),
    );
  }
}

class _LineageNode extends StatelessWidget {
  final String id;
  final bool isRoot;
  final VoidCallback onTap;
  const _LineageNode({required this.id, required this.isRoot, required this.onTap});

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(8),
      child: Container(
        width: 180,
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: isRoot ? scheme.primaryContainer : scheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: scheme.outlineVariant),
        ),
        child: Text(
          id,
          style: CatalogTheme.monoStyle(context, fontSize: 11),
          maxLines: 3,
          overflow: TextOverflow.ellipsis,
        ),
      ),
    );
  }
}
