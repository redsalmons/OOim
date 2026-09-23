import 'dart:convert';
import 'dart:io';
import 'dart:ui';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:path_provider/path_provider.dart';
import 'desktop/desktop_home.dart';
import 'desktop/dialogs/email_config_dialog.dart';
import 'desktop/app_theme.dart';
import 'desktop/user_prefs.dart';
import 'native/email_core.dart' as native;
import 'i18n/app_strings.dart' show AppStrings, appLocale;

void main(List<String> args) {
  if (args.isNotEmpty && args[0] == 'multi_window') {
    final windowId = int.parse(args[1]);
    final Map<String, dynamic> arguments = args.length > 2 && args[2].isNotEmpty
        ? jsonDecode(args[2]) as Map<String, dynamic>
        : <String, dynamic>{};
    final configPath = arguments['configPath'] as String? ?? '';
    runApp(_ConfigWindowApp(windowId: windowId, configPath: configPath));
    return;
  }
  runApp(const OIMApp());
}

void _applyPrefs(Map<String, dynamic> prefs) {
  final loc = prefs['locale']?.toString();
  appLocale.value = (loc == 'zh' || loc == 'en') ? Locale(loc!) : null;
  final theme = prefs['themeMode']?.toString();
  appThemeMode.value = theme == 'light'
      ? ThemeMode.light
      : theme == 'dark'
          ? ThemeMode.dark
          : ThemeMode.system;
  final scale = (prefs['textScale'] as num?)?.toDouble();
  if (scale != null && scale > 0) appTextScale.value = scale;
}

class _ConfigWindowApp extends StatelessWidget {
  final int windowId;
  final String configPath;

  const _ConfigWindowApp({required this.windowId, required this.configPath});

  @override
  Widget build(BuildContext context) {
    _applyPrefs(readUserPrefs(configPath));
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light(),
      darkTheme: AppTheme.dark(),
      themeMode: ThemeMode.system,
      home: EmailConfigDialog(
        configPath: configPath,
        standalone: true,
        onDone: () async {
          await DesktopMultiWindow.invokeMethod(0, 'config_completed', null);
          await WindowController.fromWindowId(windowId).close();
        },
      ),
    );
  }
}

/// Global theme mode. Defaults to following the OS; the settings page can
/// override it at runtime.
final ValueNotifier<ThemeMode> appThemeMode = ValueNotifier(ThemeMode.system);

/// Global text scale: small 0.9x / medium 1.0x / large 1.15x. Applied through
/// MediaQuery so every Text (including hard-coded fontSize) follows it.
final ValueNotifier<double> appTextScale = ValueNotifier(1.0);
const double kTextScaleSmall = 0.9;
const double kTextScaleMedium = 1.0;
const double kTextScaleLarge = 1.15;

class OIMApp extends StatelessWidget {
  const OIMApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<ThemeMode>(
      valueListenable: appThemeMode,
      builder: (context, mode, _) => ValueListenableBuilder<double>(
        valueListenable: appTextScale,
        builder: (context, scale, _) => ValueListenableBuilder<Locale?>(
        valueListenable: appLocale,
        builder: (context, _, __) => MaterialApp(
          title: 'OIM',
          debugShowCheckedModeBanner: false,
          theme: AppTheme.light(),
          darkTheme: AppTheme.dark(),
          themeMode: mode,
          builder: (context, child) => MediaQuery(
            data: MediaQuery.of(context).copyWith(textScaler: TextScaler.linear(scale)),
            child: child!,
          ),
          home: const AppRoot(),
        ),
        ),
      ),
    );
  }
}

class AppRoot extends StatefulWidget {
  const AppRoot({super.key});

  @override
  State<AppRoot> createState() => _AppRootState();
}

class _AppRootState extends State<AppRoot> {
  bool _checking = true;
  bool _configured = false;

  @override
  void initState() {
    super.initState();
    DesktopMultiWindow.setMethodHandler(_handleMethodCall);
    native.EmailCore.initialize();
    _initApp();
  }

  Future<void> _initApp() async {
    final appDir = await getApplicationSupportDirectory();

    // Create log directory and initialize logger first
    final logDir = Directory('${appDir.path}/log');
    if (!logDir.existsSync()) {
      logDir.createSync(recursive: true);
    }
    native.EmailCore.loggerInit(logDir.path);
    native.EmailCore.logWrite('[Dart] Logger initialized');

    _applyPrefs(readUserPrefs('${appDir.path}/config/oim.conf'));

    // Initialize libemail system
    native.EmailCore.logWrite('[Dart] Initializing libemail...');
    final initResult = native.EmailCore.initializeLibemail(appDir.path);
    native.EmailCore.logWrite('[Dart] Libemail initialization result: $initResult');

    // Migration: Update islocal for existing emails
    native.EmailCore.logWrite('[Dart] Running islocal migration...');
    final migrateResult = native.EmailCore.migrateIslocal();
    native.EmailCore.logWrite('[Dart] Islocal migration result: $migrateResult');

    _checkConfig();
  }

  Future<dynamic> _handleMethodCall(MethodCall call, int fromWindowId) async {
    if (call.method == 'config_completed' && mounted) {
      setState(() {
        _configured = true;
      });
    }
    return null;
  }

  Future<void> _checkConfig() async {
    final appDir = await getApplicationSupportDirectory();
    final configPath = '${appDir.path}/config/oim.conf';
    final configFile = File(configPath);

    native.EmailCore.logWrite('[Dart] App dir: ${appDir.path}');
    native.EmailCore.logWrite('[Dart] Config path: $configPath');
    native.EmailCore.logWrite('[Dart] Config file exists (Dart): ${configFile.existsSync()}');

    final exists = native.EmailCore.configExists(configPath);
    native.EmailCore.logWrite('[Dart] Config exists (native): $exists');

    if (!mounted) return;

    if (!exists) {
      setState(() {
        _configured = false;
        _checking = false;
      });
      _showConfigDialog(configPath);
    } else {
      setState(() {
        _configured = true;
        _checking = false;
      });
      // Ensure main window is shown
      await WindowController.main().show();
    }
  }

  Future<void> _showConfigDialog(String configPath) async {
    final window = await DesktopMultiWindow.createWindow(jsonEncode({
      'configPath': configPath,
    }));
    window
      ..setFrame(const Offset(0, 0) & const Size(680, 560))
      ..center()
      ..setTitle(AppStrings.emailConfig)
      ..show();
  }

  @override
  Widget build(BuildContext context) {
    if (_checking || !_configured) {
      return const Scaffold();
    }
    return const DesktopHome();
  }
}
