import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';

/// S5 Driver Tree (full-screen) — expandable formula tree for calculated
/// key figures, from `fields.formula`.
class DriverTreeScreen extends ConsumerWidget {
  final String entityId;
  const DriverTreeScreen({super.key, required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final treeAsync = ref.watch(driverTreeProvider(entityId));

    return Scaffold(
      appBar: AppBar(
        title: Text('Driver Tree — $entityId', style: CatalogTheme.monoStyle(context, fontSize: 14)),
        leading: IconButton(icon: const Icon(Icons.close), onPressed: () => context.pop()),
      ),
      body: treeAsync.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (err, _) => Center(child: Text('Failed to load driver tree: $err')),
        data: (fields) {
          if (fields.isEmpty) {
            return const Center(child: Text('No calculated key-figure formulas captured for this entity.'));
          }
          return ListView.builder(
            padding: const EdgeInsets.all(16),
            itemCount: fields.length,
            itemBuilder: (context, i) {
              final field = fields[i];
              return ExpansionTile(
                title: Text(field.name),
                children: [
                  Padding(
                    padding: const EdgeInsets.all(12),
                    child: Align(
                      alignment: Alignment.centerLeft,
                      child: SelectableText(field.formula, style: CatalogTheme.monoStyle(context)),
                    ),
                  ),
                ],
              );
            },
          );
        },
      ),
    );
  }
}
