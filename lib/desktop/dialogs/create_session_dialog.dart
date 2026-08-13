import 'dart:convert';
import 'dart:math';
import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;

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
  final _membersController = TextEditingController();
  final List<String> _members = [];
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

  String _generateMessageId(String account) {
    final ts = DateTime.now().millisecondsSinceEpoch;
    final rand = Random().nextInt(0xFFFFFF);
    final domain = account.split('@').last;
    return '<$ts.$rand@$domain>';
  }

  Future<void> _createSession() async {
    final title = _titleController.text.trim();
    if (title.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请输入会话标题'), duration: Duration(seconds: 2)),
      );
      return;
    }
    if (_selectedAccount.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请选择账户'), duration: Duration(seconds: 2)),
      );
      return;
    }
    if (_members.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('请添加至少一个会话成员'), duration: Duration(seconds: 2)),
      );
      return;
    }

    setState(() => _creating = true);

    // 1. Generate message_id
    final messageId = _generateMessageId(_selectedAccount);
    native.EmailCore.logWrite('[CREATE_SESSION] Generated message_id: $messageId');

    // 2. Load config to get account details for sending
    final config = native.EmailCore.loadConfig(widget.configPath);
    if (config == null || config.accounts.isEmpty) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('无法加载账户配置'), duration: Duration(seconds: 2)),
        );
      }
      return;
    }

    final accountData = config.accounts.where((a) => a.email == _selectedAccount).firstOrNull;
    if (accountData == null) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('未找到选中账户的配置'), duration: Duration(seconds: 2)),
        );
      }
      return;
    }

    // 3. Create session first (before sending email)
    final membersStr = _members.join(',');
    final createResult = native.EmailCore.createSession(_selectedAccount, title, membersStr, messageId);
    native.EmailCore.logWrite('[CREATE_SESSION] createSession result: $createResult');

    String sessionId = '';
    try {
      final decoded = jsonDecode(createResult);
      if (decoded['status'] != 'success') {
        if (mounted) {
          setState(() => _creating = false);
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('创建会话失败: ${decoded['error'] ?? '未知错误'}'), duration: const Duration(seconds: 2)),
          );
        }
        return;
      }
      sessionId = decoded['session_id']?.toString() ?? '';
    } catch (e) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('创建会话失败: $e'), duration: const Duration(seconds: 2)),
        );
      }
      return;
    }

    if (sessionId.isEmpty) {
      if (mounted) {
        setState(() => _creating = false);
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('创建会话失败: 未获取到 session_id'), duration: Duration(seconds: 2)),
        );
      }
      return;
    }

    native.EmailCore.logWrite('[CREATE_SESSION] Session created: $sessionId, now sending email...');

    // 4. Send email with session_id — C++ backend will handle insertSentEmail + addEmailToSession
    // Include self in recipients so self also receives a copy in INBOX
    final allRecipients = <String>{..._members, _selectedAccount};
    final recipientStr = allRecipients.join(', ');
    final content = jsonEncode({
      'recipient': recipientStr,
      'subject': title,
      'body': '',
      'in_reply_to': '',
      'message_id': messageId,
      'session_id': sessionId,
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
        sendError = '创建邮箱实例失败 ($ci)';
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
          SnackBar(content: Text('邮件发送失败: $sendError'), duration: const Duration(seconds: 5)),
        );
      }
      return;
    }

    native.EmailCore.logWrite('[CREATE_SESSION] Email sent successfully, session already created');

    setState(() => _creating = false);

    if (widget.onCreated != null) widget.onCreated!();
    if (mounted) Navigator.of(context).pop();
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('会话创建成功'), duration: Duration(seconds: 2)),
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
      title: const Text('新建会话'),
      content: SizedBox(
        width: 440,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 0. 会话标题
            const Text('会话标题', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            const SizedBox(height: 6),
            TextField(
              controller: _titleController,
              decoration: InputDecoration(
                hintText: '输入会话标题',
                hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              ),
            ),
            const SizedBox(height: 16),

            // 1. 选择账户
            const Text('选择要使用的账户', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
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
            const Text('会话成员', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            const SizedBox(height: 6),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _membersController,
                    decoration: InputDecoration(
                      hintText: '输入邮箱地址',
                      hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    ),
                    onSubmitted: (_) => _addMember(),
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
              Text('暂未添加成员', style: TextStyle(fontSize: 12, color: Colors.grey[400])),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        ElevatedButton(
          onPressed: _creating ? null : _createSession,
          child: _creating
              ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
              : const Text('创建'),
        ),
      ],
    );
  }
}
