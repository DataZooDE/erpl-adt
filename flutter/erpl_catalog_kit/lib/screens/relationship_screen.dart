import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../widgets/relationship_lens_view.dart';

/// Relationship exploration — one screen, one route
/// (`/entity/:id/relate`), a 3-way lens switcher (Where-used / Lineage /
/// Driver tree) over the same underlying edge data instead of three
/// separate screens (the old LineageScreen/DriverTreeScreen full routes
/// plus inline Where-Used-only tab). Replaces the wireframe's Screen 3.
class RelationshipScreen extends StatefulWidget {
  final String entityId;
  final RelationshipLens initialLens;
  const RelationshipScreen({
    super.key,
    required this.entityId,
    this.initialLens = RelationshipLens.whereUsed,
  });

  @override
  State<RelationshipScreen> createState() => _RelationshipScreenState();
}

class _RelationshipScreenState extends State<RelationshipScreen> {
  late RelationshipLens _lens = widget.initialLens;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('Relationships — ${widget.entityId}'),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: Center(
              child: TextButton.icon(
                onPressed: () => context.push('/entity/${widget.entityId}'),
                icon: const Icon(Icons.info_outline, size: 16),
                label: const Text('Entity detail'),
              ),
            ),
          ),
        ],
      ),
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.all(16),
            child: SegmentedButton<RelationshipLens>(
              segments: [
                for (final lens in RelationshipLens.values)
                  ButtonSegment(value: lens, label: Text(lens.label)),
              ],
              selected: {_lens},
              onSelectionChanged: (s) => setState(() => _lens = s.first),
            ),
          ),
          if (_lens == RelationshipLens.lineage)
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 16),
              child: Row(
                children: [
                  Icon(Icons.arrow_downward, size: 14),
                  SizedBox(width: 4),
                  Text('Downstream', style: TextStyle(fontSize: 12)),
                  SizedBox(width: 8),
                  Tooltip(
                    message: 'Upstream lineage isn\'t available yet — catalog_lineage only '
                        'walks outgoing edges today.',
                    child: Icon(Icons.help_outline, size: 14),
                  ),
                ],
              ),
            ),
          const SizedBox(height: 8),
          Expanded(
            child: RelationshipLensView(entityId: widget.entityId, lens: _lens),
          ),
        ],
      ),
    );
  }
}
