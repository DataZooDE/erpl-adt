import 'package:flutter/material.dart';

import '../theme/catalog_theme.dart';

/// A small colored pill showing an entity's domain + object type, e.g.
/// "BW · IOBJ" — used in search results, entity chips, and lineage lists.
class TypeIcon extends StatelessWidget {
  final String domain;
  final String objectType;
  const TypeIcon({super.key, required this.domain, required this.objectType});

  @override
  Widget build(BuildContext context) {
    final color = domainColor(domain, Theme.of(context).colorScheme);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: color.withValues(alpha: 0.4)),
      ),
      child: Text(
        '$domain · $objectType',
        style: CatalogTheme.monoStyle(context, fontSize: 11).copyWith(color: color),
      ),
    );
  }
}

/// A compact, tappable summary of a catalog entity — used in search
/// results, browse tables, and where-used/lineage lists.
class EntityChip extends StatelessWidget {
  final String id;
  final String domain;
  final String objectType;
  final String technicalName;
  final String displayName;
  final VoidCallback? onTap;

  const EntityChip({
    super.key,
    required this.id,
    required this.domain,
    required this.objectType,
    required this.technicalName,
    required this.displayName,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.symmetric(vertical: 4),
      child: ListTile(
        onTap: onTap,
        leading: TypeIcon(domain: domain, objectType: objectType),
        title: Text(
          displayName.isEmpty ? technicalName : displayName,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
        ),
        subtitle: Text(technicalName, style: CatalogTheme.monoStyle(context)),
        trailing: const Icon(Icons.chevron_right),
      ),
    );
  }
}

/// Shows the curated confidentiality level (Public/Internal/Confidential),
/// or nothing if uncurated.
class ConfidentialityBadge extends StatelessWidget {
  final String? level;
  const ConfidentialityBadge({super.key, this.level});

  @override
  Widget build(BuildContext context) {
    if (level == null || level!.isEmpty) return const SizedBox.shrink();
    final color = confidentialityColor(level, Theme.of(context).colorScheme);
    return Chip(
      label: Text(level!, style: TextStyle(color: color, fontSize: 11)),
      backgroundColor: color.withValues(alpha: 0.1),
      side: BorderSide(color: color.withValues(alpha: 0.4)),
      visualDensity: VisualDensity.compact,
    );
  }
}

/// Shows whether an entity/edge is curated or system-extracted only —
/// the "ProvenanceBadge" from the ux-spec's widget inventory.
class ProvenanceBadge extends StatelessWidget {
  final bool curated;
  const ProvenanceBadge({super.key, required this.curated});

  @override
  Widget build(BuildContext context) {
    return Chip(
      label: Text(curated ? 'Curated' : 'System-extracted', style: const TextStyle(fontSize: 11)),
      avatar: Icon(curated ? Icons.verified : Icons.auto_awesome, size: 14),
      visualDensity: VisualDensity.compact,
    );
  }
}

/// Search omnibox (S1) — "/" focuses it in a real desktop build; kept
/// simple here (a TextField + mode selector) since keyboard-shortcut
/// wiring is a polish item, not core functionality.
class SearchOmnibox extends StatelessWidget {
  final String initialQuery;
  final String mode;
  final ValueChanged<String> onSubmitted;
  final ValueChanged<String> onModeChanged;

  const SearchOmnibox({
    super.key,
    required this.initialQuery,
    required this.mode,
    required this.onSubmitted,
    required this.onModeChanged,
  });

  @override
  Widget build(BuildContext context) {
    final controller = TextEditingController(text: initialQuery);
    return Row(
      children: [
        Expanded(
          child: TextField(
            controller: controller,
            autofocus: true,
            decoration: const InputDecoration(
              hintText: 'Search the catalog — e.g. "procurement value"',
              prefixIcon: Icon(Icons.search),
              border: OutlineInputBorder(),
            ),
            onSubmitted: onSubmitted,
          ),
        ),
        const SizedBox(width: 12),
        DropdownButton<String>(
          value: mode,
          items: const [
            DropdownMenuItem(value: 'fts', child: Text('Full-text')),
            DropdownMenuItem(value: 'vss', child: Text('Semantic')),
            DropdownMenuItem(value: 'hybrid', child: Text('Hybrid')),
          ],
          onChanged: (v) => v != null ? onModeChanged(v) : null,
        ),
        const SizedBox(width: 8),
        FilledButton(onPressed: () => onSubmitted(controller.text), child: const Text('Search')),
      ],
    );
  }
}

/// A labeled key/value row used throughout the entity detail "fact sheet".
class FactSheet extends StatelessWidget {
  final List<MapEntry<String, String?>> entries;
  const FactSheet({super.key, required this.entries});

  @override
  Widget build(BuildContext context) {
    final visible = entries.where((e) => e.value != null && e.value!.isNotEmpty).toList();
    if (visible.isEmpty) {
      return const Padding(
        padding: EdgeInsets.all(12),
        child: Text('No details available.'),
      );
    }
    return Table(
      columnWidths: const {0: IntrinsicColumnWidth(), 1: FlexColumnWidth()},
      children: [
        for (final entry in visible)
          TableRow(children: [
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 6, horizontal: 8),
              child: Text(entry.key, style: const TextStyle(fontWeight: FontWeight.w600)),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 6, horizontal: 8),
              child: SelectableText(entry.value!),
            ),
          ]),
      ],
    );
  }
}
