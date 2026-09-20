import 'dart:convert';
import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import '../../native/unified_session.dart';
import '../../i18n/app_strings.dart';

class CreateSessionDialog extends StatefulWidget {
  final List<String> accounts;
  final String configPath;
  final VoidCallback? onCreated;

  const CreateSessionDialog({
    super.key,
    required this.accounts,
    required this.configPath,
    this.onCreated,
  });

  @override
  State<CreateSessionDialog> createState() => _CreateSessionDialogState();
}

class _CreateSessionDialogState extends State<CreateSessionDialog> {
  late String _selectedAccount;
  final _titleController = TextEditingController();
  TextEditingController _membersController = TextEditingController();
  final List<String> _members = [];
  final Set<String> _mailmen = {}; // 附加发送邮差池（主邮箱默认总是邮差，不在此列）
  bool _creating = false;

  @override
  void initState() {
    super.initState();
    _selectedAccount = widget.accounts.isNotEmpty ? widget.accounts.first : '';
  }

  void _addMember() {
    final text = _membersController.text.trim();
    if (text.isEmpty) return;
    if (!_members.contains(text)) {
      setState(() {
        _members.add(text);
        _membersController.clear();
      });
    }
  }

  void _removeMember(String member) {
    setState(() {
      _members.remove(member);
    });
  }

  Future<void> _createSession() async {
    final title = _titleController.text.trim();
    if (title.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.pleaseEnterTitle), duration: const Duration(seconds: 2)),
      );
      return;
    }
    if (_selectedAccount.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.pleaseSelectAccount), duration: const Duration(seconds: 2)),
      );
      return;
    }
    if (_members.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.pleaseAddMember), duration: const Duration(seconds: 2)),
      );
      return;
    }

    setState(() => _creating = true);

    // Build members list: self + all remote members
    final allMembers = [_selectedAccount, ..._members];
    native.EmailCore.logWrite('[CREATE_SESSION] Creating unified session: account=$_selectedAccount members=$allMembers title=$title');

    // Create unified session via UnifiedSessionManager (auto-routes Signal/MLS)
    String createResult;
    try {
      createResult = UnifiedSessionManager.createSession(
          _selectedAccount, title, allMembers,
          mailmen: [_selectedAccount, ..._mailmen.where((m) => m != _selectedAccount)]);
      native.EmailCore.logWrite('[CREATE_SESSION] createSession result: $createResult');
    } catch (e) {
      native.EmailCore.logWrite('[CREATE_SESSION] createSession exception: $e');
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('${AppStrings.createEmailInstanceFailed}: $e'), duration: const Duration(seconds: 3)),
        );
      }
      return;
    }

    final createJson = jsonDecode(createResult);
    if (createJson['status'] != 'success') {
      setState(() => _creating = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('${AppStrings.createEmailInstanceFailed}: ${createJson['error'] ?? 'unknown'}'), duration: const Duration(seconds: 3)),
      );
      return;
    }

    final messageId = createJson['message_id']?.toString() ?? '';
    final xMailer = createJson['x_mailer']?.toString() ?? '';
    final sessionId = createJson['session_id']?.toString() ?? '';
    final encryptedBody = createJson['encrypted_body']?.toString() ?? '';

    // Determine if this is a Signal or MLS session based on member count.
    // Signal (2 members): 创建即密钥交换 — 发明文 PREKEY_BUNDLE(1.0.0)，body 是 prekey bundle JSON。
    // MLS (3+ members): group_create already set up the group — pass x_mailer for task processing.
    final isSignal = (_members.length == 1); // 1 remote member = 2 total = Signal
    final bodyForSmtp = encryptedBody;  // Signal: prekey bundle JSON; MLS: group encrypted body
    final sessionIdForSmtp = isSignal ? '' : sessionId;
    final xMailerForSmtp = isSignal ? '1.0.0' : xMailer;  // Signal: PREKEY_BUNDLE

    // Load config for SMTP send
    final config = native.EmailCore.loadConfig(widget.configPath);
    if (config == null || config.accounts.isEmpty) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.cannotLoadConfig), duration: const Duration(seconds: 2)),
        );
      }
      return;
    }

    final accountData = config.accounts.where((a) => a.email == _selectedAccount).firstOrNull;
    if (accountData == null) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.accountNotFound), duration: const Duration(seconds: 2)),
        );
      }
      return;
    }

    // Build SMTP content — recipients exclude self
    final recipientStr = _members.join(', ');
    final content = jsonEncode({
      'recipient': recipientStr,
      'subject': title,
      'body': bodyForSmtp,
      'in_reply_to': '',
      'message_id': messageId,
      'session_id': sessionIdForSmtp,
      'x_session_chart': xMailerForSmtp,
      'encrypt_method': 0,  // PREKEY_BUNDLE 是明文发送，不需要 SMTP 层加密
      'members': _members.join(','),
    });

    native.EmailCore.logWrite('[CREATE_SESSION] Sending email to: $recipientStr, subject: $title');

    bool sendOk = false;
    String sendError = '';
    try {
      await Future.delayed(const Duration(milliseconds: 50));

      int ci;
      if (accountData.type == 'outlook.com') {
        ci = native.EmailCore.oemailimAddOutlookEmail();
      } else {
        ci = native.EmailCore.oemailimOpenNewEmail(accountData.id);
      }
      native.EmailCore.logWrite('[CREATE_SESSION] configIndex: $ci');

      if (ci < 0) {
        sendError = '${AppStrings.createEmailInstanceFailed} ($ci)';
      } else {
        native.EmailCore.setEmailCredentials(ci, accountData.email, accountData.authCode);
        native.EmailCore.oemailimSetImapServer(ci, accountData.imapServer, accountData.imapPort);
        native.EmailCore.oemailimSetSmtpServer(ci, accountData.smtpServer, accountData.smtpPort);
        native.EmailCore.setRefreshToken(ci, accountData.authCode);

        final sendResult = native.EmailCore.sendViaConfigRaw(ci, content);
        native.EmailCore.logWrite('[CREATE_SESSION] sendViaConfig result: $sendResult');
        sendOk = sendResult == 0;
        if (!sendOk) {
          sendError = native.EmailCore.getLastError(ci);
        }
      }
    } catch (e) {
      sendError = '$e';
      native.EmailCore.logWrite('[CREATE_SESSION] Exception: $sendError');
    }

    if (!sendOk) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('${AppStrings.emailSendFailed}: $sendError'), duration: const Duration(seconds: 5)),
        );
      }
      return;
    }

    native.EmailCore.logWrite('[CREATE_SESSION] Unified session created and email sent, session_id=${createJson['session_id']}');

    setState(() => _creating = false);

    if (widget.onCreated != null) widget.onCreated!();
    if (mounted) Navigator.of(context).pop();
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.sessionCreatedSuccess), duration: const Duration(seconds: 2)),
      );
    }
  }

  @override
  void dispose() {
    _titleController.dispose();
    _membersController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      scrollable: true,
      title: Text(AppStrings.newSession),
      content: SizedBox(
        width: 440,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 0. 会话标题
            Text(AppStrings.sessionTitle, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            const SizedBox(height: 6),
            TextField(
              controller: _titleController,
              decoration: InputDecoration(
                hintText: AppStrings.enterSessionTitle,
                hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              ),
            ),
            const SizedBox(height: 16),

            // 1. 选择账户
            Text(AppStrings.selectAccount, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            const SizedBox(height: 6),
            DropdownButtonFormField<String>(
              value: _selectedAccount,
              decoration: InputDecoration(
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              ),
              items: widget.accounts
                  .map((acc) => DropdownMenuItem(value: acc, child: Text(acc)))
                  .toList(),
              onChanged: (v) {
                if (v != null) setState(() => _selectedAccount = v);
              },
            ),
            const SizedBox(height: 16),

            // 2. 添加会话成员
            Text(AppStrings.sessionMembers, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            const SizedBox(height: 6),
            Row(
              children: [
                Expanded(
                  child: Autocomplete<String>(
                    fieldViewBuilder: (context, controller, focusNode, onFieldSubmitted) {
                      _membersController = controller;
                      return TextField(
                        controller: controller,
                        focusNode: focusNode,
                        decoration: InputDecoration(
                          hintText: AppStrings.enterEmail,
                          hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                          contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                        ),
                        onSubmitted: (_) {
                          _addMember();
                          onFieldSubmitted();
                        },
                      );
                    },
                    optionsBuilder: (TextEditingValue textEditingValue) {
                      if (textEditingValue.text.isEmpty) {
                        return widget.accounts.where((a) => !_members.contains(a));
                      }
                      final input = textEditingValue.text.toLowerCase();
                      return widget.accounts
                          .where((a) => a.toLowerCase().contains(input) && !_members.contains(a));
                    },
                    onSelected: (String selection) {
                      setState(() {
                        if (!_members.contains(selection)) {
                          _members.add(selection);
                        }
                        _membersController.clear();
                      });
                    },
                    optionsViewBuilder: (context, onSelected, options) {
                      return Align(
                        alignment: Alignment.topLeft,
                        child: Material(
                          elevation: 4,
                          borderRadius: BorderRadius.circular(6),
                          child: ConstrainedBox(
                            constraints: const BoxConstraints(maxHeight: 200),
                            child: ListView.builder(
                              shrinkWrap: true,
                              itemCount: options.length,
                              itemBuilder: (context, index) {
                                final option = options.elementAt(index);
                                return ListTile(
                                  dense: true,
                                  title: Text(option, style: const TextStyle(fontSize: 13)),
                                  onTap: () => onSelected(option),
                                );
                              },
                            ),
                          ),
                        ),
                      );
                    },
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  onPressed: _addMember,
                  icon: const Icon(Icons.add_circle, color: Colors.blue),
                ),
              ],
            ),
            const SizedBox(height: 8),
            if (_members.isNotEmpty)
              Wrap(
                spacing: 6,
                runSpacing: 4,
                children: _members.map((member) {
                  return Chip(
                    label: Text(member, style: const TextStyle(fontSize: 12)),
                    deleteIcon: const Icon(Icons.close, size: 16),
                    onDeleted: () => _removeMember(member),
                  );
                }).toList(),
              ),
            if (_members.isEmpty)
              Text(AppStrings.noMembers, style: TextStyle(fontSize: 12, color: Colors.grey[400])),
            const SizedBox(height: 12),

            // 3. 发送邮差池（多邮箱轮流发送，绕过单邮箱频率限制）
            Text(AppStrings.mailmanPool, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            const SizedBox(height: 6),
            Wrap(
              spacing: 8,
              runSpacing: 4,
              children: widget.accounts
                  .where((a) => a != _selectedAccount) // 主邮箱默认发送，不显示在池子里
                  .map((acc) {
                final checked = _mailmen.contains(acc);
                return FilterChip(
                  label: Text(acc, style: const TextStyle(fontSize: 12)),
                  selected: checked,
                  onSelected: (v) {
                    setState(() {
                      if (v) {
                        _mailmen.add(acc);
                      } else {
                        _mailmen.remove(acc);
                      }
                    });
                  },
                );
              }).toList(),
            ),
            Text(AppStrings.mailmanPoolHint, style: TextStyle(fontSize: 11, color: Colors.grey[500])),
            const SizedBox(height: 8),


          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(AppStrings.cancel),
        ),
        ElevatedButton(
          onPressed: _creating ? null : _createSession,
          child: _creating
              ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : Text(AppStrings.create),
        ),
      ],
    );
  }
}
