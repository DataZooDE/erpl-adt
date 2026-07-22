import 'package:erpl_catalog_kit/erpl_catalog_kit.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

/// Standalone example app for erpl_catalog_kit — talks to a local
/// `erpl-adt mcp --http` server. SID is read from --dart-define
/// (SYSTEM_SID). The MCP base URL defaults to same-origin (wherever this
/// page itself was loaded from) rather than a hardcoded host:port — this
/// build is embedded into the erpl-adt binary and served same-origin with
/// the /mcp endpoint (`erpl-adt catalog webui`), and that origin varies:
/// localhost during dev, a LAN/Tailscale address when someone else opens
/// the same server remotely. A hardcoded "http://127.0.0.1:8383" default
/// breaks the moment the page isn't opened via that exact host:port —
/// exactly the DioException a remote Tailscale client hit. --dart-define
/// MCP_BASE_URL still overrides this for pointing at a *different* server
/// than the one serving the page.
void main() {
  const mcpBaseUrlOverride = String.fromEnvironment('MCP_BASE_URL');
  final mcpBaseUrl = mcpBaseUrlOverride.isNotEmpty
      ? mcpBaseUrlOverride
      : '${Uri.base.scheme}://${Uri.base.host}:${Uri.base.port}';
  const systemSid = String.fromEnvironment('SYSTEM_SID', defaultValue: 'A4H');

  runApp(
    ProviderScope(
      overrides: [
        catalogConfigProvider.overrideWithValue(
          CatalogConfig(mcpBaseUrl: mcpBaseUrl, systemSid: systemSid),
        ),
      ],
      child: const CatalogExplorerApp(),
    ),
  );
}

class CatalogExplorerApp extends StatelessWidget {
  const CatalogExplorerApp({super.key});

  @override
  Widget build(BuildContext context) {
    final router = buildCatalogRouter();
    return MaterialApp.router(
      title: 'erpl-adt Catalog',
      theme: CatalogTheme.light(),
      darkTheme: CatalogTheme.dark(),
      routerConfig: router,
    );
  }
}
