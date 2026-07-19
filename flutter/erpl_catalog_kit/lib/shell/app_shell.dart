import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../inspector/entity_detail_view.dart';
import '../screens/browse_screen.dart';
import '../screens/curate_screen.dart';
import '../screens/driver_tree_screen.dart';
import '../screens/feed_export_screen.dart';
import '../screens/lineage_screen.dart';
import '../screens/search_screen.dart';
import '../screens/sync_status_screen.dart';

/// go_router route table — S1-S8 per ux-spec.md, all deep-linkable.
/// Standalone-app fallback: this router can be used directly (as
/// [buildCatalogRouter] does for the example app), or a host app can mount
/// these same routes/screens inside its own GoRouter (embeddable, matching
/// escurel_explorer_kit's own pattern).
GoRouter buildCatalogRouter({String initialLocation = '/'}) {
  return GoRouter(
    initialLocation: initialLocation,
    routes: [
      ShellRoute(
        builder: (context, state, child) => AppShell(child: child),
        routes: [
          GoRoute(path: '/', builder: (context, state) => const SearchScreen()),
          GoRoute(path: '/browse', builder: (context, state) => const BrowseScreen()),
          GoRoute(
            path: '/entity/:id',
            builder: (context, state) => EntityDetailView(entityId: state.pathParameters['id']!),
          ),
          GoRoute(path: '/admin/sync', builder: (context, state) => const SyncStatusScreen()),
          GoRoute(path: '/admin/feed', builder: (context, state) => const FeedExportScreen()),
        ],
      ),
      // Full-screen routes (outside the shell chrome) — lineage/driver-tree
      // expanded views and curation open as their own page, not nested
      // inside the tab layout.
      GoRoute(
        path: '/entity/:id/lineage',
        builder: (context, state) => LineageScreen(entityId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/entity/:id/driver-tree',
        builder: (context, state) => DriverTreeScreen(entityId: state.pathParameters['id']!),
      ),
      GoRoute(
        path: '/curate/:id',
        builder: (context, state) => CurateScreen(entityId: state.pathParameters['id']!),
      ),
    ],
  );
}

/// AppShell — top bar + left nav, wraps the S1/S2/S7/S8 tab routes.
class AppShell extends StatelessWidget {
  final Widget child;
  const AppShell({super.key, required this.child});

  static const _destinations = [
    _NavDestination('/', 'Search', Icons.search),
    _NavDestination('/browse', 'Browse', Icons.folder_outlined),
    _NavDestination('/admin/sync', 'Sync Status', Icons.sync),
    _NavDestination('/admin/feed', 'Feed Export', Icons.download_outlined),
  ];

  int _currentIndex(String location) {
    final index = _destinations.indexWhere((d) => d.path == location);
    return index == -1 ? 0 : index;
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
            selectedIndex: selected,
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
