import 'package:erpl_catalog_kit/erpl_catalog_kit.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:go_router/go_router.dart';

// Every Explorer view must have a stable, copy-pastable URL: in-app navigation
// uses context.go (which reflects in the address bar) rather than context.push
// (which does not), and the relationship lens is serialized into the URL. These
// tests exercise routing/URL behaviour only, so the catalog client is stubbed
// with one that returns empty results — no HTTP, no pending timers at teardown.
class _EmptyCatalogClient implements CatalogClient {
  @override
  Future<List<CatalogEdgeRef>> whereUsed(String id, {int maxResults = 50}) async => [];
  @override
  Future<List<CatalogEdgeRef>> lineage(String id, {int maxDepth = 5}) async => [];
  @override
  Future<List<DriverTreeField>> driverTree(String id) async => [];
  @override
  Future<CatalogEntity?> getEntity(String id) async => null;
  @override
  dynamic noSuchMethod(Invocation invocation) =>
      throw UnimplementedError('${invocation.memberName} not stubbed');
}

Widget _app(GoRouter router) => ProviderScope(
      overrides: [
        catalogConfigProvider.overrideWithValue(
          const CatalogConfig(mcpBaseUrl: 'http://127.0.0.1:1', systemSid: 'A4H'),
        ),
        catalogClientProvider.overrideWithValue(_EmptyCatalogClient()),
      ],
      child: MaterialApp.router(routerConfig: router),
    );

String _location(GoRouter router) =>
    router.routeInformationProvider.value.uri.toString();

void main() {
  testWidgets('a relationship deep link resolves to the requested lens view',
      (tester) async {
    final router =
        buildCatalogRouter(initialLocation: '/entity/E1/relate?lens=lineage');
    await tester.pumpWidget(_app(router));
    await tester.pump();

    // The RelationshipScreen rendered from the deep link (its three-lens
    // switcher is present).
    expect(find.text('Where-used'), findsOneWidget);
    expect(find.text('Lineage'), findsOneWidget);
    expect(find.text('Driver tree'), findsOneWidget);
    // The lineage lens' "Downstream" affordance only shows when lineage is the
    // active lens — confirms the ?lens=lineage query was honoured, not ignored.
    expect(find.text('Downstream'), findsOneWidget);
  });

  testWidgets('switching the relationship lens serializes it into the URL',
      (tester) async {
    final router =
        buildCatalogRouter(initialLocation: '/entity/E1/relate?lens=whereUsed');
    await tester.pumpWidget(_app(router));
    await tester.pump();

    expect(_location(router), '/entity/E1/relate?lens=whereUsed');

    await tester.tap(find.text('Lineage'));
    await tester.pump();

    // The fix: the active lens is written to the URL, so lineage / driver-tree
    // views are each their own copy-pastable link.
    expect(_location(router), '/entity/E1/relate?lens=lineage');

    await tester.tap(find.text('Driver tree'));
    await tester.pump();
    expect(_location(router), '/entity/E1/relate?lens=driverTree');
  });

  testWidgets('in-app navigation updates the address-bar URL (go, not push)',
      (tester) async {
    final router =
        buildCatalogRouter(initialLocation: '/entity/E1/relate?lens=whereUsed');
    await tester.pumpWidget(_app(router));
    await tester.pump();

    // The "Entity detail" action navigates to the entity's own URL. With
    // context.push this route change would not reflect in the URL; with
    // context.go it does.
    await tester.tap(find.text('Entity detail'));
    await tester.pump();

    expect(_location(router), '/entity/E1');
  });
}
