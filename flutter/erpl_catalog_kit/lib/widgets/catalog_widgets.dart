import 'package:flutter/material.dart';

import '../theme/catalog_theme.dart';
import '../theme/catalog_tokens.dart';

/// A small colored pill showing an entity's domain + object type, e.g.
/// "BW · IOBJ" — used in search results, entity chips, and lineage lists.
class TypeIcon extends StatelessWidget {
  final String domain;
  final String objectType;
  const TypeIcon({super.key, required this.domain, required this.objectType});

  @override
  Widget build(BuildContext context) {
    final color = domainColor(domain, context);
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
  final String? packageOrInfoarea;
  final bool? isCurated;
  final VoidCallback? onTap;

  const EntityChip({
    super.key,
    required this.id,
    required this.domain,
    required this.objectType,
    required this.technicalName,
    required this.displayName,
    this.packageOrInfoarea,
    this.isCurated,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final subtitleParts = [
      technicalName,
      if (packageOrInfoarea != null && packageOrInfoarea!.isNotEmpty) packageOrInfoarea!,
    ];
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
        subtitle: Text(subtitleParts.join(' · '), style: CatalogTheme.monoStyle(context)),
        trailing: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (isCurated != null)
              Tooltip(
                message: isCurated! ? 'Curated' : 'Not yet curated',
                child: Icon(
                  isCurated! ? Icons.check_circle : Icons.circle_outlined,
                  size: 14,
                  color: isCurated!
                      ? Theme.of(context).extension<CatalogTokens>()?.success ??
                          Theme.of(context).colorScheme.secondary
                      : Theme.of(context).colorScheme.outline,
                ),
              ),
            const SizedBox(width: 8),
            const Icon(Icons.chevron_right),
          ],
        ),
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
    final color = confidentialityColor(level, context);
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

/// Discovery's unified search box (S1) — one field drives both "search" and
/// "browse": an empty query browses everything, typing narrows it. No
/// fts/vss/hybrid picker — the backend only serves 'fts' today (vss/hybrid
/// reject with an error), so offering a selector that mostly errors would
/// over-promise; the "smart match" chip is a static indicator of *how*
/// results are ranked, not a mode switch.
class SearchOmnibox extends StatefulWidget {
  final String initialQuery;
  final ValueChanged<String> onChanged;

  const SearchOmnibox({super.key, required this.initialQuery, required this.onChanged});

  @override
  State<SearchOmnibox> createState() => _SearchOmniboxState();
}

class _SearchOmniboxState extends State<SearchOmnibox> {
  late final TextEditingController _controller =
      TextEditingController(text: widget.initialQuery);

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Expanded(
          child: TextField(
            controller: _controller,
            autofocus: true,
            decoration: InputDecoration(
              hintText: 'Search or browse the catalog — e.g. "procurement value"',
              prefixIcon: const Icon(Icons.search),
              border: const OutlineInputBorder(),
              suffixIcon: _controller.text.isEmpty
                  ? null
                  : IconButton(
                      icon: const Icon(Icons.close),
                      tooltip: 'Clear',
                      onPressed: () {
                        _controller.clear();
                        widget.onChanged('');
                        setState(() {});
                      },
                    ),
            ),
            onChanged: (v) {
              widget.onChanged(v);
              setState(() {}); // toggles the clear button
            },
            onSubmitted: widget.onChanged,
          ),
        ),
        const SizedBox(width: 12),
        Tooltip(
          message: 'Meaning-aware full-text match',
          child: Chip(
            avatar: const Icon(Icons.auto_awesome, size: 14),
            label: const Text('Smart match', style: TextStyle(fontSize: 11)),
            visualDensity: VisualDensity.compact,
          ),
        ),
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
