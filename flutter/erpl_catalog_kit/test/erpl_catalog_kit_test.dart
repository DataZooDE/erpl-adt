import 'package:flutter_test/flutter_test.dart';

import 'package:erpl_catalog_kit/erpl_catalog_kit.dart';

void main() {
  group('CatalogEntity.fromJson', () {
    test('parses the shape catalog_get returns', () {
      final entity = CatalogEntity.fromJson({
        'id': 'abc123',
        'domain': 'DDIC',
        'object_type': 'TABL',
        'technical_name': 'SFLIGHT',
        'display_name': 'Flight schedule',
        'package_or_infoarea': 'STEST',
        'biz_definition': 'Flight schedule master data',
        'fields': [
          {'name': 'CARRID', 'data_type': 'S_CARR_ID'},
        ],
      });

      expect(entity.id, 'abc123');
      expect(entity.domain, 'DDIC');
      expect(entity.technicalName, 'SFLIGHT');
      expect(entity.bizDefinition, 'Flight schedule master data');
      expect(entity.fields, hasLength(1));
      expect(entity.fields.first.name, 'CARRID');
    });

    test('tolerates missing optional fields', () {
      final entity = CatalogEntity.fromJson({
        'id': 'abc123',
        'domain': 'ABAP',
        'object_type': 'CLAS',
        'technical_name': 'ZCL_EXAMPLE',
        'display_name': 'Example class',
      });

      expect(entity.bizDefinition, isNull);
      expect(entity.fields, isEmpty);
      expect(entity.objectSubtype, isNull);
    });

    test('parses object_subtype when present (BW query vs. variable)', () {
      final entity = CatalogEntity.fromJson({
        'id': 'abc123',
        'domain': 'BW',
        'object_type': 'ELEM',
        'object_subtype': 'REP',
        'technical_name': '0BPC_BPF_ACTIVITY_REP',
        'display_name': 'BPC BPF Activity Report',
      });

      expect(entity.objectSubtype, 'REP');
    });
  });

  group('CatalogSearchHit.fromJson', () {
    test('parses score alongside the entity', () {
      final hit = CatalogSearchHit.fromJson({
        'id': 'abc123',
        'domain': 'DDIC',
        'object_type': 'TABL',
        'technical_name': 'SFLIGHT',
        'display_name': 'Flight schedule',
        'score': 0.87,
      });

      expect(hit.score, 0.87);
      expect(hit.entity.technicalName, 'SFLIGHT');
    });
  });
}
