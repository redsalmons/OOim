import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/email_core.dart' as native;
import 'package:oim_core/native/unified_session.dart';

class NewSessionPage extends StatefulWidget {
  const NewSessionPage({super.key, required this.configPath});

  final String configPath;

  @override
  State<NewSessionPage> createState() => _NewSessionPageState();
}

class _NewSessionPageState extends State<NewSessionPage> {
  final _title = TextEditingController();
  final _memberInput = TextEditingController();
  late String _account;
  final _members = <String>[];
  final _mailmen = <String>{};
  List<String> _accounts = const [];
  bool _creating = false;

  @override
  void initState() {
    super.initState();
    final config = native.EmailCore.loadConfig(widget.configPath);
    _accounts = [
      for (final a in config?.accounts ?? const <dynamic>[])
        if ((a.email as String).isNotEmpty) a.email as String,
    ];
    final def = config?.accounts
        .where((a) => a.isDefault)
        .firstOrNull;
    _account = (def != null && _accounts.contains(def.email))
        ? def.email
        : (_accounts.isNotEmpty ? _accounts.first : '');
  }

  @override
  void dispose() {
    _title.dispose();
    _memberInput.dispose();
    super.dispose();
  }

  void _toast(String msg) => ScaffoldMessenger.of(context)
      .showSnackBar(SnackBar(content: Text(msg)));

  void _addMember(String email) {
    final member = email.trim();
    if (member.isEmpty || member == _account || _members.contains(member)) {
      return;
    }
    setState(() {
      _members.add(member);
      _memberInput.clear();
    });
  }

  Future<void> _create() async {
    final title = _title.text.trim();
    if (title.isEmpty) return _toast(AppStrings.pleaseEnterTitle);
    if (_account.isEmpty) return _toast(AppStrings.pleaseSelectAccount);
    if (_members.isEmpty) return _toast(AppStrings.pleaseAddMember);

    setState(() => _creating = true);
    final allMembers = [_account, ..._members];
    String result;
    try {
      result = UnifiedSessionManager.createSession(_account, title, allMembers,
          mailmen: [_account, ..._mailmen.where((m) => m != _account)]);
    } catch (e) {
      if (mounted) {
        setState(() => _creating = false);
        _toast('${AppStrings.createEmailInstanceFailed}: $e');
      }
      return;
    }
    final json = jsonDecode(result);
    if (json['status'] != 'success') {
      if (mounted) {
        setState(() => _creating = false);
        _toast('${AppStrings.createEmailInstanceFailed}: ${json['error'] ?? 'unknown'}');
      }
      return;
    }
    if (mounted) {
      Navigator.of(context).pop();
      _toast(AppStrings.sessionCreatedSuccess);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.newSession),
        actions: [
          TextButton(
            onPressed: _creating ? null : _create,
            child: _creating
                ? const SizedBox(
                    width: 16, height: 16,
                    child: CircularProgressIndicator(strokeWidth: 2))
                : Text(AppStrings.create),
          ),
        ],
      ),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        Text(AppStrings.selectAccount,
            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
        const SizedBox(height: 6),
        DropdownButtonFormField<String>(
          initialValue: _account.isEmpty ? null : _account,
          items: [
            for (final acc in _accounts)
              DropdownMenuItem(value: acc, child: Text(acc)),
          ],
          onChanged: (v) => setState(() => _account = v ?? ''),
          decoration: const InputDecoration(
              border: OutlineInputBorder(), isDense: true),
        ),
        const SizedBox(height: 16),
        Text(AppStrings.sessionTitle,
            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
        const SizedBox(height: 6),
        TextField(
          controller: _title,
          decoration: InputDecoration(
            hintText: AppStrings.enterSessionTitle,
            border: const OutlineInputBorder(),
            isDense: true,
          ),
        ),
        const SizedBox(height: 16),
        Text(AppStrings.sessionMembers,
            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
        const SizedBox(height: 6),
        Autocomplete<String>(
          fieldViewBuilder: (context, controller, focusNode, onSubmitted) {
            _memberInput.value = controller.value;
            return TextField(
              controller: controller,
              focusNode: focusNode,
              decoration: InputDecoration(
                hintText: AppStrings.enterEmail,
                border: const OutlineInputBorder(),
                isDense: true,
                suffixIcon: IconButton(
                  icon: const Icon(Icons.add_circle_outline),
                  onPressed: () => _addMember(controller.text),
                ),
              ),
              onSubmitted: (_) => _addMember(controller.text),
            );
          },
          optionsBuilder: (value) {
            final input = value.text.toLowerCase();
            return _accounts.where((a) =>
                a != _account &&
                !_members.contains(a) &&
                a.toLowerCase().contains(input));
          },
          onSelected: _addMember,
        ),
        const SizedBox(height: 8),
        if (_members.isEmpty)
          Text(AppStrings.noMembers,
              style: TextStyle(fontSize: 12, color: Colors.grey.shade600))
        else
          Wrap(spacing: 6, runSpacing: 4, children: [
            for (final m in _members)
              Chip(
                label: Text(m, style: const TextStyle(fontSize: 12)),
                deleteIcon: const Icon(Icons.close, size: 16),
                onDeleted: () => setState(() => _members.remove(m)),
              ),
          ]),
        if (_accounts.length > 1) ...[
          const SizedBox(height: 16),
          Text(AppStrings.mailmanPool,
              style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
          const SizedBox(height: 6),
          Wrap(spacing: 8, runSpacing: 4, children: [
            for (final acc in _accounts.where((a) => a != _account))
              FilterChip(
                label: Text(acc, style: const TextStyle(fontSize: 12)),
                selected: _mailmen.contains(acc),
                onSelected: (v) => setState(() => v
                    ? _mailmen.add(acc)
                    : _mailmen.remove(acc)),
              ),
          ]),
          Text(AppStrings.mailmanPoolHint,
              style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
        ],
      ]),
    );
  }
}
