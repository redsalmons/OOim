import 'package:flutter/material.dart';

/// App-specific semantic colors that Material's ColorScheme does not model
/// (chat bubbles, list panes, section headers...). Registered as a
/// ThemeExtension so every widget can read `context.oim.xxx` and get the
/// right value for the active brightness.
class OimColors extends ThemeExtension<OimColors> {
  final Color sidebar;
  final Color listPane;            // left list background
  final Color sectionHeader;       // first-level section header
  final Color groupHeader;         // second-level group header
  final Color selectedItem;        // selected list row
  final Color panel;               // side panels / input bar background
  final Color inputField;          // text field surface
  final Color border;              // hairline borders
  final Color bubbleMine;          // outgoing chat bubble
  final Color bubbleTheirs;        // incoming chat bubble
  final Color textPrimary;
  final Color textSecondary;
  final Color textMuted;
  final Color accentGreen;         // WeChat-style send / ok
  final Color avatarMineBg;
  final Color avatarMineFg;
  final Color avatarTheirsBg;
  final Color avatarTheirsFg;
  final Color avatarGroupBg;
  final Color avatarGroupFg;

  const OimColors({
    required this.sidebar,
    required this.listPane,
    required this.sectionHeader,
    required this.groupHeader,
    required this.selectedItem,
    required this.panel,
    required this.inputField,
    required this.border,
    required this.bubbleMine,
    required this.bubbleTheirs,
    required this.textPrimary,
    required this.textSecondary,
    required this.textMuted,
    required this.accentGreen,
    required this.avatarMineBg,
    required this.avatarMineFg,
    required this.avatarTheirsBg,
    required this.avatarTheirsFg,
    required this.avatarGroupBg,
    required this.avatarGroupFg,
  });

  static const light = OimColors(
    sidebar: Color(0xFF1C1B33),
    listPane: Color(0xFFFAFAFA),
    sectionHeader: Color(0xFFE4E4E4),
    groupHeader: Color(0xFFF0F0F0),
    selectedItem: Color(0xFFE3F2FD),
    panel: Color(0xFFF7F7F7),
    inputField: Colors.white,
    border: Color(0xFFE0E0E0),
    bubbleMine: Color(0xFF95EC69),
    bubbleTheirs: Colors.white,
    textPrimary: Color(0xFF212121),
    textSecondary: Color(0xFF616161),
    textMuted: Color(0xFF9E9E9E),
    accentGreen: Color(0xFF07C160),
    avatarMineBg: Color(0xFFC8E6C9),
    avatarMineFg: Color(0xFF388E3C),
    avatarTheirsBg: Color(0xFFBBDEFB),
    avatarTheirsFg: Color(0xFF1976D2),
    avatarGroupBg: Color(0xFFC8E6C9),
    avatarGroupFg: Color(0xFF388E3C),
  );

  static const dark = OimColors(
    sidebar: Color(0xFF141422),
    listPane: Color(0xFF1E1E1E),
    sectionHeader: Color(0xFF2C2C2C),
    groupHeader: Color(0xFF262626),
    selectedItem: Color(0xFF2D3A4A),
    panel: Color(0xFF232323),
    inputField: Color(0xFF2A2A2A),
    border: Color(0xFF3A3A3A),
    bubbleMine: Color(0xFF3E7A3A),
    bubbleTheirs: Color(0xFF2E2E2E),
    textPrimary: Color(0xFFE6E6E6),
    textSecondary: Color(0xFFB0B0B0),
    textMuted: Color(0xFF7A7A7A),
    accentGreen: Color(0xFF07C160),
    avatarMineBg: Color(0xFF2E4A2E),
    avatarMineFg: Color(0xFF81C784),
    avatarTheirsBg: Color(0xFF263A55),
    avatarTheirsFg: Color(0xFF64B5F6),
    avatarGroupBg: Color(0xFF2E4A2E),
    avatarGroupFg: Color(0xFF81C784),
  );

  @override
  OimColors copyWith({
    Color? sidebar, Color? listPane, Color? sectionHeader, Color? groupHeader,
    Color? selectedItem, Color? panel, Color? inputField, Color? border,
    Color? bubbleMine, Color? bubbleTheirs, Color? textPrimary, Color? textSecondary,
    Color? textMuted, Color? accentGreen, Color? avatarMineBg, Color? avatarMineFg,
    Color? avatarTheirsBg, Color? avatarTheirsFg, Color? avatarGroupBg, Color? avatarGroupFg,
  }) {
    return OimColors(
      sidebar: sidebar ?? this.sidebar,
      listPane: listPane ?? this.listPane,
      sectionHeader: sectionHeader ?? this.sectionHeader,
      groupHeader: groupHeader ?? this.groupHeader,
      selectedItem: selectedItem ?? this.selectedItem,
      panel: panel ?? this.panel,
      inputField: inputField ?? this.inputField,
      border: border ?? this.border,
      bubbleMine: bubbleMine ?? this.bubbleMine,
      bubbleTheirs: bubbleTheirs ?? this.bubbleTheirs,
      textPrimary: textPrimary ?? this.textPrimary,
      textSecondary: textSecondary ?? this.textSecondary,
      textMuted: textMuted ?? this.textMuted,
      accentGreen: accentGreen ?? this.accentGreen,
      avatarMineBg: avatarMineBg ?? this.avatarMineBg,
      avatarMineFg: avatarMineFg ?? this.avatarMineFg,
      avatarTheirsBg: avatarTheirsBg ?? this.avatarTheirsBg,
      avatarTheirsFg: avatarTheirsFg ?? this.avatarTheirsFg,
      avatarGroupBg: avatarGroupBg ?? this.avatarGroupBg,
      avatarGroupFg: avatarGroupFg ?? this.avatarGroupFg,
    );
  }

  @override
  OimColors lerp(ThemeExtension<OimColors>? other, double t) {
    if (other is! OimColors) return this;
    Color l(Color a, Color b) => Color.lerp(a, b, t)!;
    return OimColors(
      sidebar: l(sidebar, other.sidebar),
      listPane: l(listPane, other.listPane),
      sectionHeader: l(sectionHeader, other.sectionHeader),
      groupHeader: l(groupHeader, other.groupHeader),
      selectedItem: l(selectedItem, other.selectedItem),
      panel: l(panel, other.panel),
      inputField: l(inputField, other.inputField),
      border: l(border, other.border),
      bubbleMine: l(bubbleMine, other.bubbleMine),
      bubbleTheirs: l(bubbleTheirs, other.bubbleTheirs),
      textPrimary: l(textPrimary, other.textPrimary),
      textSecondary: l(textSecondary, other.textSecondary),
      textMuted: l(textMuted, other.textMuted),
      accentGreen: l(accentGreen, other.accentGreen),
      avatarMineBg: l(avatarMineBg, other.avatarMineBg),
      avatarMineFg: l(avatarMineFg, other.avatarMineFg),
      avatarTheirsBg: l(avatarTheirsBg, other.avatarTheirsBg),
      avatarTheirsFg: l(avatarTheirsFg, other.avatarTheirsFg),
      avatarGroupBg: l(avatarGroupBg, other.avatarGroupBg),
      avatarGroupFg: l(avatarGroupFg, other.avatarGroupFg),
    );
  }
}

extension OimThemeX on BuildContext {
  OimColors get oim => Theme.of(this).extension<OimColors>()!;
  ColorScheme get scheme => Theme.of(this).colorScheme;
  bool get isDark => Theme.of(this).brightness == Brightness.dark;
}

class AppTheme {
  static const Color seed = Color(0xFF5B5FEF);
  static const Color sidebarColor = Color(0xFF1C1B33);
  static const List<Color> avatarGradient = [Color(0xFF6C63FF), Color(0xFF3B82F6)];

  static ThemeData light() => _build(Brightness.light);
  static ThemeData dark() => _build(Brightness.dark);

  static ThemeData _build(Brightness brightness) {
    final isDark = brightness == Brightness.dark;
    final colorScheme = ColorScheme.fromSeed(seedColor: seed, brightness: brightness);
    final oim = isDark ? OimColors.dark : OimColors.light;

    return ThemeData(
      useMaterial3: true,
      brightness: brightness,
      colorScheme: colorScheme,
      extensions: [oim],
      scaffoldBackgroundColor: isDark ? const Color(0xFF121212) : const Color(0xFFF6F6FB),
      visualDensity: VisualDensity.adaptivePlatformDensity,
      cardTheme: CardThemeData(
        elevation: 0,
        color: isDark ? const Color(0xFF1E1E1E) : Colors.white,
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
      ),
      dialogTheme: DialogThemeData(
        backgroundColor: isDark ? const Color(0xFF1E1E1E) : Colors.white,
        surfaceTintColor: Colors.transparent,
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          elevation: 0,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: oim.inputField,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: BorderSide(color: oim.border),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: BorderSide(color: oim.border),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(10),
          borderSide: BorderSide(color: colorScheme.primary, width: 1.5),
        ),
      ),
      dividerTheme: DividerThemeData(color: oim.border),
      appBarTheme: AppBarTheme(
        backgroundColor: colorScheme.primary,
        foregroundColor: Colors.white,
        elevation: 0,
      ),
    );
  }
}
