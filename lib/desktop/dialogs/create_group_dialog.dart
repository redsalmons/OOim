import 'dart:convert';
import 'dart:math';
import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import '../../i18n/app_strings.dart';

class CreateGroupDialog extends StatefulWidget {
  final List<String> accounts;
  final String configPath;
  final VoidCallback? onCreated;

  const CreateGroupDialog({
    super.key,
    required this.accounts,
    required this.configPath,
    this.onCreated,
  });

  @override
  State<CreateGroupDialog> createState() => _CreateGroupDialogState();
}

class _CreateGroupDialogState extends State<CreateGroupDialog> {
  late String _selectedAccount;
  final _titleController = TextEditingController();
  TextEditingController _membersController = TextEditingController();
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

  Future<void> _createGroup() async {
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

    final allMembers = <String>[_selectedAccount, ..._members];

    native.EmailCore.logWrite('[CREATE_GROUP] members=$allMembers');

    // 1. Create group session in DB (group_id and group_email are auto-generated)
    final createResult = native.EmailCore.groupCreate(
      _selectedAccount, '', '', title, allMembers);
    native.EmailCore.logWrite('[CREATE_GROUP] groupCreate result: $createResult');

    final createJson = jsonDecode(createResult);
    if (createJson['status'] != 'success') {
      setState(() => _creating = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('创建群组失败: ${createJson['error'] ?? 'unknown'}'), duration: const Duration(seconds: 2)),
      );
      return;
    }
    final groupId = createJson['group_id'].toString();

    // 2. Generate our own Sender Key
    final skResult = native.EmailCore.senderKeyGenerate(_selectedAccount, groupId);
    native.EmailCore.logWrite('[CREATE_GROUP] senderKeyGenerate result: $skResult');

    // 3. Get distribution payload
    final distPayload = native.EmailCore.senderKeyGetDistribution(_selectedAccount, groupId);
    native.EmailCore.logWrite('[CREATE_GROUP] distribution payload: $distPayload');
    final distPayloadJson = jsonDecode(distPayload);

    // 4. Distribute to each member via 1:1 DR
    // The creator's SENDER_KEY_DIST is the root of the group's reply chain:
    //   x_message_id = locally generated id
    //   in_reply_to  = '' (root)
    // send_email() performs the actual Signal encryption; we just queue the plaintext wrapper
    String rootMessageId = '';
    for (final member in _members) {
      if (member == _selectedAccount) continue;
      try {
        final sessionId = _getActiveSessionId(_selectedAccount, member);
        final messageId = _generateMessageId(_selectedAccount);
        if (rootMessageId.isEmpty) rootMessageId = messageId;

        final distWrapper = jsonEncode({
          'type': 'sender_key_distribution',
          'x_message_id': messageId,
          'x_reply_to': '',
          'subject': title,
          'members': allMembers,
          'owner': _selectedAccount,
          'sender_key': distPayloadJson,
        });

        final rc = native.EmailCore.taskInsert(
          account: _selectedAccount,
          recipient: member,
          subject: title,
          body: distWrapper,
          inReplyTo: '',
          messageId: messageId,
          sessionId: sessionId,
          xSessionChart: native.XMailer.senderKeyDist,
        );
        native.EmailCore.logWrite('[CREATE_GROUP] SenderKey queued to $member (session=$sessionId) task rc=$rc');
      } catch (e) {
        native.EmailCore.logWrite('[CREATE_GROUP] Failed to distribute to $member: $e');
      }
    }

    // 5. Record the root message_id on the group session
    if (rootMessageId.isNotEmpty) {
      final setXResult = native.EmailCore.groupSetXReplyId(_selectedAccount, groupId, rootMessageId);
      native.EmailCore.logWrite('[CREATE_GROUP] groupSetXReplyId(root=$rootMessageId) result: $setXResult');
    }

    setState(() => _creating = false);

    if (widget.onCreated != null) widget.onCreated!();
    if (mounted) Navigator.of(context).pop();
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('群组创建成功'), duration: const Duration(seconds: 2)),
      );
    }
  }

  String _getActiveSessionId(String account, String peerEmail) {
    try {
      final sid = native.EmailCore.groupFind1to1Session(account, peerEmail);
      native.EmailCore.logWrite('[CREATE_GROUP] _getActiveSessionId($peerEmail) = "$sid"');
      return sid;
    } catch (e) {
      native.EmailCore.logWrite('[CREATE_GROUP] _getActiveSessionId($peerEmail) error: $e');
      return '';
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
      title: const Text('创建群组'),
      content: SizedBox(
        width: 420,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Group title
            TextField(
              controller: _titleController,
              decoration: const InputDecoration(
                labelText: '群组名称',
                hintText: '输入群组名称',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 16),

            // Account selector
            DropdownButtonFormField<String>(
              value: _selectedAccount,
              decoration: const InputDecoration(
                labelText: '发送账号',
                border: OutlineInputBorder(),
              ),
              items: widget.accounts.map((a) => DropdownMenuItem(value: a, child: Text(a))).toList(),
              onChanged: (v) => setState(() => _selectedAccount = v ?? ''),
            ),
            const SizedBox(height: 16),

            // Members input
            const Text('群成员', style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
            const SizedBox(height: 8),

            // Member input: autocomplete from existing accounts + manual add
            Row(
              children: [
                Expanded(
                  child: Autocomplete<String>(
                    fieldViewBuilder: (context, controller, focusNode, onFieldSubmitted) {
                      _membersController = controller;
                      return TextField(
                        controller: controller,
                        focusNode: focusNode,
                        decoration: const InputDecoration(
                          hintText: '输入或选择成员邮箱',
                          border: OutlineInputBorder(),
                          isDense: true,
                        ),
                        onSubmitted: (_) {
                          _addMember();
                          onFieldSubmitted();
                        },
                      );
                    },
                    optionsBuilder: (TextEditingValue textEditingValue) {
                      if (textEditingValue.text.isEmpty) {
                        return widget.accounts.where((a) => a != _selectedAccount && !_members.contains(a));
                      }
                      final input = textEditingValue.text.toLowerCase();
                      return widget.accounts
                          .where((a) => a.toLowerCase().contains(input) && a != _selectedAccount && !_members.contains(a));
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

            // Member chips
            Wrap(
              spacing: 8,
              runSpacing: 4,
              children: _members.map((m) => Chip(
                label: Text(m, style: const TextStyle(fontSize: 12)),
                deleteIcon: const Icon(Icons.close, size: 16),
                onDeleted: () => _removeMember(m),
              )).toList(),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: _creating ? null : () => Navigator.of(context).pop(),
          child: Text(AppStrings.cancel),
        ),
        ElevatedButton(
          onPressed: _creating ? null : _createGroup,
          style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF07C160)),
          child: _creating
              ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
              : const Text('创建', style: TextStyle(color: Colors.white)),
        ),
      ],
    );
  }
}
