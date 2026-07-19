import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

/// Light/dark theme for erpl_catalog_kit — a monospace font is used for
/// technical identifiers (entity IDs, object types, technical_name) per
/// ux-spec.md §4, so they're visually distinct from curated business text.
class CatalogTheme {
  static TextStyle monoStyle(BuildContext context, {double? fontSize, FontWeight? fontWeight}) {
    return GoogleFonts.jetBrainsMono(
      fontSize: fontSize ?? 13,
      fontWeight: fontWeight ?? FontWeight.w400,
      color: Theme.of(context).colorScheme.onSurfaceVariant,
    );
  }

  static ThemeData light() {
    final scheme = ColorScheme.fromSeed(seedColor: const Color(0xFF2D5BFF), brightness: Brightness.light);
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      textTheme: GoogleFonts.interTextTheme(),
      scaffoldBackgroundColor: scheme.surface,
    );
  }

  static ThemeData dark() {
    final scheme = ColorScheme.fromSeed(seedColor: const Color(0xFF6C8CFF), brightness: Brightness.dark);
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      textTheme: GoogleFonts.interTextTheme(ThemeData(brightness: Brightness.dark).textTheme),
      scaffoldBackgroundColor: scheme.surface,
    );
  }
}

/// Confidentiality → color mapping shared by ConfidentialityBadge and any
/// other widget that needs to signal sensitivity at a glance.
Color confidentialityColor(String? level, ColorScheme scheme) {
  switch (level) {
    case 'Confidential':
      return Colors.red.shade700;
    case 'Internal':
      return Colors.orange.shade700;
    case 'Public':
      return Colors.green.shade700;
    default:
      return scheme.outline;
  }
}

/// Domain → color mapping (ABAP/DDIC/CDS/BW) — used by TypeIcon/EntityChip
/// so users can scan a result list by domain without reading text.
Color domainColor(String domain, ColorScheme scheme) {
  switch (domain) {
    case 'ABAP':
      return Colors.indigo;
    case 'DDIC':
      return Colors.teal;
    case 'CDS':
      return Colors.deepPurple;
    case 'BW':
      return Colors.brown;
    default:
      return scheme.primary;
  }
}
