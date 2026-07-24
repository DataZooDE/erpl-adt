import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../client/catalog_models.dart';
import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';
import 'dashed_border.dart';

enum RelationshipLens { whereUsed, lineage, driverTree }

extension RelationshipLensLabel on RelationshipLens {
  String get label => switch (this) {
        RelationshipLens.whereUsed => 'Where-used',
        RelationshipLens.lineage => 'Lineage',
        RelationshipLens.driverTree => 'Driver tree',
      };
}

/// Shared rendering for the 3 relationship lenses — used both by the
/// full-screen /entity/:id/relate route and embedded (compact) in Entity
/// Detail's Relationships tab, so where-used/lineage/driver-tree logic
/// isn't duplicated in three places the way it used to be (a standalone
/// LineageScreen, a standalone DriverTreeScreen, and inline tabs, each with
/// their own rendering).
///
/// Every related-entity tap re-centers exploration on that entity — for
/// where-used/lineage this means navigating to `/entity/<id>/relate` with
/// the same lens, not just Entity Detail's Overview, so the user stays in
/// context instead of dead-ending.
class RelationshipLensView extends ConsumerWidget {
  final String entityId;
  final RelationshipLens lens;
  final bool compact;

  const RelationshipLensView({
    super.key,
    required this.entityId,
    required this.lens,
    this.compact = false,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    switch (lens) {
      case RelationshipLens.whereUsed:
        return _EdgeList(
          async: ref.watch(whereUsedProvider(entityId)),
          emptyText: 'Nothing in the cached catalog references this entity.',
          icon: Icons.arrow_back,
          idOf: (e) => e.fromId,
          compact: compact,
        );
      case RelationshipLens.lineage:
        return _EdgeList(
          async: ref.watch(lineageProvider(entityId)),
          emptyText: 'No downstream lineage recorded.',
          icon: Icons.arrow_forward,
          idOf: (e) => e.toId,
          compact: compact,
          headerBuilder: (chain) => '${chain.length} hop(s) downstream',
        );
      case RelationshipLens.driverTree:
        return _DriverTreePanel(
          async: ref.watch(driverTreeProvider(entityId)),
          compact: compact,
        );
    }
  }
}

class _EdgeList extends StatelessWidget {
  final AsyncValue<List<CatalogEdgeRef>> async;
  final String emptyText;
  final IconData icon;
  final String Function(CatalogEdgeRef) idOf;
  final bool compact;
  final String Function(List<CatalogEdgeRef>)? headerBuilder;

  const _EdgeList({
    required this.async,
    required this.emptyText,
    required this.icon,
    required this.idOf,
    required this.compact,
    this.headerBuilder,
  });

  @override
  Widget build(BuildContext context) {
    return async.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, _) => Center(child: Text('Failed to load: $err')),
      data: (edges) {
        if (edges.isEmpty) {
          return Center(child: Text(emptyText));
        }
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (headerBuilder != null)
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                child: Text(headerBuilder!(edges), style: Theme.of(context).textTheme.titleSmall),
              ),
            Expanded(
              child: ListView.builder(
                padding: EdgeInsets.symmetric(horizontal: compact ? 0 : 16, vertical: 4),
                itemCount: edges.length,
                itemBuilder: (context, i) => _EdgeTile(edge: edges[i], icon: icon, idOf: idOf),
              ),
            ),
          ],
        );
      },
    );
  }
}

class _EdgeTile extends StatelessWidget {
  final CatalogEdgeRef edge;
  final IconData icon;
  final String Function(CatalogEdgeRef) idOf;
  const _EdgeTile({required this.edge, required this.icon, required this.idOf});

  bool get _isUnresolved => edge.resolution != 'resolved' && edge.resolution.isNotEmpty;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final targetId = idOf(edge);
    final tile = ListTile(
      dense: true,
      leading: Icon(icon, color: _isUnresolved ? scheme.outline : null),
      title: Text(
        targetId,
        style: CatalogTheme.monoStyle(context).copyWith(
          color: _isUnresolved ? scheme.outline : null,
          fontStyle: _isUnresolved ? FontStyle.italic : FontStyle.normal,
        ),
      ),
      subtitle: Text(edge.detail == null
          ? '${edge.kind} · ${edge.resolution}'
          : '${edge.kind} · ${edge.resolution} · '
              '${edge.detail!.entries.map((e) => '${e.key}=${e.value}').join(', ')}'),
      // Land on the related entity's own detail page — not directly on
      // *its* Relationships view, which is frequently empty for whatever
      // was tapped and reads as "this goes nowhere." The entity's
      // Relationships tab (or the summary cards on its Overview) is one
      // tap away from there if the user wants to keep exploring outward.
      onTap: () => context.push('/entity/$targetId'),
    );

    if (!_isUnresolved) {
      return Padding(padding: const EdgeInsets.symmetric(vertical: 2), child: tile);
    }
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: DashedBorder(
        color: scheme.outline,
        padding: EdgeInsets.zero,
        child: tile,
      ),
    );
  }
}

/// Driver tree renders as a visually distinct "composition" panel — this is
/// a formula/decomposition breakdown of one measure-bearing entity, not a
/// traversal chain like the other two lenses, so it shouldn't look like one.
/// DriverTreeField today is flat (name+formula, no parent/child) — true
/// hierarchical nesting is an explicit non-goal of this pass.
class _DriverTreePanel extends StatelessWidget {
  final AsyncValue<List<DriverTreeField>> async;
  final bool compact;
  const _DriverTreePanel({required this.async, required this.compact});

  @override
  Widget build(BuildContext context) {
    return async.when(
      loading: () => const Center(child: CircularProgressIndicator()),
      error: (err, _) => Center(child: Text('Failed to load driver tree: $err')),
      data: (fields) {
        if (fields.isEmpty) {
          return const Center(child: Text('No calculated key-figure formulas captured for this entity.'));
        }
        final scheme = Theme.of(context).colorScheme;
        return ListView(
          padding: EdgeInsets.all(compact ? 0 : 16),
          children: [
            Container(
              margin: const EdgeInsets.only(bottom: 4),
              padding: const EdgeInsets.all(8),
              decoration: BoxDecoration(
                color: scheme.secondaryContainer.withValues(alpha: 0.4),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Row(
                children: [
                  Icon(Icons.functions, size: 16, color: scheme.onSecondaryContainer),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      'Composition — what feeds this figure, not a traversal chain.',
                      style: Theme.of(context)
                          .textTheme
                          .labelSmall
                          ?.copyWith(color: scheme.onSecondaryContainer),
                    ),
                  ),
                ],
              ),
            ),
            for (final field in fields)
              Card(
                margin: const EdgeInsets.symmetric(vertical: 4),
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
              ),
          ],
        );
      },
    );
  }
}
