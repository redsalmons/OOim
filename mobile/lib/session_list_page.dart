import 'package:flutter/material.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/session_queries.dart';
import 'package:oim_core/native/unified_session.dart';

import 'conversation_page.dart';
import 'settings_page.dart';
import 'mail_service.dart';

class SessionListPage extends StatefulWidget {
  const SessionListPage({super.key, required this.configPath});

  final String configPath;

  @override
  State<SessionListPage> createState() => _SessionListPageState();
}

class _SessionListPageState extends State<SessionListPage> {
  List<UnifiedSessionInfo> _sessions = [];

  @override
  void initState() {
    super.initState();
    MailService.instance.start(widget.configPath);
    MailService.instance.tick.addListener(_reload);
    _reload();
  }

  @override
  void dispose() {
    MailService.instance.tick.removeListener(_reload);
    super.dispose();
  }

  void _reload() {
    if (!mounted) return;
    setState(() => _sessions = listAllSessions(widget.configPath));
  }

  String _title(UnifiedSessionInfo s) {
    if (s.subject.isNotEmpty) return s.subject;
    final peers = s.members.where((m) => m != s.account).toList();
    return peers.isNotEmpty ? peers.join(', ') : s.account;
  }

  static final _rfcDate = RegExp(
      r'(\d{1,2})\s+(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\s+(\d{4})\s+(\d{1,2}):(\d{2}):(\d{2})');

  DateTime? _parseTime(String ts) {
    final iso = DateTime.tryParse(ts);
    if (iso != null) return iso;
    final m = _rfcDate.firstMatch(ts);
    if (m == null) return null;
    const months = {'Jan': 1, 'Feb': 2, 'Mar': 3, 'Apr': 4, 'May': 5, 'Jun': 6,
                    'Jul': 7, 'Aug': 8, 'Sep': 9, 'Oct': 10, 'Nov': 11, 'Dec': 12};
    return DateTime(int.parse(m[3]!), months[m[2]!]!, int.parse(m[1]!),
        int.parse(m[4]!), int.parse(m[5]!), int.parse(m[6]!));
  }

  String _fmtLastTime(String ts) {
    final t = _parseTime(ts);
    if (t == null) return '';
    String s(int n) => n.toString().padLeft(2, '0');
    if (AppStrings.isZh) return '${t.month}月${t.day}日 ${s(t.hour)}:${s(t.minute)}';
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    return '${months[t.month - 1]} ${t.day}, ${s(t.hour)}:${s(t.minute)}';
  }

  Widget _sessionTile(UnifiedSessionInfo s) {
    final title = _title(s);
    return ListTile(
      leading: CircleAvatar(
        child: Text(title.isNotEmpty ? title[0].toUpperCase() : '?'),
      ),
      title: Row(
        children: [
          Expanded(
            child: Text(title, maxLines: 1, overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500)),
          ),
          const SizedBox(width: 8),
          Text(
            _fmtLastTime(s.updatedAt),
            style: const TextStyle(fontSize: 11, color: Colors.grey),
          ),
        ],
      ),
      subtitle: Text(
        '${s.members.length.toString().padRight(3)} ${s.account}',
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
      ),
      trailing: s.pinned ? const Icon(Icons.push_pin, size: 16) : null,
      onTap: () async {
        await Navigator.of(context).push(
          MaterialPageRoute(
            builder: (_) =>
                ConversationPage(session: s, configPath: widget.configPath),
          ),
        );
        _reload();
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.appTitle),
        actions: [
          IconButton(
            icon: const Icon(Icons.settings_outlined),
            onPressed: () async {
              await Navigator.of(context).push(
                MaterialPageRoute(
                  builder: (_) => SettingsPage(configPath: widget.configPath),
                ),
              );
              _reload();
            },
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: () async => _reload(),
        child: _sessions.isEmpty
            ? ListView(children: [
                const SizedBox(height: 120),
                Center(
                  child: Text(
                    AppStrings.isZh ? '还没有会话' : 'No conversations yet',
                    style: const TextStyle(color: Colors.grey),
                  ),
                ),
              ])
            : Builder(builder: (context) {
                final pinned =
                    _sessions.where((s) => s.pinned && !s.hidden).toList();
                final normal =
                    _sessions.where((s) => !s.pinned && !s.hidden).toList();
                final hidden = _sessions.where((s) => s.hidden).toList();
                return ListView(children: [
                  if (pinned.isNotEmpty)
                    ExpansionTile(
                      dense: true,
                      initiallyExpanded: true,
                      leading: const Icon(Icons.push_pin_outlined, size: 18),
                      title: Text(
                          '${AppStrings.isZh ? "置顶" : "Pinned"} (${pinned.length})',
                          style: const TextStyle(fontSize: 14)),
                      children: [
                        for (final s in pinned) _sessionTile(s),
                      ],
                    ),
                  for (var i = 0; i < normal.length; i++) ...[
                    _sessionTile(normal[i]),
                    if (i < normal.length - 1 || hidden.isNotEmpty)
                      const Divider(height: 1),
                  ],
                  if (hidden.isNotEmpty)
                    ExpansionTile(
                      dense: true,
                      leading: const Icon(Icons.visibility_off_outlined,
                          size: 18),
                      title: Text(
                          '${AppStrings.isZh ? "已隐藏" : "Hidden"} (${hidden.length})',
                          style: const TextStyle(fontSize: 14)),
                      children: [
                        for (final s in hidden) _sessionTile(s),
                      ],
                    ),
                ]);
              }),
      ),
    );
  }
}
