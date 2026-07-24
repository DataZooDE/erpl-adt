import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';

const _kFormats = [
  (
    key: 'json',
    title: 'Catalog Feed v1 (JSON)',
    description: 'The full entity/field/edge contract — deterministic, versioned.',
  ),
  (
    key: 'openmetadata',
    title: 'OpenMetadata',
    description: 'DDIC/CDS entities as OpenMetadata tables, ready to ingest into a third-party catalog.',
  ),
  (
    key: 'mermaid',
    title: 'Mermaid',
    description: 'A flowchart of entities connected by edges — for docs/diffs, not a replacement '
        'for the interactive lineage view.',
  ),
];

/// S8 Feed Export — a scope → format → run-it stepper. Export always builds
/// a fresh, live feed from SAP (there is no catalog_export MCP fast-tool —
/// it isn't a cache read like every other screen), so this stays a
/// generated-command surface rather than an in-app trigger, per the
/// functional spec's open question on export.
class FeedExportScreen extends ConsumerStatefulWidget {
  const FeedExportScreen({super.key});

  @override
  ConsumerState<FeedExportScreen> createState() => _FeedExportScreenState();
}

class _FeedExportScreenState extends ConsumerState<FeedExportScreen> {
  late final TextEditingController _scopeController;
  String _format = 'json';

  @override
  void initState() {
    super.initState();
    _scopeController = TextEditingController(text: ref.read(exportScopeProvider) ?? '');
  }

  @override
  void dispose() {
    _scopeController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final config = ref.watch(catalogConfigProvider);
    final scope = _scopeController.text.trim();
    final command = scope.isEmpty
        ? null
        : 'erpl-adt catalog build --sid ${config.systemSid} --package $scope --format $_format';

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Export the catalog', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 4),
        Text(
          'Run the generated command from a machine that can reach SAP — export always builds a '
          'live feed, it is not a cache read.',
          style: Theme.of(context).textTheme.bodySmall,
        ),
        const SizedBox(height: 20),
        _StepHeader(number: 1, title: 'Scope'),
        const SizedBox(height: 8),
        TextField(
          controller: _scopeController,
          decoration: const InputDecoration(
            labelText: 'Package or InfoArea',
            hintText: 'e.g. ZLOCAL, or an InfoArea technical name',
            border: OutlineInputBorder(),
          ),
          onChanged: (_) => setState(() {}),
        ),
        const SizedBox(height: 24),
        _StepHeader(number: 2, title: 'Format'),
        const SizedBox(height: 8),
        for (final f in _kFormats)
          RadioListTile<String>(
            value: f.key,
            groupValue: _format,
            onChanged: (v) => setState(() => _format = v!),
            title: Text(f.title),
            subtitle: Text(f.description),
            dense: true,
          ),
        const SizedBox(height: 24),
        _StepHeader(number: 3, title: 'Run it'),
        const SizedBox(height: 8),
        if (command == null)
          Text('Enter a scope above to generate the command.',
              style: Theme.of(context).textTheme.bodySmall)
        else
          Container(
            width: double.infinity,
            padding: const EdgeInsets.all(10),
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.surfaceContainerHighest,
              borderRadius: BorderRadius.circular(6),
            ),
            child: SelectableText(command, style: CatalogTheme.monoStyle(context)),
          ),
        const SizedBox(height: 32),
        Text('Recent export runs', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 4),
        Text(
          'Not tracked yet — export runs happen outside the explorer, on whatever machine has '
          'SAP connectivity, so there\'s no cache-backed history to show here.',
          style: Theme.of(context).textTheme.bodySmall,
        ),
      ],
    );
  }
}

class _StepHeader extends StatelessWidget {
  final int number;
  final String title;
  const _StepHeader({required this.number, required this.title});

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Row(
      children: [
        CircleAvatar(
          radius: 12,
          backgroundColor: scheme.primaryContainer,
          child: Text('$number',
              style: TextStyle(color: scheme.onPrimaryContainer, fontSize: 12, fontWeight: FontWeight.w600)),
        ),
        const SizedBox(width: 8),
        Text(title, style: Theme.of(context).textTheme.titleSmall),
      ],
    );
  }
}
