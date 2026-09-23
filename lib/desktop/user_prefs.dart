import 'dart:convert';
import 'dart:io';

/// App-level UI preferences (username, locale, theme mode, text scale).
/// Stored as JSON next to oim.conf so both the main window and the standalone
/// config window can read them.

String prefsPathFor(String configPath) =>
    '${File(configPath).parent.path}/user_prefs.json';

Map<String, dynamic> readUserPrefs(String configPath) {
  try {
    final f = File(prefsPathFor(configPath));
    if (f.existsSync()) {
      final j = jsonDecode(f.readAsStringSync());
      if (j is Map<String, dynamic>) return j;
    }
  } catch (_) {}
  return {};
}

void writeUserPref(String configPath, String key, dynamic value) {
  try {
    final prefs = readUserPrefs(configPath);
    prefs[key] = value;
    File(prefsPathFor(configPath)).writeAsStringSync(jsonEncode(prefs));
  } catch (_) {}
}
