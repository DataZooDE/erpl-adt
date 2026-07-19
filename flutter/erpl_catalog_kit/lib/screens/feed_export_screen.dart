import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';

/// S8 Feed Export — format cards for the three export shapes erpl-adt
/// produces (`catalog build --format ...`). Export generation happens
/// server-side via the CLI/erpl-adt binary (there is no catalog_export
/// MCP-fast-tool — building the feed is a live-SAP operation, not a cache
/// read), so this screen surfaces the exact command to run rather than
/// re-implementing export generation client-side.
class FeedExportScreen extends ConsumerWidget {
  const FeedExportScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final config = ref.watch(catalogConfigProvider);

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Export the catalog', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 4),
        Text(
          'Run one of these from the machine that can reach SAP — export always builds a live feed, '
          'it is not a cache read.',
          style: Theme.of(context).textTheme.bodySmall,
        ),
        const SizedBox(height: 16),
        _FormatCard(
          title: 'Catalog Feed v1 (JSON)',
          description: 'The full entity/field/edge contract — deterministic, versioned.',
          command:
              'erpl-adt catalog build --sid ${config.systemSid} --package <pkg> --format json',
        ),
        _FormatCard(
          title: 'OpenMetadata',
          description: 'DDIC/CDS entities as OpenMetadata tables, ready to ingest into a third-party catalog.',
          command:
              'erpl-adt catalog build --sid ${config.systemSid} --package <pkg> --format openmetadata',
        ),
        _FormatCard(
          title: 'Mermaid',
          description: 'A flowchart of entities connected by edges — for docs/diffs, not a replacement '
              'for the interactive lineage view.',
          command:
              'erpl-adt catalog build --sid ${config.systemSid} --package <pkg> --format mermaid',
        ),
      ],
    );
  }
}

class _FormatCard extends StatelessWidget {
  final String title;
  final String description;
  final String command;
  const _FormatCard({required this.title, required this.description, required this.command});

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(title, style: Theme.of(context).textTheme.titleSmall),
            const SizedBox(height: 4),
            Text(description),
            const SizedBox(height: 8),
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: Theme.of(context).colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(6),
              ),
              child: SelectableText(command, style: CatalogTheme.monoStyle(context)),
            ),
          ],
        ),
      ),
    );
  }
}
