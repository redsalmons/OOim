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
import 'native/email_core.dart' as native;

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

class _ConfigWindowApp extends StatelessWidget {
  final int windowId;
  final String configPath;

  const _ConfigWindowApp({required this.windowId, required this.configPath});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light(),
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

class OIMApp extends StatelessWidget {
  const OIMApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'OIM',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.light(),
      home: const AppRoot(),
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

    // Initialize libemail system
    native.EmailCore.logWrite('[Dart] Initializing libemail...');
    final initResult = native.EmailCore.initializeLibemail(appDir.path);
    native.EmailCore.logWrite('[Dart] Libemail initialization result: $initResult');

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
      ..setTitle('邮箱配置')
      ..show();
  }

  @override
  Widget build(BuildContext context) {
    if (_checking || !_configured) {
      return const Scaffold(backgroundColor: Colors.white);
    }
    return const DesktopHome();
  }
}
