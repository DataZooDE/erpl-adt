import 'package:erpl_catalog_kit/erpl_catalog_kit.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

/// Standalone example app for erpl_catalog_kit — talks to a local
/// `erpl-adt mcp --http` server. Base URL/SID are read from --dart-define
/// (MCP_BASE_URL, SYSTEM_SID) so this can point at any running server
/// without a rebuild.
void main() {
  const mcpBaseUrl = String.fromEnvironment('MCP_BASE_URL', defaultValue: 'http://127.0.0.1:8383');
  const systemSid = String.fromEnvironment('SYSTEM_SID', defaultValue: 'A4H');

  runApp(
    ProviderScope(
      overrides: [
        catalogConfigProvider.overrideWithValue(
          const CatalogConfig(mcpBaseUrl: mcpBaseUrl, systemSid: systemSid),
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
