import 'email_core.dart' as native;
import 'unified_session.dart';

/// Session list helpers shared by the desktop and mobile UIs. Conversation
/// content comes from [UnifiedSessionManager.listMessages] (native core).

/// Accounts configured in oim.conf (non-empty emails only).
List<String> configAccounts(String configPath) {
  final config = native.EmailCore.loadConfig(configPath);
  return config?.accounts
          .where((a) => a.email.isNotEmpty)
          .map((a) => a.email)
          .toList() ??
      <String>[];
}

/// All unified sessions across configured accounts, de-duplicated by
/// session id. Pinned first, hidden last, otherwise as returned.
List<UnifiedSessionInfo> listAllSessions(String configPath) {
  final all = <UnifiedSessionInfo>[];
  final seen = <String>{};
  for (final account in configAccounts(configPath)) {
    try {
      for (final s in UnifiedSessionManager.listSessionsAsObjects(account)) {
        if (s.sessionId.isNotEmpty && seen.add(s.sessionId)) all.add(s);
      }
    } catch (e) {
      native.EmailCore.logWrite('[Session] listSessions error for $account: $e');
    }
  }
  all.sort((a, b) {
    if (a.pinned != b.pinned) return a.pinned ? -1 : 1;
    if (a.hidden != b.hidden) return a.hidden ? 1 : -1;
    return 0;
  });
  return all;
}
