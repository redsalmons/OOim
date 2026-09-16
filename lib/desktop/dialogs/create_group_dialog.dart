import 'dart:convert';
import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import '../../i18n/app_strings.dart';

class CreateGroupDialog extends StatefulWidget {
  final List<String> accounts;
  final String configPath;
  final VoidCallback? onCreated;
  final String? initialTitle;
  final String? initialAccount;
  final List<String>? initialMembers;

  const CreateGroupDialog({
    super.key,
    required this.accounts,
    required this.configPath,
    this.onCreated,
    this.initialTitle,
    this.initialAccount,
    this.initialMembers,
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
    _selectedAccount = widget.initialAccount ?? (widget.accounts.isNotEmpty ? widget.accounts.first : '');
    if (widget.initialTitle != null) {
      _titleController.text = widget.initialTitle!;
    }
    if (widget.initialMembers != null) {
      _members.addAll(widget.initialMembers!);
    }
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

    // MLS group creation: the C++ side creates the group, sends the invite to all
    // members, and queues the tasks. Members reply with KeyPackages, the owner
    // sends Welcomes — all handled by group_create / group_handle_incoming.
    final createResult = native.EmailCore.groupCreate(
      _selectedAccount, title, allMembers);
    native.EmailCore.logWrite('[CREATE_GROUP] groupCreate result: $createResult');

    final createJson = jsonDecode(createResult);
    if (createJson['status'] != 'success') {
      setState(() => _creating = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('创建群组失败: ${createJson['error'] ?? 'unknown'}'), duration: const Duration(seconds: 2)),
      );
      return;
    }

    setState(() => _creating = false);

    if (widget.onCreated != null) widget.onCreated!();
    if (mounted) Navigator.of(context).pop();
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('群组创建成功，等待成员加入...'), duration: const Duration(seconds: 2)),
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
