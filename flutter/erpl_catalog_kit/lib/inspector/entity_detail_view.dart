import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../client/catalog_models.dart';
import '../md/curated_text.dart';
import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';
import '../widgets/catalog_widgets.dart';

/// S3 Entity Detail — tabbed view (Overview / Technical / Where-Used /
/// Lineage / Driver Tree), deep-linkable via go_router at /entity/:id.
class EntityDetailView extends ConsumerWidget {
  final String entityId;
  const EntityDetailView({super.key, required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final entityAsync = ref.watch(entityDetailProvider(entityId));

    return entityAsync.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, _) => Center(child: Text('Failed to load entity: $err')),
      data: (entity) {
        if (entity == null) {
          return const Center(child: Text('Entity not found in the cached catalog.'));
        }
        return DefaultTabController(
          length: 5,
          child: Column(
            children: [
              _Header(entity: entity),
              const TabBar(tabs: [
                Tab(text: 'Overview'),
                Tab(text: 'Technical'),
                Tab(text: 'Where-Used'),
                Tab(text: 'Lineage'),
                Tab(text: 'Driver Tree'),
              ]),
              Expanded(
                child: TabBarView(children: [
                  _OverviewTab(entity: entity),
                  _TechnicalTab(entity: entity),
                  _WhereUsedTab(entityId: entityId),
                  _LineageTab(entityId: entityId),
                  _DriverTreeTab(entityId: entityId),
                ]),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _Header extends StatelessWidget {
  final CatalogEntity entity;
  const _Header({required this.entity});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(16),
      child: Row(
        children: [
          TypeIcon(domain: entity.domain, objectType: entity.objectType),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  entity.displayName.isEmpty ? entity.technicalName : entity.displayName,
                  style: Theme.of(context).textTheme.headlineSmall,
                ),
                Text(entity.technicalName, style: CatalogTheme.monoStyle(context)),
              ],
            ),
          ),
          ConfidentialityBadge(level: entity.bizConfidentiality),
          const SizedBox(width: 8),
          ProvenanceBadge(curated: entity.bizDefinition != null),
          const SizedBox(width: 8),
          FilledButton.tonalIcon(
            onPressed: () => context.push('/curate/${entity.id}'),
            icon: const Icon(Icons.edit_note),
            label: const Text('Curate'),
          ),
        ],
      ),
    );
  }
}

class _OverviewTab extends StatelessWidget {
  final CatalogEntity entity;
  const _OverviewTab({required this.entity});

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('Business definition', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          entity.bizDefinition != null
              ? CuratedText(text: entity.bizDefinition!)
              : Text('Not yet curated.', style: Theme.of(context).textTheme.bodyMedium),
          const SizedBox(height: 24),
          FactSheet(entries: [
            MapEntry('Owner', entity.bizOwner),
            MapEntry('Line of Business', entity.bizLob),
            MapEntry('Package / InfoArea', entity.packageOrInfoarea),
          ]),
        ],
      ),
    );
  }
}

class _TechnicalTab extends StatelessWidget {
  final CatalogEntity entity;
  const _TechnicalTab({required this.entity});

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          FactSheet(entries: [
            MapEntry('Entity ID', entity.id),
            MapEntry('Domain', entity.domain),
            MapEntry('Object type', entity.objectType),
            MapEntry('Technical name', entity.technicalName),
          ]),
          const SizedBox(height: 16),
          Text('Fields (${entity.fields.length})', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          if (entity.fields.isEmpty) const Text('No fields resolved for this entity.'),
          for (final field in entity.fields)
            ListTile(
              dense: true,
              title: Text(field.name, style: CatalogTheme.monoStyle(context)),
              subtitle: field.dataType != null ? Text(field.dataType!) : null,
              trailing: field.role != null ? Chip(label: Text(field.role!)) : null,
            ),
        ],
      ),
    );
  }
}

class _WhereUsedTab extends ConsumerWidget {
  final String entityId;
  const _WhereUsedTab({required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final edgesAsync = ref.watch(whereUsedProvider(entityId));
    return edgesAsync.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, _) => Center(child: Text('Failed to load where-used: $err')),
      data: (edges) {
        if (edges.isEmpty) {
          return const Center(child: Text('Nothing in the cached catalog references this entity.'));
        }
        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: edges.length,
          itemBuilder: (context, i) {
            final edge = edges[i];
            return ListTile(
              leading: const Icon(Icons.arrow_back),
              title: Text(edge.fromId, style: CatalogTheme.monoStyle(context)),
              subtitle: Text('${edge.kind} · ${edge.resolution}'),
              onTap: () => context.push('/entity/${edge.fromId}'),
            );
          },
        );
      },
    );
  }
}

class _LineageTab extends ConsumerWidget {
  final String entityId;
  const _LineageTab({required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final lineageAsync = ref.watch(lineageProvider(entityId));
    return lineageAsync.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, _) => Center(child: Text('Failed to load lineage: $err')),
      data: (chain) {
        return Column(
          children: [
            Padding(
              padding: const EdgeInsets.all(16),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text('${chain.length} hop(s) downstream', style: Theme.of(context).textTheme.titleMedium),
                  OutlinedButton.icon(
                    onPressed: () => context.push('/entity/$entityId/lineage'),
                    icon: const Icon(Icons.open_in_full),
                    label: const Text('Expand'),
                  ),
                ],
              ),
            ),
            if (chain.isEmpty) const Expanded(child: Center(child: Text('No downstream lineage recorded.'))),
            if (chain.isNotEmpty)
              Expanded(
                child: ListView.builder(
                  padding: const EdgeInsets.symmetric(horizontal: 16),
                  itemCount: chain.length,
                  itemBuilder: (context, i) {
                    final edge = chain[i];
                    return ListTile(
                      leading: const Icon(Icons.arrow_forward),
                      title: Text(edge.toId, style: CatalogTheme.monoStyle(context)),
                      subtitle: Text(edge.kind),
                      onTap: () => context.push('/entity/${edge.toId}'),
                    );
                  },
                ),
              ),
          ],
        );
      },
    );
  }
}

class _DriverTreeTab extends ConsumerWidget {
  final String entityId;
  const _DriverTreeTab({required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final treeAsync = ref.watch(driverTreeProvider(entityId));
    return treeAsync.when(
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
            return Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(field.name, style: Theme.of(context).textTheme.titleSmall),
                    const SizedBox(height: 4),
                    Text(field.formula, style: CatalogTheme.monoStyle(context)),
                  ],
                ),
              ),
            );
          },
        );
      },
    );
  }
}
