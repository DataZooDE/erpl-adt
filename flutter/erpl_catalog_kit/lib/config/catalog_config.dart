/// Runtime configuration for erpl_catalog_kit — resolved once at app
/// startup (e.g. from --dart-define or a config screen) and provided via
/// [catalogConfigProvider] in state/providers.dart.
class CatalogConfig {
  /// Base URL of the erpl-adt MCP HTTP transport, e.g.
  /// "http://127.0.0.1:8383" (matches `erpl-adt mcp --http --mcp-port 8383`).
  final String mcpBaseUrl;

  /// System SID this catalog instance is scoped to (e.g. "A4H") — shown in
  /// the shell's top bar so users always know which SAP system they're
  /// looking at.
  final String systemSid;

  const CatalogConfig({required this.mcpBaseUrl, required this.systemSid});
}
