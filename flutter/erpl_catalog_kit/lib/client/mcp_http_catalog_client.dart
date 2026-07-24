import 'dart:convert';
import 'dart:math';

import 'package:dio/dio.dart';

import 'catalog_client.dart';
import 'catalog_models.dart';

/// Hosted/mobile-mode CatalogClient: talks to erpl-adt's MCP-over-HTTP
/// transport (`erpl-adt mcp --http`) — same JSON-RPC 2.0 tool contract the
/// stdio transport and the CLI's `catalog search/annotate` share
/// (BRD.md FR-MCP-2: identical tool contract on both transports).
class McpHttpCatalogClient implements CatalogClient {
  final Dio _dio;
  final Random _idGen = Random();

  McpHttpCatalogClient({required String baseUrl, Dio? dio})
      : _dio = dio ??
            Dio(BaseOptions(
              baseUrl: baseUrl,
              contentType: 'application/json',
              connectTimeout: const Duration(seconds: 10),
              receiveTimeout: const Duration(seconds: 30),
            ));

  Future<Map<String, dynamic>> _callTool(String name, Map<String, dynamic> arguments) async {
    final id = _idGen.nextInt(1 << 31);
    final response = await _dio.post<Map<String, dynamic>>(
      '/mcp',
      data: jsonEncode({
        'jsonrpc': '2.0',
        'id': id,
        'method': 'tools/call',
        'params': {'name': name, 'arguments': arguments},
      }),
    );

    final body = response.data;
    if (body == null) {
      throw const CatalogClientException('Empty response from MCP server');
    }
    if (body.containsKey('error')) {
      final error = body['error'] as Map<String, dynamic>;
      throw CatalogClientException(error['message'] as String? ?? 'Unknown MCP error');
    }

    final result = body['result'] as Map<String, dynamic>?;
    final content = result?['content'] as List<dynamic>?;
    if (content == null || content.isEmpty) {
      throw const CatalogClientException('MCP tool returned no content');
    }
    final text = content.first['text'] as String? ?? '{}';
    final decoded = jsonDecode(text) as Map<String, dynamic>;

    if (result?['isError'] == true) {
      throw CatalogClientException(text);
    }
    return decoded;
  }

  @override
  Future<CatalogSearchPage> search(
    String query, {
    String mode = 'fts',
    int maxResults = 20,
    int cursor = 0,
    String? domain,
    String? objectType,
    String? objectSubtype,
    bool curatedOnly = false,
  }) async {
    final j = await _callTool('catalog_search', {
      'query': query,
      'mode': mode,
      'max_results': maxResults,
      'cursor': cursor,
      if (domain != null) 'domain': domain,
      if (objectType != null) 'object_type': objectType,
      if (objectSubtype != null) 'subtype': objectSubtype,
      if (curatedOnly) 'curated_only': curatedOnly,
    });
    return CatalogSearchPage.fromJson(j);
  }

  @override
  Future<CatalogEntity?> getEntity(String id) async {
    try {
      final j = await _callTool('catalog_get', {'id': id});
      return CatalogEntity.fromJson(j);
    } on CatalogClientException {
      return null; // unknown id — catalog_get returns a tool error, not a 404
    }
  }

  @override
  Future<List<CatalogEdgeRef>> whereUsed(String id, {int maxResults = 50}) async {
    final j = await _callTool('catalog_where_used', {'id': id, 'max_results': maxResults});
    final edges = j['edges'] as List<dynamic>? ?? const [];
    return edges.map((e) => CatalogEdgeRef.fromJson(e as Map<String, dynamic>)).toList();
  }

  @override
  Future<List<CatalogEdgeRef>> lineage(String id, {int maxDepth = 5}) async {
    final j = await _callTool('catalog_lineage', {'id': id, 'max_depth': maxDepth});
    final chain = j['chain'] as List<dynamic>? ?? const [];
    return chain.map((e) => CatalogEdgeRef.fromJson(e as Map<String, dynamic>)).toList();
  }

  @override
  Future<List<DriverTreeField>> driverTree(String id) async {
    final j = await _callTool('catalog_driver_tree', {'id': id});
    final fields = j['fields'] as List<dynamic>? ?? const [];
    return fields.map((f) => DriverTreeField.fromJson(f as Map<String, dynamic>)).toList();
  }

  @override
  Future<List<CatalogSyncRun>> syncStatus({int maxResults = 10}) async {
    final j = await _callTool('catalog_sync_status', {'max_results': maxResults});
    final runs = j['runs'] as List<dynamic>? ?? const [];
    return runs.map((r) => CatalogSyncRun.fromJson(r as Map<String, dynamic>)).toList();
  }

  @override
  Future<CatalogStats> stats() async {
    final j = await _callTool('catalog_stats', {});
    return CatalogStats.fromJson(j);
  }

  @override
  Future<List<CatalogObjectTypeCount>> objectTypes({String? query}) async {
    final j = await _callTool('catalog_object_types', {
      if (query != null && query.isNotEmpty) 'query': query,
    });
    final types = j['types'] as List<dynamic>? ?? const [];
    return types
        .map((t) => CatalogObjectTypeCount.fromJson(t as Map<String, dynamic>))
        .toList();
  }

  @override
  Future<List<CatalogObjectSubtypeCount>> objectSubtypes({String? query}) async {
    final j = await _callTool('catalog_object_subtypes', {
      if (query != null && query.isNotEmpty) 'query': query,
    });
    final subtypes = j['subtypes'] as List<dynamic>? ?? const [];
    return subtypes
        .map((t) => CatalogObjectSubtypeCount.fromJson(t as Map<String, dynamic>))
        .toList();
  }

  @override
  Future<void> annotate(
    String id, {
    String? definition,
    String? owner,
    String? lob,
    String? confidentiality,
  }) async {
    await _callTool('catalog_annotate', {
      'id': id,
      if (definition != null) 'definition': definition,
      if (owner != null) 'owner': owner,
      if (lob != null) 'lob': lob,
      if (confidentiality != null) 'confidentiality': confidentiality,
    });
  }
}
