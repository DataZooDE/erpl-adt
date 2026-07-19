import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../state/catalog_providers.dart';
import '../theme/catalog_theme.dart';

/// S6 Curate — writes business-overlay fields via catalog_annotate. The
/// only screen that writes anything; every other screen is read-only
/// against the cache (matches FR-MCP-5: the fast tool group never writes
/// except catalog_annotate, which goes through this same validated path).
class CurateScreen extends ConsumerStatefulWidget {
  final String entityId;
  const CurateScreen({super.key, required this.entityId});

  @override
  ConsumerState<CurateScreen> createState() => _CurateScreenState();
}

class _CurateScreenState extends ConsumerState<CurateScreen> {
  final _definitionController = TextEditingController();
  final _ownerController = TextEditingController();
  final _lobController = TextEditingController();
  String? _confidentiality;
  bool _saving = false;
  String? _error;

  @override
  void dispose() {
    _definitionController.dispose();
    _ownerController.dispose();
    _lobController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final entityAsync = ref.watch(entityDetailProvider(widget.entityId));

    return Scaffold(
      appBar: AppBar(title: const Text('Curate')),
      body: entityAsync.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (err, _) => Center(child: Text('Failed to load entity: $err')),
        data: (entity) {
          if (entity == null) return const Center(child: Text('Entity not found.'));
          if (_definitionController.text.isEmpty && entity.bizDefinition != null) {
            _definitionController.text = entity.bizDefinition!;
          }
          if (_ownerController.text.isEmpty && entity.bizOwner != null) {
            _ownerController.text = entity.bizOwner!;
          }
          if (_lobController.text.isEmpty && entity.bizLob != null) {
            _lobController.text = entity.bizLob!;
          }
          _confidentiality ??= entity.bizConfidentiality;

          return SingleChildScrollView(
            padding: const EdgeInsets.all(24),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 640),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(entity.technicalName, style: CatalogTheme.monoStyle(context, fontSize: 16)),
                  Text(entity.displayName, style: Theme.of(context).textTheme.titleMedium),
                  const SizedBox(height: 24),
                  TextField(
                    controller: _definitionController,
                    maxLines: 4,
                    decoration: const InputDecoration(labelText: 'Business definition', border: OutlineInputBorder()),
                  ),
                  const SizedBox(height: 16),
                  TextField(
                    controller: _ownerController,
                    decoration: const InputDecoration(labelText: 'Owner / contact', border: OutlineInputBorder()),
                  ),
                  const SizedBox(height: 16),
                  TextField(
                    controller: _lobController,
                    decoration: const InputDecoration(labelText: 'Line of Business', border: OutlineInputBorder()),
                  ),
                  const SizedBox(height: 16),
                  DropdownButtonFormField<String?>(
                    initialValue: _confidentiality,
                    decoration: const InputDecoration(labelText: 'Confidentiality', border: OutlineInputBorder()),
                    items: const [
                      DropdownMenuItem(value: null, child: Text('(unset)')),
                      DropdownMenuItem(value: 'Public', child: Text('Public')),
                      DropdownMenuItem(value: 'Internal', child: Text('Internal')),
                      DropdownMenuItem(value: 'Confidential', child: Text('Confidential')),
                    ],
                    onChanged: (v) => setState(() => _confidentiality = v),
                  ),
                  if (_error != null) ...[
                    const SizedBox(height: 12),
                    Text(_error!, style: TextStyle(color: Theme.of(context).colorScheme.error)),
                  ],
                  const SizedBox(height: 24),
                  Row(
                    children: [
                      FilledButton.icon(
                        onPressed: _saving ? null : () => _save(context),
                        icon: _saving
                            ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                            : const Icon(Icons.save),
                        label: const Text('Save'),
                      ),
                      const SizedBox(width: 12),
                      OutlinedButton(onPressed: () => context.pop(), child: const Text('Cancel')),
                    ],
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }

  Future<void> _save(BuildContext context) async {
    setState(() {
      _saving = true;
      _error = null;
    });
    try {
      final client = ref.read(catalogClientProvider);
      await client.annotate(
        widget.entityId,
        definition: _definitionController.text.trim().isEmpty ? null : _definitionController.text.trim(),
        owner: _ownerController.text.trim().isEmpty ? null : _ownerController.text.trim(),
        lob: _lobController.text.trim().isEmpty ? null : _lobController.text.trim(),
        confidentiality: _confidentiality,
      );
      ref.invalidate(entityDetailProvider(widget.entityId));
    } catch (e) {
      setState(() => _error = 'Save failed: $e');
      return;
    } finally {
      if (mounted) setState(() => _saving = false);
    }
    // Navigation happens only after the write is confirmed to have
    // succeeded, and only pops if there's actually somewhere to pop back to
    // — this screen may have been reached via a direct deep link (no
    // navigator history), in which case go_router's context.pop() throws
    // ("There is nothing to pop"). That must never be reported as a save
    // failure: the write already committed by this point.
    if (context.mounted && context.canPop()) {
      context.pop();
    } else if (context.mounted) {
      context.go('/entity/${widget.entityId}');
    }
  }
}
