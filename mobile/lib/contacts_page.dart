import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/email_core.dart' as native;

class _Contact {
  int id;
  String email;
  String name;
  String groupName;
  String notes;
  _Contact({required this.id, required this.email, required this.name,
      required this.groupName, required this.notes});
}

class ContactsPage extends StatefulWidget {
  const ContactsPage({super.key});
  @override
  State<ContactsPage> createState() => _ContactsPageState();
}

class _ContactsPageState extends State<ContactsPage> {
  final _searchCtrl = TextEditingController();
  final List<_Contact> _contacts = [];
  List<String> _groups = [];
  String? _selectedGroup;

  @override
  void initState() {
    super.initState();
    native.EmailCore.addressbookMigrate();
    _reload();
  }

  void _reload() {
    final jsonStr = native.EmailCore.addressbookQueryAll();
    if (jsonStr == null) return;
    try {
      final list = jsonDecode(jsonStr) as List;
      setState(() {
        _contacts
          ..clear()
          ..addAll(list.map((item) {
            final m = item as Map<String, dynamic>;
            return _Contact(
              id: m['id'] as int? ?? 0,
              email: m['email'] as String? ?? '',
              name: m['name'] as String? ?? '',
              groupName: m['group_name'] as String? ?? '',
              notes: m['notes'] as String? ?? '',
            );
          }));
      });
      final groupsJson = native.EmailCore.addressbookQueryGroups();
      if (groupsJson != null) {
        _groups = (jsonDecode(groupsJson) as List).cast<String>();
      }
    } catch (_) {}
  }

  List<_Contact> get _filtered {
    final q = _searchCtrl.text.trim().toLowerCase();
    return _contacts.where((c) {
      if (_selectedGroup != null && c.groupName != _selectedGroup) return false;
      if (q.isEmpty) return true;
      return c.name.toLowerCase().contains(q) || c.email.toLowerCase().contains(q);
    }).toList();
  }

  void _openEditor({_Contact? existing}) async {
    final changed = await Navigator.of(context).push<bool>(MaterialPageRoute(
      builder: (_) => ContactEditPage(existing: existing),
    ));
    if (changed == true) _reload();
  }

  void _deleteContact(_Contact c) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(AppStrings.isZh ? '删除联系人' : 'Delete contact'),
        content: Text(c.email),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: Text(AppStrings.cancel)),
          TextButton(
            onPressed: () {
              native.EmailCore.addressbookDelete(c.id);
              Navigator.pop(ctx);
              _reload();
            },
            child: Text(AppStrings.isZh ? '删除' : 'Delete', style: const TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final filtered = _filtered;
    return Scaffold(
      appBar: AppBar(
        title: Text(AppStrings.contacts),
        actions: [
          IconButton(icon: const Icon(Icons.add), onPressed: () => _openEditor()),
        ],
      ),
      body: Column(children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: TextField(
            controller: _searchCtrl,
            onChanged: (_) => setState(() {}),
            decoration: InputDecoration(
              hintText: AppStrings.searchContacts,
              prefixIcon: const Icon(Icons.search),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(10)),
              isDense: true,
            ),
          ),
        ),
        if (_groups.isNotEmpty)
          SizedBox(
            height: 40,
            child: ListView(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: 12),
              children: [
                for (final g in [null, ..._groups])
                  Padding(
                    padding: const EdgeInsets.only(right: 8),
                    child: ChoiceChip(
                      label: Text(g ?? (AppStrings.isZh ? '全部' : 'All')),
                      selected: _selectedGroup == g,
                      onSelected: (_) => setState(() => _selectedGroup = g),
                    ),
                  ),
              ],
            ),
          ),
        Expanded(
          child: filtered.isEmpty
              ? Center(child: Text(AppStrings.isZh ? '暂无联系人' : 'No contacts',
                  style: const TextStyle(color: Colors.grey)))
              : ListView.builder(
                  itemCount: filtered.length,
                  itemBuilder: (_, i) {
                    final c = filtered[i];
                    return ListTile(
                      leading: CircleAvatar(
                        child: Text(c.name.isNotEmpty ? c.name[0].toUpperCase() : '?'),
                      ),
                      title: Text(c.name.isNotEmpty ? c.name : c.email),
                      subtitle: Text(c.groupName.isNotEmpty ? '${c.email} · ${c.groupName}' : c.email),
                      trailing: IconButton(
                        icon: const Icon(Icons.delete_outline, size: 20),
                        onPressed: () => _deleteContact(c),
                      ),
                      onTap: () => _openEditor(existing: c),
                    );
                  },
                ),
        ),
      ]),
    );
  }
}

class ContactEditPage extends StatefulWidget {
  final _Contact? existing;
  const ContactEditPage({super.key, this.existing});
  @override
  State<ContactEditPage> createState() => _ContactEditPageState();
}

class _ContactEditPageState extends State<ContactEditPage> {
  late final _nameCtrl = TextEditingController(text: widget.existing?.name ?? '');
  late final _emailCtrl = TextEditingController(text: widget.existing?.email ?? '');
  late final _groupCtrl = TextEditingController(text: widget.existing?.groupName ?? '');
  late final _notesCtrl = TextEditingController(text: widget.existing?.notes ?? '');

  void _save() {
    final isZh = AppStrings.isZh;
    final email = _emailCtrl.text.trim();
    final name = _nameCtrl.text.trim();
    if (email.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(isZh ? '请输入邮箱地址' : 'Please enter an email address'),
        duration: const Duration(seconds: 2),
      ));
      return;
    }
    try {
      if (widget.existing == null) {
        native.EmailCore.addressbookAddEmail(email, name.isEmpty ? email : name);
      }
      int id = widget.existing?.id ?? 0;
      if (id == 0) {
        final jsonStr = native.EmailCore.addressbookQueryAll();
        if (jsonStr != null) {
          for (final item in jsonDecode(jsonStr) as List) {
            if ((item as Map<String, dynamic>)['email'] == email) {
              id = item['id'] as int? ?? 0;
              break;
            }
          }
        }
      }
      if (id == 0) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('${AppStrings.saveFailed}: add'),
          duration: const Duration(seconds: 2),
        ));
        return;
      }
      final rc = native.EmailCore.addressbookUpdate(
          id, name.isEmpty ? email : name, _groupCtrl.text.trim(), _notesCtrl.text.trim());
      if (rc < 0) {
        ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text('${AppStrings.saveFailed}: $rc'),
          duration: const Duration(seconds: 2),
        ));
        return;
      }
      Navigator.of(context).pop(true);
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text('${AppStrings.saveFailed}: $e'),
        duration: const Duration(seconds: 2),
      ));
    }
  }

  @override
  Widget build(BuildContext context) {
    final isZh = AppStrings.isZh;
    final isNew = widget.existing == null;
    return Scaffold(
      appBar: AppBar(title: Text(isNew ? (isZh ? '添加联系人' : 'Add contact')
                                        : (isZh ? '编辑联系人' : 'Edit contact'))),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          TextField(controller: _nameCtrl, decoration: InputDecoration(labelText: AppStrings.contactName)),
          const SizedBox(height: 12),
          TextField(
            controller: _emailCtrl,
            enabled: isNew,
            keyboardType: TextInputType.emailAddress,
            decoration: InputDecoration(labelText: AppStrings.contactEmail),
          ),
          const SizedBox(height: 12),
          TextField(controller: _groupCtrl, decoration: InputDecoration(labelText: AppStrings.contactCategory)),
          const SizedBox(height: 12),
          TextField(controller: _notesCtrl, maxLines: 3, decoration: InputDecoration(labelText: AppStrings.contactNotes)),
          const SizedBox(height: 24),
          FilledButton(onPressed: _save, child: Text(AppStrings.save)),
        ],
      ),
    );
  }
}
