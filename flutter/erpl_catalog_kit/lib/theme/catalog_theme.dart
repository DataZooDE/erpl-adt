import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

import 'catalog_tokens.dart';

/// Light/dark theme for erpl_catalog_kit — a monospace font is used for
/// technical identifiers (entity IDs, object types, technical_name) per
/// ux-spec.md §4, so they're visually distinct from curated business text.
///
/// Colors come from the "data zoo" brand design system
/// (colors_and_type.css) — typography stays Inter/JetBrains Mono per an
/// explicit decision to not bundle the brand's Arvo/Lato fonts.
class CatalogTheme {
  static TextStyle monoStyle(BuildContext context, {double? fontSize, FontWeight? fontWeight}) {
    return GoogleFonts.jetBrainsMono(
      fontSize: fontSize ?? 13,
      fontWeight: fontWeight ?? FontWeight.w400,
      color: Theme.of(context).colorScheme.onSurfaceVariant,
    );
  }

  static ThemeData light() {
    const scheme = ColorScheme(
      brightness: Brightness.light,
      primary: Color(0xFF004149),
      onPrimary: Color(0xFFFFFFFF),
      primaryContainer: Color(0xFF1D5962),
      onPrimaryContainer: Color(0xFFCCE8EC),
      secondary: Color(0xFF006A61),
      onSecondary: Color(0xFFFFFFFF),
      secondaryContainer: Color(0xFF9DF2E6),
      onSecondaryContainer: Color(0xFF00201D),
      tertiary: Color(0xFF4A6268),
      onTertiary: Color(0xFFFFFFFF),
      tertiaryContainer: Color(0xFFCDE7EE),
      onTertiaryContainer: Color(0xFF051F24),
      surface: Color(0xFFF7F9FF),
      onSurface: Color(0xFF091D2E),
      surfaceContainerLowest: Color(0xFFFFFFFF),
      surfaceContainerLow: Color(0xFFEDF4FF),
      surfaceContainer: Color(0xFFE5EEFC),
      surfaceContainerHigh: Color(0xFFD9EAFF),
      surfaceContainerHighest: Color(0xFFC7DCF4),
      onSurfaceVariant: Color(0xFF40484D),
      outline: Color(0xFF70787D),
      outlineVariant: Color(0xFFC0C8CD),
      error: Color(0xFFBA1A1A),
      onError: Color(0xFFFFFFFF),
      errorContainer: Color(0xFFFFDAD6),
      onErrorContainer: Color(0xFF410002),
    );
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      textTheme: GoogleFonts.interTextTheme(),
      scaffoldBackgroundColor: scheme.surface,
      extensions: [
        CatalogTokens.brand(
          warning: const Color(0xFF9A6700),
          success: const Color(0xFF2E7D5C),
          info: const Color(0xFF0B5E8F),
          // These are used directly as TEXT color in TypeIcon chips (not
          // just as a background tint), so they need to be readable
          // foreground colors — a *Container tone (meant for large fills)
          // reads as near-invisible here.
          domainBw: scheme.primary,
          domainCds: scheme.secondary,
          domainDdic: scheme.tertiary,
          domainAbap: const Color(0xFF7A4B00),
        ),
      ],
    );
  }

  // No dark-mode tokens exist in colors_and_type.css yet (light-only
  // :root) — keep deriving dark mode from a seeded scheme until the
  // designer supplies real dark tokens, rather than inventing them here.
  // TODO(brand): replace with real dark-theme hex values once available.
  static ThemeData dark() {
    final scheme =
        ColorScheme.fromSeed(seedColor: const Color(0xFF6C8CFF), brightness: Brightness.dark);
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      textTheme: GoogleFonts.interTextTheme(ThemeData(brightness: Brightness.dark).textTheme),
      scaffoldBackgroundColor: scheme.surface,
      extensions: [
        CatalogTokens.brand(
          warning: const Color(0xFFE0B23D),
          success: const Color(0xFF6FCF9B),
          info: const Color(0xFF6FB6E0),
          domainBw: scheme.primary,
          domainCds: scheme.secondary,
          domainDdic: scheme.tertiary,
          domainAbap: const Color(0xFFDDA85C),
        ),
      ],
    );
  }
}

/// Confidentiality → color mapping shared by ConfidentialityBadge and any
/// other widget that needs to signal sensitivity at a glance. Theme-derived
/// (not hardcoded Material shades) so a palette swap propagates here too.
Color confidentialityColor(String? level, BuildContext context) {
  final scheme = Theme.of(context).colorScheme;
  final tokens = Theme.of(context).extension<CatalogTokens>();
  switch (level) {
    case 'Confidential':
      return scheme.error;
    case 'Internal':
      return tokens?.warning ?? scheme.tertiary;
    case 'Public':
      return tokens?.success ?? scheme.secondary;
    default:
      return scheme.outline;
  }
}

/// Domain → color mapping (ABAP/DDIC/CDS/BW) — used by TypeIcon/EntityChip
/// so users can scan a result list by domain without reading text. Mirrors
/// the wireframe's exact chip treatment: BW/CDS/DDIC/ABAP against
/// primary/secondary/tertiary/surface containers respectively.
Color domainColor(String domain, BuildContext context) {
  final tokens = Theme.of(context).extension<CatalogTokens>();
  if (tokens != null) return tokens.domainColor(domain);
  return Theme.of(context).colorScheme.primary;
}
