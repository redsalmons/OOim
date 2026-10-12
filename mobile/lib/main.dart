import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/email_core.dart' as native;
import 'package:path_provider/path_provider.dart';

import 'session_list_page.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final status = await _initNative();
  runApp(OceanTalkMobileApp(initStatus: status));
}

Future<({bool ok, String configPath, String error})> _initNative() async {
  try {
    if (Platform.isAndroid) {
      const channel = MethodChannel('oim/keystore');
      final hex = await channel.invokeMethod<String>('getMasterKeyHex');
      if (hex == null || !native.EmailCore.setDbMasterKey(hex)) {
        return (ok: false, configPath: '', error: 'master key injection failed');
      }
    }
    final appDir = await getApplicationSupportDirectory();
    for (final sub in ['data', 'config', 'log']) {
      Directory('${appDir.path}/$sub').createSync(recursive: true);
    }
    native.EmailCore.loggerInit('${appDir.path}/log');
    if (!native.EmailCore.initializeLibemail(appDir.path)) {
      return (ok: false, configPath: '', error: 'initializeLibemail failed');
    }
    return (ok: true, configPath: '${appDir.path}/config/oim.conf', error: '');
  } catch (e) {
    return (ok: false, configPath: '', error: '$e');
  }
}

class OceanTalkMobileApp extends StatelessWidget {
  const OceanTalkMobileApp({super.key, required this.initStatus});

  final ({bool ok, String configPath, String error}) initStatus;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: AppStrings.appTitle,
      localizationsDelegates: const [
        GlobalMaterialLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
      ],
      supportedLocales: const [Locale('zh'), Locale('en')],
      home: initStatus.ok
          ? SessionListPage(configPath: initStatus.configPath)
          : Scaffold(
              appBar: AppBar(title: Text(AppStrings.appTitle)),
              body: Center(child: Text('init error: ${initStatus.error}')),
            ),
    );
  }
}
