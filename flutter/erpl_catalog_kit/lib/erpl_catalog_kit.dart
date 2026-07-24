/// erpl_catalog_kit — Flutter catalog explorer for erpl-adt.
///
/// Search, browse, inspect, curate, and monitor the sync status of an
/// erpl-adt catalog cache, talking to the erpl-adt MCP-over-HTTP transport
/// (`erpl-adt mcp --http`). Mirrors escurel_explorer_kit's package shape:
/// a Riverpod/go_router/dio Flutter package, embeddable in a host app or
/// runnable standalone (see example/).
library;

export 'client/catalog_client.dart';
export 'client/catalog_models.dart';
export 'client/mcp_http_catalog_client.dart';
export 'config/catalog_config.dart';
export 'shell/app_shell.dart';
export 'state/catalog_providers.dart';
export 'theme/catalog_theme.dart';
