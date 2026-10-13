import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/email_core.dart' as native;
import 'package:oim_core/native/unified_session.dart';

/// Session info / member management page, pushed from the conversation page.
/// Mirrors the desktop member panel: member list, rename, add member,
/// pin / hide toggles.
class MembersPage extends StatefulWidget {
  const MembersPage({super.key, required this.session, required this.configPath});

  final UnifiedSessionInfo session;
  final String configPath;

  @override
  State<MembersPage> createState() => _MembersPageState();
}

class _MembersPageState extends State<MembersPage> {
  UnifiedSessionInfo? _live;
  UnifiedSessionInfo get session => _live ?? widget.session;

  @override
  void initState() {
    super.initState();
    _load();
  }

  void _load() {
    if (!mounted) return;
    try {
      final resp = jsonDecode(UnifiedSessionManager.getSession(session.sessionId));
      if (resp['status'] == 'success') {
        setState(() => _live =
            UnifiedSessionInfo.fromJson(resp['session'] as Map<String, dynamic>));
      }
    } catch (_) {}
  }

  void _toast(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(msg), duration: const Duration(seconds: 2)));
  }

  String get _title => session.subject.isNotEmpty
      ? session.subject
      : session.members
          .where((m) => m != session.account)
          .take(3)
          .join(', ');

  // --- rename ---

  Future<void> _rename() async {
    final controller = TextEditingController(text: session.subject);
    final newTitle = await showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text(AppStrings.isZh ? '修改会话标题' : 'Rename session'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: InputDecoration(
            hintText: AppStrings.isZh ? '会话标题' : 'Session title',
            border: const OutlineInputBorder(),
            isDense: true,
          ),
          onSubmitted: (v) => Navigator.of(dialogContext).pop(v),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(dialogContext).pop(),
            child: Text(AppStrings.cancel),
          ),
          ElevatedButton(
            onPressed: () =>
                Navigator.of(dialogContext).pop(controller.text.trim()),
            child: Text(AppStrings.isZh ? '确定' : 'OK'),
          ),
        ],
      ),
    );
    if (newTitle == null || newTitle.isEmpty || newTitle == session.subject) return;
    UnifiedSessionManager.setSessionSubject(session.sessionId, newTitle);
    _load();
  }

  // --- add member ---

  Future<void> _addMember() async {
    final newMembers = await Navigator.of(context).push<List<String>>(
      MaterialPageRoute(
        builder: (_) => AddMemberPage(
            session: session, configPath: widget.configPath),
      ),
    );
    if (newMembers == null || newMembers.isEmpty) return;

    final resultJson = UnifiedSessionManager.addMembers(
        session.account, session.sessionId, newMembers);
    native.EmailCore.logWrite('[Unified] addMembers result: $resultJson');

    try {
      final result = jsonDecode(resultJson);
      if (result['status'] == 'success') {
        if (result['upgraded'] == true) {
          _toast(AppStrings.isZh ? '会话已升级为群组' : 'Session upgraded to group');
        }
        _load();
      } else {
        _toast(AppStrings.addFailed('${result['error'] ?? 'unknown'}'));
      }
    } catch (e) {
      _toast(AppStrings.addFailed('$e'));
    }
  }

  // --- build ---

  @override
  Widget build(BuildContext context) {
    final members = session.members;
    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.isZh ? '会话信息' : 'Session info'),
      ),
      body: ListView(
        children: [
          // Title row: tap to rename.
          ListTile(
            leading: CircleAvatar(
              child: Icon(
                  session.members.length > 2 ? Icons.group : Icons.person,
                  size: 20),
            ),
            title: Text(
              _title.isEmpty ? AppStrings.noSubject : _title,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontWeight: FontWeight.w600),
            ),
            subtitle: Text(
                '${members.length}${AppStrings.isZh ? " 人" : " members"} · ${session.mode == 'mls' ? 'MLS' : 'Signal'}'),
            trailing: const Icon(Icons.edit_outlined, size: 18),
            onTap: _rename,
          ),
          const Divider(height: 1),
          // Member list.
          for (final m in members)
            ListTile(
              dense: true,
              leading: CircleAvatar(
                radius: 16,
                child: Text(m.isNotEmpty ? m[0].toUpperCase() : '?',
                    style: const TextStyle(fontSize: 12)),
              ),
              title: Text(
                m.toLowerCase() == session.account.toLowerCase()
                    ? AppStrings.me
                    : m.split('@').first,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
              subtitle: Text(m, maxLines: 1, overflow: TextOverflow.ellipsis),
            ),
          ListTile(
            dense: true,
            leading: const Icon(Icons.person_add_outlined, size: 20),
            title: Text(AppStrings.isZh ? '添加成员' : 'Add member'),
            trailing: const Icon(Icons.chevron_right, size: 20),
            onTap: _addMember,
          ),
          const Divider(height: 1),
          // Pin / hide toggles (persisted in DB).
          SwitchListTile(
            dense: true,
            title: Text(AppStrings.pinSession),
            value: session.pinned,
            onChanged: (v) {
              UnifiedSessionManager.setSessionFlags(
                  session.sessionId, v, session.hidden);
              _load();
            },
          ),
          SwitchListTile(
            dense: true,
            title: Text(AppStrings.hideSession),
            value: session.hidden,
            onChanged: (v) {
              UnifiedSessionManager.setSessionFlags(
                  session.sessionId, session.pinned, v);
              _load();
            },
          ),
        ],
      ),
    );
  }
}

/// Pushed page for picking new members from the configured accounts.
/// Pops with the selected (and/or manually entered) emails on Save.
class AddMemberPage extends StatefulWidget {
  const AddMemberPage(
      {super.key, required this.session, required this.configPath});

  final UnifiedSessionInfo session;
  final String configPath;

  @override
  State<AddMemberPage> createState() => _AddMemberPageState();
}

class _AddMemberPageState extends State<AddMemberPage> {
  final _search = TextEditingController();
  final _selected = <String>{};
  late final List<String> _candidates;

  @override
  void initState() {
    super.initState();
    final existing = widget.session.members.toSet();
    final config = native.EmailCore.loadConfig(widget.configPath);
    _candidates = (config?.accounts ?? <native.EmailAccountData>[])
        .where((a) => a.email.isNotEmpty && !existing.contains(a.email))
        .map((a) => a.email)
        .toList();
  }

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  void _save() {
    final manual = _search.text.trim();
    final emails = {..._selected};
    if (manual.contains('@') && !widget.session.members.contains(manual)) {
      emails.add(manual);
    }
    if (emails.isEmpty) return;
    Navigator.of(context).pop(emails.toList());
  }

  @override
  Widget build(BuildContext context) {
    final query = _search.text.trim().toLowerCase();
    final filtered = _candidates
        .where((a) => query.isEmpty || a.toLowerCase().contains(query))
        .toList();
    final canSave = _selected.isNotEmpty ||
        (query.contains('@') &&
            !widget.session.members.contains(_search.text.trim()));
    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.isZh ? '添加成员' : 'Add member'),
        actions: [
          TextButton(
            onPressed: canSave ? _save : null,
            child: Text(AppStrings.isZh ? '保存' : 'Save'),
          ),
        ],
      ),
      body: Column(children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: TextField(
            controller: _search,
            autofocus: true,
            decoration: InputDecoration(
              hintText: AppStrings.addMemberHint,
              prefixIcon: const Icon(Icons.search, size: 20),
              border: const OutlineInputBorder(),
              isDense: true,
            ),
            onChanged: (_) => setState(() {}),
            onSubmitted: (_) => _save(),
          ),
        ),
        Expanded(
          child: ListView(children: [
            for (final a in filtered)
              CheckboxListTile(
                dense: true,
                secondary: CircleAvatar(
                  radius: 16,
                  child: Text(a.isNotEmpty ? a[0].toUpperCase() : '?',
                      style: const TextStyle(fontSize: 12)),
                ),
                title: Text(a.split('@').first,
                    maxLines: 1, overflow: TextOverflow.ellipsis),
                subtitle:
                    Text(a, maxLines: 1, overflow: TextOverflow.ellipsis),
                value: _selected.contains(a),
                onChanged: (v) => setState(() {
                  if (v == true) {
                    _selected.add(a);
                  } else {
                    _selected.remove(a);
                  }
                }),
              ),
            if (filtered.isEmpty)
              Padding(
                padding: const EdgeInsets.all(24),
                child: Center(
                  child: Text(
                    AppStrings.isZh ? '无匹配账号' : 'No matching account',
                    style: const TextStyle(color: Colors.grey),
                  ),
                ),
              ),
          ]),
        ),
      ]),
    );
  }
}
