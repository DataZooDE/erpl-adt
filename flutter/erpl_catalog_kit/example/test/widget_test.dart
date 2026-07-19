import 'package:erpl_catalog_kit/erpl_catalog_kit.dart';
import 'package:erpl_catalog_kit_example/main.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('app boots to the Search screen', (WidgetTester tester) async {
    await tester.pumpWidget(
      ProviderScope(
        overrides: [
          catalogConfigProvider.overrideWithValue(
            const CatalogConfig(mcpBaseUrl: 'http://127.0.0.1:8383', systemSid: 'A4H'),
          ),
        ],
        child: const CatalogExplorerApp(),
      ),
    );
    await tester.pump();

    expect(find.text('erpl-adt Catalog'), findsOneWidget);
    expect(find.text('Search the catalog to get started.'), findsOneWidget);
  });
}
