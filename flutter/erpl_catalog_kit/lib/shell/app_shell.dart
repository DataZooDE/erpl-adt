import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../inspector/entity_detail_view.dart';
import '../screens/curate_screen.dart';
import '../screens/discovery_screen.dart';
import '../screens/feed_export_screen.dart';
import '../screens/relationship_screen.dart';
import '../screens/sync_status_screen.dart';
import '../widgets/relationship_lens_view.dart';

/// go_router route table — all deep-linkable. Standalone-app fallback: this
/// router can be used directly (as [buildCatalogRouter] does for the
/// example app), or a host app can mount these same routes/screens inside
/// its own GoRouter (embeddable, matching escurel_explorer_kit's own
/// pattern).
///
/// Discovery merges what used to be separate Search/Browse screens (Browse
/// had no query of its own — it only filtered whatever Search last
/// fetched). Relationships merges what used to be a standalone Lineage
/// route, a standalone Driver-Tree route, and an inline Where-Used tab,
/// each with duplicated rendering logic.
GoRouter buildCatalogRouter({String initialLocation = '/'}) {
  return GoRouter(
    initialLocation: initialLocation,
    routes: [
      ShellRoute(
        builder: (context, state, child) => AppShell(child: child),
        routes: [
          GoRoute(path: '/', builder: (context, state) => const DiscoveryScreen()),
          GoRoute(
            path: '/entity/:id',
            builder: (context, state) => EntityDetailView(entityId: state.pathParameters['id']!),
          ),
          GoRoute(path: '/admin/sync', builder: (context, state) => const SyncStatusScreen()),
          GoRoute(path: '/admin/feed', builder: (context, state) => const FeedExportScreen()),
        ],
      ),
      // Full-screen routes (outside the shell chrome) — relationship
      // exploration and curation open as their own page, not nested inside
      // the tab layout.
      GoRoute(
        path: '/entity/:id/relate',
        builder: (context, state) => RelationshipScreen(
          entityId: state.pathParameters['id']!,
          initialLens: _parseLens(state.uri.queryParameters['lens']),
        ),
      ),
      GoRoute(
        path: '/curate/:id',
        builder: (context, state) => CurateScreen(entityId: state.pathParameters['id']!),
      ),
    ],
  );
}

RelationshipLens _parseLens(String? raw) {
  return switch (raw) {
    'lineage' => RelationshipLens.lineage,
    'driverTree' => RelationshipLens.driverTree,
    _ => RelationshipLens.whereUsed,
  };
}

/// AppShell — top bar + left nav, wraps the Discovery/Sync-Status/Feed-
/// Export tab routes. Entity Detail, Relationships, and Curate open
/// full-screen instead of being nav-rail destinations.
class AppShell extends StatelessWidget {
  final Widget child;
  const AppShell({super.key, required this.child});

  static const _destinations = [
    _NavDestination('/', 'Discover', Icons.search),
    _NavDestination('/admin/sync', 'Sync Status', Icons.sync),
    _NavDestination('/admin/feed', 'Feed Export', Icons.download_outlined),
  ];

  /// -1 (no rail item highlighted) for routes that aren't one of the rail's
  /// own destinations — entity/relate/curate previously fell through to
  /// index 0, which falsely highlighted Discovery while looking at
  /// something else entirely.
  int _currentIndex(String location) {
    return _destinations.indexWhere((d) => d.path == location);
  }

  @override
  Widget build(BuildContext context) {
    final location = GoRouterState.of(context).uri.toString();
    final selected = _currentIndex(location);

    return Scaffold(
      appBar: AppBar(
        title: const Text('erpl-adt Catalog'),
        centerTitle: false,
      ),
      body: Row(
        children: [
          NavigationRail(
            selectedIndex: selected == -1 ? null : selected,
            labelType: NavigationRailLabelType.all,
            onDestinationSelected: (i) => context.go(_destinations[i].path),
            destinations: [
              for (final d in _destinations)
                NavigationRailDestination(icon: Icon(d.icon), label: Text(d.label)),
            ],
          ),
          const VerticalDivider(width: 1),
          Expanded(child: child),
        ],
      ),
    );
  }
}

class _NavDestination {
  final String path;
  final String label;
  final IconData icon;
  const _NavDestination(this.path, this.label, this.icon);
}
