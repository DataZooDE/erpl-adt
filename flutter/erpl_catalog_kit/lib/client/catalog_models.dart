/// Data models mirroring the catalog_* MCP tool JSON shapes
/// (see erpl-adt src/mcp/catalog_tool_handlers.cpp).
library;

class CatalogEntity {
  final String id;
  final String domain;
  final String objectType;
  final String technicalName;
  final String displayName;
  final String? packageOrInfoarea;
  final String? bizDefinition;
  final String? bizOwner;
  final String? bizLob;
  final String? bizConfidentiality;
  final List<CatalogFieldRef> fields;

  const CatalogEntity({
    required this.id,
    required this.domain,
    required this.objectType,
    required this.technicalName,
    required this.displayName,
    this.packageOrInfoarea,
    this.bizDefinition,
    this.bizOwner,
    this.bizLob,
    this.bizConfidentiality,
    this.fields = const [],
  });

  factory CatalogEntity.fromJson(Map<String, dynamic> j) {
    return CatalogEntity(
      id: j['id'] as String? ?? '',
      domain: j['domain'] as String? ?? '',
      objectType: j['object_type'] as String? ?? '',
      technicalName: j['technical_name'] as String? ?? '',
      displayName: j['display_name'] as String? ?? '',
      packageOrInfoarea: j['package_or_infoarea'] as String?,
      bizDefinition: j['biz_definition'] as String?,
      bizOwner: j['biz_owner'] as String?,
      bizLob: j['biz_lob'] as String?,
      bizConfidentiality: j['biz_confidentiality'] as String?,
      fields: (j['fields'] as List<dynamic>? ?? const [])
          .map((f) => CatalogFieldRef.fromJson(f as Map<String, dynamic>))
          .toList(),
    );
  }
}

class CatalogFieldRef {
  final String name;
  final String? dataType;
  final String? role;
  final String? formula;

  const CatalogFieldRef({required this.name, this.dataType, this.role, this.formula});

  factory CatalogFieldRef.fromJson(Map<String, dynamic> j) {
    return CatalogFieldRef(
      name: j['name'] as String? ?? '',
      dataType: j['data_type'] as String?,
      role: j['role'] as String?,
      formula: j['formula'] as String?,
    );
  }
}

class CatalogSearchHit {
  final CatalogEntity entity;
  final double score;

  const CatalogSearchHit({required this.entity, required this.score});

  factory CatalogSearchHit.fromJson(Map<String, dynamic> j) {
    return CatalogSearchHit(
      entity: CatalogEntity.fromJson(j),
      score: (j['score'] as num?)?.toDouble() ?? 0.0,
    );
  }
}

class CatalogEdgeRef {
  final String id;
  final String fromId;
  final String toId;
  final String kind;
  final String resolution;

  const CatalogEdgeRef({
    required this.id,
    required this.fromId,
    required this.toId,
    required this.kind,
    required this.resolution,
  });

  factory CatalogEdgeRef.fromJson(Map<String, dynamic> j) {
    return CatalogEdgeRef(
      id: j['id'] as String? ?? '',
      fromId: j['from_id'] as String? ?? '',
      toId: j['to_id'] as String? ?? '',
      kind: j['kind'] as String? ?? '',
      resolution: j['resolution'] as String? ?? '',
    );
  }
}

class CatalogSyncRun {
  final String id;
  final String startedAt;
  final String finishedAt;
  final String mode;
  final String scope;
  final int added;
  final int changed;
  final int removed;
  final String status;

  const CatalogSyncRun({
    required this.id,
    required this.startedAt,
    required this.finishedAt,
    required this.mode,
    required this.scope,
    required this.added,
    required this.changed,
    required this.removed,
    required this.status,
  });

  factory CatalogSyncRun.fromJson(Map<String, dynamic> j) {
    return CatalogSyncRun(
      id: j['id'] as String? ?? '',
      startedAt: j['started_at'] as String? ?? '',
      finishedAt: j['finished_at'] as String? ?? '',
      mode: j['mode'] as String? ?? '',
      scope: j['scope'] as String? ?? '',
      added: (j['added'] as num?)?.toInt() ?? 0,
      changed: (j['changed'] as num?)?.toInt() ?? 0,
      removed: (j['removed'] as num?)?.toInt() ?? 0,
      status: j['status'] as String? ?? '',
    );
  }
}

class CatalogStats {
  final int entityCount;
  final int fieldCount;
  final int edgeCount;
  final int unresolvedEdgeCount;
  final int curatedEntityCount;

  const CatalogStats({
    required this.entityCount,
    required this.fieldCount,
    required this.edgeCount,
    required this.unresolvedEdgeCount,
    required this.curatedEntityCount,
  });

  factory CatalogStats.fromJson(Map<String, dynamic> j) {
    return CatalogStats(
      entityCount: (j['entity_count'] as num?)?.toInt() ?? 0,
      fieldCount: (j['field_count'] as num?)?.toInt() ?? 0,
      edgeCount: (j['edge_count'] as num?)?.toInt() ?? 0,
      unresolvedEdgeCount: (j['unresolved_edge_count'] as num?)?.toInt() ?? 0,
      curatedEntityCount: (j['curated_entity_count'] as num?)?.toInt() ?? 0,
    );
  }
}

class DriverTreeField {
  final String name;
  final String formula;

  const DriverTreeField({required this.name, required this.formula});

  factory DriverTreeField.fromJson(Map<String, dynamic> j) {
    return DriverTreeField(
      name: j['name'] as String? ?? '',
      formula: j['formula'] as String? ?? '',
    );
  }
}
