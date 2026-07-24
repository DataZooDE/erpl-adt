import 'package:flutter/material.dart';

/// Spacing/radius/shadow/semantic-color tokens from the "data zoo" brand
/// design system (colors_and_type.css) that don't map onto any first-class
/// slot in Flutter's Material 3 [ColorScheme]/[ThemeData] — kept as a
/// [ThemeExtension] so they theme-swap and interpolate like everything else.
@immutable
class CatalogTokens extends ThemeExtension<CatalogTokens> {
  final double spacing1;
  final double spacing2;
  final double spacing3;
  final double spacing4;
  final double spacing5;
  final double spacing6;
  final double spacing7;
  final double spacing8;

  final BorderRadius radiusXs;
  final BorderRadius radiusSm;
  final BorderRadius radiusMd;
  final BorderRadius radiusLg;
  final BorderRadius radiusXl;
  final BorderRadius radius2xl;

  final List<BoxShadow> shadowAmbient;
  final List<BoxShadow> shadowModal;

  /// Semantic colors — distinct from the accent hue, used for status/
  /// freshness signaling (a green/amber/red dot, a warning badge), never
  /// for branding.
  final Color warning;
  final Color success;
  final Color info;

  /// Domain chip fills, in the exact order the wireframes use them
  /// (BW/CDS/DDIC/ABAP), applied via [CatalogTokens.domainColor].
  final Color domainBw;
  final Color domainCds;
  final Color domainDdic;
  final Color domainAbap;

  const CatalogTokens({
    required this.spacing1,
    required this.spacing2,
    required this.spacing3,
    required this.spacing4,
    required this.spacing5,
    required this.spacing6,
    required this.spacing7,
    required this.spacing8,
    required this.radiusXs,
    required this.radiusSm,
    required this.radiusMd,
    required this.radiusLg,
    required this.radiusXl,
    required this.radius2xl,
    required this.shadowAmbient,
    required this.shadowModal,
    required this.warning,
    required this.success,
    required this.info,
    required this.domainBw,
    required this.domainCds,
    required this.domainDdic,
    required this.domainAbap,
  });

  /// data zoo spacing/radius/shadow scale — identical in both themes (these
  /// tokens don't vary with brightness in colors_and_type.css). Semantic
  /// and domain colors are supplied by the caller since they differ
  /// slightly in emphasis between the light and dark palettes.
  factory CatalogTokens.brand({
    required Color warning,
    required Color success,
    required Color info,
    required Color domainBw,
    required Color domainCds,
    required Color domainDdic,
    required Color domainAbap,
  }) {
    return CatalogTokens(
      spacing1: 4,
      spacing2: 8,
      spacing3: 12,
      spacing4: 16,
      spacing5: 20,
      spacing6: 24,
      spacing7: 32,
      spacing8: 40,
      radiusXs: BorderRadius.circular(4),
      radiusSm: BorderRadius.circular(6),
      radiusMd: BorderRadius.circular(8),
      radiusLg: BorderRadius.circular(10),
      radiusXl: BorderRadius.circular(12),
      radius2xl: BorderRadius.circular(16),
      shadowAmbient: const [
        BoxShadow(color: Color(0x0F091D2E), blurRadius: 32, offset: Offset(0, 12)),
      ],
      shadowModal: const [
        BoxShadow(color: Color(0x1A091D2E), blurRadius: 64, offset: Offset(0, 24)),
      ],
      warning: warning,
      success: success,
      info: info,
      domainBw: domainBw,
      domainCds: domainCds,
      domainDdic: domainDdic,
      domainAbap: domainAbap,
    );
  }

  @override
  CatalogTokens copyWith({
    double? spacing1,
    double? spacing2,
    double? spacing3,
    double? spacing4,
    double? spacing5,
    double? spacing6,
    double? spacing7,
    double? spacing8,
    BorderRadius? radiusXs,
    BorderRadius? radiusSm,
    BorderRadius? radiusMd,
    BorderRadius? radiusLg,
    BorderRadius? radiusXl,
    BorderRadius? radius2xl,
    List<BoxShadow>? shadowAmbient,
    List<BoxShadow>? shadowModal,
    Color? warning,
    Color? success,
    Color? info,
    Color? domainBw,
    Color? domainCds,
    Color? domainDdic,
    Color? domainAbap,
  }) {
    return CatalogTokens(
      spacing1: spacing1 ?? this.spacing1,
      spacing2: spacing2 ?? this.spacing2,
      spacing3: spacing3 ?? this.spacing3,
      spacing4: spacing4 ?? this.spacing4,
      spacing5: spacing5 ?? this.spacing5,
      spacing6: spacing6 ?? this.spacing6,
      spacing7: spacing7 ?? this.spacing7,
      spacing8: spacing8 ?? this.spacing8,
      radiusXs: radiusXs ?? this.radiusXs,
      radiusSm: radiusSm ?? this.radiusSm,
      radiusMd: radiusMd ?? this.radiusMd,
      radiusLg: radiusLg ?? this.radiusLg,
      radiusXl: radiusXl ?? this.radiusXl,
      radius2xl: radius2xl ?? this.radius2xl,
      shadowAmbient: shadowAmbient ?? this.shadowAmbient,
      shadowModal: shadowModal ?? this.shadowModal,
      warning: warning ?? this.warning,
      success: success ?? this.success,
      info: info ?? this.info,
      domainBw: domainBw ?? this.domainBw,
      domainCds: domainCds ?? this.domainCds,
      domainDdic: domainDdic ?? this.domainDdic,
      domainAbap: domainAbap ?? this.domainAbap,
    );
  }

  @override
  CatalogTokens lerp(ThemeExtension<CatalogTokens>? other, double t) {
    if (other is! CatalogTokens) return this;
    return CatalogTokens(
      spacing1: spacing1, spacing2: spacing2, spacing3: spacing3, spacing4: spacing4,
      spacing5: spacing5, spacing6: spacing6, spacing7: spacing7, spacing8: spacing8,
      radiusXs: radiusXs, radiusSm: radiusSm, radiusMd: radiusMd, radiusLg: radiusLg,
      radiusXl: radiusXl, radius2xl: radius2xl,
      shadowAmbient: BoxShadow.lerpList(shadowAmbient, other.shadowAmbient, t) ?? shadowAmbient,
      shadowModal: BoxShadow.lerpList(shadowModal, other.shadowModal, t) ?? shadowModal,
      warning: Color.lerp(warning, other.warning, t) ?? warning,
      success: Color.lerp(success, other.success, t) ?? success,
      info: Color.lerp(info, other.info, t) ?? info,
      domainBw: Color.lerp(domainBw, other.domainBw, t) ?? domainBw,
      domainCds: Color.lerp(domainCds, other.domainCds, t) ?? domainCds,
      domainDdic: Color.lerp(domainDdic, other.domainDdic, t) ?? domainDdic,
      domainAbap: Color.lerp(domainAbap, other.domainAbap, t) ?? domainAbap,
    );
  }

  Color domainColor(String domain) {
    switch (domain) {
      case 'BW':
        return domainBw;
      case 'CDS':
        return domainCds;
      case 'DDIC':
        return domainDdic;
      case 'ABAP':
        return domainAbap;
      default:
        return info;
    }
  }
}
