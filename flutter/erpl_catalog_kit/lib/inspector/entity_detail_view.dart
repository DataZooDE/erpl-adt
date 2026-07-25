import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../client/catalog_models.dart';
import '../md/curated_text.dart';
import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';
import '../theme/catalog_tokens.dart';
import '../widgets/catalog_widgets.dart';
import '../widgets/relationship_lens_view.dart';

/// S3 Entity Detail — 4 tabs (Overview / Fields / Business context /
/// Relationships), deep-linkable via go_router at /entity/:id. Overview
/// deliberately visually separates technical facts (what SAP says) from
/// curated business facts (what a person said) — conflating them was
/// flagged as a real trust problem in the functional spec this screen was
/// built from.
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
          length: 4,
          child: Column(
            children: [
              _Header(entity: entity),
              TabBar(tabs: [
                const Tab(text: 'Overview'),
                Tab(text: 'Fields (${entity.fields.length})'),
                const Tab(text: 'Business context'),
                const Tab(text: 'Relationships'),
              ]),
              Expanded(
                child: TabBarView(children: [
                  _OverviewTab(entity: entity),
                  _FieldsTab(entity: entity),
                  _BusinessContextTab(entity: entity),
                  RelationshipLensView(
                    entityId: entityId,
                    lens: RelationshipLens.whereUsed,
                    compact: true,
                  ),
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
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              IconButton(
                icon: const Icon(Icons.arrow_back),
                tooltip: 'Back to results',
                onPressed: () =>
                    context.canPop() ? context.pop() : context.go('/'),
              ),
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
              ProvenanceBadge(curated: entity.isCurated),
              const SizedBox(width: 8),
              FilledButton.tonalIcon(
                onPressed: () => context.go('/curate/${entity.id}'),
                icon: const Icon(Icons.edit_note),
                label: const Text('Curate'),
              ),
            ],
          ),
          const SizedBox(height: 8),
          _FreshnessRow(entity: entity),
        ],
      ),
    );
  }
}

/// "Last extracted `{date}`" + a color dot signaling how stale the answer
/// might be — the concrete surface for extracted_at/changed_at now that
/// the backend serializes them (catalog_tool_handlers.cpp EntityToJson).
class _FreshnessRow extends StatelessWidget {
  final CatalogEntity entity;
  const _FreshnessRow({required this.entity});

  @override
  Widget build(BuildContext context) {
    final extractedAt = entity.extractedAt;
    if (extractedAt == null || extractedAt.isEmpty) return const SizedBox.shrink();

    final tokens = Theme.of(context).extension<CatalogTokens>();
    final scheme = Theme.of(context).colorScheme;
    final ageDays = _parseAgeDays(extractedAt);
    final Color dot;
    final String label;
    if (ageDays == null) {
      dot = scheme.outline;
      label = 'Last extracted $extractedAt';
    } else if (ageDays <= 7) {
      dot = tokens?.success ?? scheme.secondary;
      label = 'Fresh — extracted $ageDays day${ageDays == 1 ? '' : 's'} ago';
    } else if (ageDays <= 30) {
      dot = tokens?.warning ?? scheme.tertiary;
      label = 'Extracted $ageDays days ago';
    } else {
      dot = scheme.error;
      label = 'Stale — extracted $ageDays days ago';
    }

    return Row(
      children: [
        Container(
          width: 8,
          height: 8,
          decoration: BoxDecoration(color: dot, shape: BoxShape.circle),
        ),
        const SizedBox(width: 6),
        Text(label, style: Theme.of(context).textTheme.labelSmall),
      ],
    );
  }

  static int? _parseAgeDays(String extractedAt) {
    final parsed = DateTime.tryParse(extractedAt.replaceFirst(' ', 'T'));
    if (parsed == null) return null;
    return DateTime.now().toUtc().difference(parsed.toUtc()).inDays;
  }
}

class _OverviewTab extends ConsumerWidget {
  final CatalogEntity entity;
  const _OverviewTab({required this.entity});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final scheme = Theme.of(context).colorScheme;
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          FactSheet(entries: [
            MapEntry('Entity ID', entity.id),
            MapEntry('Domain', entity.domain),
            MapEntry('Object type', entity.objectType),
            MapEntry('Package / InfoArea', entity.packageOrInfoarea),
            if (entity.sourceTable != null) MapEntry('Source table', entity.sourceTable),
          ]),
          const SizedBox(height: 12),
          entity.fields.isEmpty
              ? Text(
                  '${entity.objectType} objects don\'t expose a field list.',
                  style: Theme.of(context).textTheme.bodySmall,
                )
              : Text('${entity.fields.length} field(s) — see the Fields tab.',
                  style: Theme.of(context).textTheme.bodySmall),
          if (entity.packageOrInfoarea != null) ...[
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: () {
                ref.read(exportScopeProvider.notifier).state = entity.packageOrInfoarea;
                context.go('/admin/feed');
              },
              icon: const Icon(Icons.download_outlined, size: 16),
              label: Text('Export ${entity.packageOrInfoarea}'),
            ),
          ],
          const SizedBox(height: 20),
          // Curated business context reads as a visually distinct block —
          // a different surface tint from the technical facts above, not
          // just another section with identical styling.
          Container(
            width: double.infinity,
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: scheme.secondaryContainer.withValues(alpha: 0.35),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: scheme.secondaryContainer),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(Icons.person_outline, size: 16, color: scheme.onSecondaryContainer),
                    const SizedBox(width: 6),
                    Text('Business context',
                        style: Theme.of(context)
                            .textTheme
                            .titleSmall
                            ?.copyWith(color: scheme.onSecondaryContainer)),
                  ],
                ),
                const SizedBox(height: 8),
                entity.bizDefinition != null
                    ? CuratedText(text: entity.bizDefinition!)
                    : Text('Not yet curated.', style: Theme.of(context).textTheme.bodyMedium),
              ],
            ),
          ),
          const SizedBox(height: 20),
          _RelationshipSummary(entityId: entity.id),
        ],
      ),
    );
  }
}

/// At-a-glance relationship counts, each linking into the full
/// Relationships screen at the right lens.
class _RelationshipSummary extends ConsumerWidget {
  final String entityId;
  const _RelationshipSummary({required this.entityId});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final whereUsed = ref.watch(whereUsedProvider(entityId));
    final lineage = ref.watch(lineageProvider(entityId));
    final driverTree = ref.watch(driverTreeProvider(entityId));

    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: [
        _SummaryCard(
          label: 'Used by',
          value: whereUsed.value?.length,
          icon: Icons.arrow_back,
          onTap: () => context.go('/entity/$entityId/relate?lens=whereUsed'),
        ),
        _SummaryCard(
          label: 'Downstream hops',
          value: lineage.value?.length,
          icon: Icons.arrow_forward,
          onTap: () => context.go('/entity/$entityId/relate?lens=lineage'),
        ),
        _SummaryCard(
          label: 'Driver formulas',
          value: driverTree.value?.length,
          icon: Icons.functions,
          onTap: () => context.go('/entity/$entityId/relate?lens=driverTree'),
        ),
      ],
    );
  }
}

class _SummaryCard extends StatelessWidget {
  final String label;
  final int? value;
  final IconData icon;
  final VoidCallback onTap;
  const _SummaryCard({required this.label, required this.value, required this.icon, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(8),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 16),
              const SizedBox(width: 8),
              Text(value?.toString() ?? '…', style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(width: 6),
              Text(label, style: Theme.of(context).textTheme.bodySmall),
            ],
          ),
        ),
      ),
    );
  }
}

class _FieldsTab extends StatelessWidget {
  final CatalogEntity entity;
  const _FieldsTab({required this.entity});

  @override
  Widget build(BuildContext context) {
    if (entity.fields.isEmpty) {
      return Center(
        child: Text('${entity.objectType} objects don\'t expose a field list.'),
      );
    }
    return ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: entity.fields.length,
      itemBuilder: (context, i) {
        final field = entity.fields[i];

        // Type line: data type, optionally with length/decimals in
        // parentheses (e.g. "S_PRICE (15,2)"), plus unit if present.
        String? typeLine;
        if (field.dataType != null) {
          final buf = StringBuffer(field.dataType!);
          if (field.length != null) {
            buf.write(field.decimals != null ? ' (${field.length},${field.decimals})' : ' (${field.length})');
          }
          if (field.unit != null) buf.write(' · ${field.unit}');
          typeLine = buf.toString();
        }

        return Card(
          margin: const EdgeInsets.symmetric(vertical: 4),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Text(field.name, style: CatalogTheme.monoStyle(context)),
                          if (field.isKey) ...[
                            const SizedBox(width: 6),
                            const Icon(Icons.vpn_key, size: 14),
                          ],
                        ],
                      ),
                      if (field.description != null) ...[
                        const SizedBox(height: 2),
                        Text(field.description!, style: Theme.of(context).textTheme.bodyMedium),
                      ],
                      if (typeLine != null) ...[
                        const SizedBox(height: 2),
                        Text(typeLine, style: Theme.of(context).textTheme.bodySmall),
                      ],
                      if (field.aggregation != null) ...[
                        const SizedBox(height: 2),
                        Text('Aggregation: ${field.aggregation}',
                            style: Theme.of(context).textTheme.bodySmall),
                      ],
                      if (field.checkTable != null) ...[
                        const SizedBox(height: 2),
                        Text('FK -> ${field.checkTable}',
                            style: Theme.of(context).textTheme.bodySmall),
                      ],
                      if (field.sourceExpression != null) ...[
                        const SizedBox(height: 2),
                        Text('from: ${field.sourceExpression}',
                            style: CatalogTheme.monoStyle(context, fontSize: 12)),
                      ],
                      if (field.formula != null) ...[
                        const SizedBox(height: 4),
                        Text(field.formula!, style: CatalogTheme.monoStyle(context, fontSize: 12)),
                      ],
                      if (field.fixedValues != null && field.fixedValues!.isNotEmpty) ...[
                        const SizedBox(height: 4),
                        Wrap(
                          spacing: 4,
                          runSpacing: 4,
                          children: [
                            for (final fv in field.fixedValues!)
                              Chip(
                                label: Text(
                                    '${(fv as Map)['low'] ?? ''}${fv['text'] != null ? ' = ${fv['text']}' : ''}',
                                    style: const TextStyle(fontSize: 11)),
                                visualDensity: VisualDensity.compact,
                              ),
                          ],
                        ),
                      ],
                      if (field.annotations != null && field.annotations!.isNotEmpty) ...[
                        const SizedBox(height: 4),
                        Text(field.annotations!.join('  '),
                            style: CatalogTheme.monoStyle(context, fontSize: 11)),
                      ],
                    ],
                  ),
                ),
                if (field.role != null) ...[
                  const SizedBox(width: 8),
                  Chip(label: Text(field.role!), visualDensity: VisualDensity.compact),
                ],
              ],
            ),
          ),
        );
      },
    );
  }
}

class _BusinessContextTab extends StatelessWidget {
  final CatalogEntity entity;
  const _BusinessContextTab({required this.entity});

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
          const SizedBox(height: 20),
          FactSheet(entries: [
            MapEntry('Owner', entity.bizOwner),
            MapEntry('Line of Business', entity.bizLob),
            MapEntry('Confidentiality', entity.bizConfidentiality),
            MapEntry('Curated by', entity.bizCuratedBy),
            MapEntry('Curated at', entity.bizCuratedAt),
          ]),
          const SizedBox(height: 20),
          OutlinedButton.icon(
            onPressed: () => context.go('/curate/${entity.id}'),
            icon: const Icon(Icons.edit_note),
            label: Text(entity.isCurated ? 'Edit business context' : 'Curate this entity'),
          ),
        ],
      ),
    );
  }
}
