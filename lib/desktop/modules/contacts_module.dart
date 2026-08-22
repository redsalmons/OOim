import 'dart:convert';
import 'package:flutter/material.dart';
import '../../i18n/app_strings.dart';
import '../../native/email_core.dart' as native;

class ContactsModule extends StatefulWidget {
  const ContactsModule({super.key});

  @override
  State<ContactsModule> createState() => ContactsModuleState();
}

class ContactsModuleState extends State<ContactsModule> {
  final TextEditingController _searchController = TextEditingController();
  String _selectedGroup = '';
  late List<String> _groups;
  AddressBookEntry? _selectedContact;
  bool _saving = false;
  final List<AddressBookEntry> _contacts = [];

  @override
  void initState() {
    super.initState();
    _groups = [AppStrings.allContacts];
    _selectedGroup = _groups[0];
    // Migrate contacts from existing emails into addressbook
    native.EmailCore.addressbookMigrate();
    _loadContactsFromDb();
  }

  void reloadFromDb() {
    _loadContactsFromDb();
  }

  void _loadContactsFromDb() {
    final jsonStr = native.EmailCore.addressbookQueryAll();
    if (jsonStr == null) return;
    try {
      final list = jsonDecode(jsonStr) as List;
      setState(() {
        _contacts.clear();
        for (final item in list) {
          final m = item as Map<String, dynamic>;
          _contacts.add(AddressBookEntry(
            id: m['id'] as int? ?? 0,
            email: m['email'] as String? ?? '',
            name: m['name'] as String? ?? '',
            groupName: m['group_name'] as String? ?? '',
            notes: m['notes'] as String? ?? '',
          ));
        }
      });
      _loadGroups();
    } catch (e) {
      print('Failed to load addressbook: $e');
    }
  }

  void _loadGroups() {
    final jsonStr = native.EmailCore.addressbookQueryGroups();
    if (jsonStr == null) return;
    try {
      final list = jsonDecode(jsonStr) as List;
      setState(() {
        _groups = [AppStrings.allContacts];
        for (final item in list) {
          final g = item as String;
          if (!_groups.contains(g)) _groups.add(g);
        }
      });
    } catch (_) {}
  }

  void _deleteContact() {
    final contact = _selectedContact;
    if (contact == null) return;
    final result = native.EmailCore.addressbookDelete(contact.id);
    if (result == 0) {
      setState(() { _selectedContact = null; });
      _loadContactsFromDb();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.isZh ? '联系人已删除' : 'Contact deleted'), duration: const Duration(seconds: 2)),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('${AppStrings.isZh ? '删除失败' : 'Delete failed'}: $result'), duration: const Duration(seconds: 2)),
      );
    }
  }

  void _saveContact(AddressBookEntry contact) {
    if (_saving) return;
    setState(() { _saving = true; });
    final result = native.EmailCore.addressbookUpdate(contact.id, contact.name, contact.groupName, contact.notes);
    setState(() { _saving = false; });
    if (result == 0) {
      _loadContactsFromDb();
      // Update selected contact to the refreshed object
      setState(() {
        _selectedContact = _contacts.where((c) => c.id == contact.id).firstOrNull;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.contactSaved), duration: const Duration(seconds: 2)),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('${AppStrings.saveFailed}: $result'), duration: const Duration(seconds: 2)),
      );
    }
  }

  List<AddressBookEntry> get _filteredContacts {
    final search = _searchController.text.trim().toLowerCase();
    var result = _contacts;
    if (_selectedGroup != _groups[0]) {
      result = result.where((c) => c.groupName == _selectedGroup).toList();
    }
    if (search.isNotEmpty) {
      result = result.where((c) =>
        c.name.toLowerCase().contains(search) ||
        c.email.toLowerCase().contains(search)
      ).toList();
    }
    return result;
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        _buildContactList(),
        _buildContactDetail(),
      ],
    );
  }

  Widget _buildContactList() {
    return Container(
      width: 320,
      color: const Color(0xFFF5F5F5),
      child: Column(
        children: [
          _buildSearchBar(),
          _buildGroupSelector(),
          Expanded(
            child: ListView.builder(
              itemCount: _filteredContacts.length,
              itemBuilder: (context, index) {
                final contact = _filteredContacts[index];
                return _buildContactItem(contact);
              },
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSearchBar() {
    return Container(
      padding: const EdgeInsets.all(12),
      child: Container(
        height: 36,
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(10),
        ),
        child: TextField(
          controller: _searchController,
          onChanged: (_) => setState(() {}),
          decoration: InputDecoration(
            hintText: AppStrings.searchContacts,
            hintStyle: const TextStyle(fontSize: 14),
            prefixIcon: const Icon(Icons.search, size: 20, color: Colors.grey),
            border: InputBorder.none,
            contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
          ),
        ),
      ),
    );
  }

  Widget _buildGroupSelector() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        child: Row(
          children: _groups.map((group) {
            final isSelected = _selectedGroup == group;
            return Padding(
              padding: const EdgeInsets.only(right: 8),
              child: GestureDetector(
                onTap: () {
                  setState(() {
                    _selectedGroup = group;
                  });
                },
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: isSelected ? Theme.of(context).colorScheme.primary : Colors.white,
                    borderRadius: BorderRadius.circular(16),
                    border: Border.all(
                      color: isSelected ? Theme.of(context).colorScheme.primary : Colors.grey[300]!,
                    ),
                  ),
                  child: Text(
                    group,
                    style: TextStyle(
                      fontSize: 13,
                      color: isSelected ? Colors.white : Colors.grey[700],
                    ),
                  ),
                ),
              ),
            );
          }).toList(),
        ),
      ),
    );
  }

  Widget _buildContactItem(AddressBookEntry contact) {
    final isSelected = _selectedContact?.id == contact.id;
    final primary = Theme.of(context).colorScheme.primary;
    final displayName = contact.name.isNotEmpty ? contact.name : contact.email;
    return GestureDetector(
      onTap: () {
        setState(() { _selectedContact = contact; });
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: isSelected ? primary.withValues(alpha: 0.08) : Colors.transparent,
          border: Border(bottom: BorderSide(color: Colors.grey[200]!, width: 0.5)),
        ),
        child: Row(
          children: [
            _buildAvatar(displayName),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(displayName, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500), maxLines: 1, overflow: TextOverflow.ellipsis),
                  const SizedBox(height: 4),
                  Text(contact.email, style: TextStyle(fontSize: 12, color: Colors.grey[600]), maxLines: 1, overflow: TextOverflow.ellipsis),
                ],
              ),
            ),
            Icon(Icons.chevron_right, color: Colors.grey[400], size: 20),
          ],
        ),
      ),
    );
  }

  Widget _buildAvatar(String name) {
    final initial = name.isNotEmpty ? name[0].toUpperCase() : '?';
    return Container(
      width: 45,
      height: 45,
      decoration: BoxDecoration(
        gradient: const LinearGradient(colors: [Color(0xFFFF9966), Color(0xFFFF5E62)], begin: Alignment.topLeft, end: Alignment.bottomRight),
        borderRadius: BorderRadius.circular(22.5),
      ),
      child: Center(child: Text(initial, style: const TextStyle(color: Colors.white, fontSize: 16))),
    );
  }

  Widget _buildContactDetail() {
    return Expanded(
      child: Container(
        color: Colors.white,
        child: _selectedContact == null
          ? Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(
                    Icons.contacts,
                    size: 80,
                    color: Colors.grey[300],
                  ),
                  const SizedBox(height: 16),
                  Text(
                    AppStrings.isZh ? '选择联系人查看详情' : 'Select a contact to view details',
                    style: TextStyle(
                      fontSize: 16,
                      color: Colors.grey[600],
                    ),
                  ),
                ],
              ),
            )
          : _AddressBookEditPanel(
              key: ValueKey('contact_${_selectedContact!.id}'),
              contact: _selectedContact!,
              existingGroups: _groups.skip(1).toList(),
              onSave: _saveContact,
              onDelete: _deleteContact,
            ),
      ),
    );
  }
}

class AddressBookEntry {
  final int id;
  final String email;
  final String name;
  final String groupName;
  final String notes;

  AddressBookEntry({
    required this.id,
    required this.email,
    required this.name,
    required this.groupName,
    required this.notes,
  });
}

class _AddressBookEditPanel extends StatefulWidget {
  final AddressBookEntry contact;
  final List<String> existingGroups;
  final void Function(AddressBookEntry) onSave;
  final VoidCallback onDelete;

  const _AddressBookEditPanel({
    super.key,
    required this.contact,
    required this.existingGroups,
    required this.onSave,
    required this.onDelete,
  });

  @override
  State<_AddressBookEditPanel> createState() => _AddressBookEditPanelState();
}

class _AddressBookEditPanelState extends State<_AddressBookEditPanel> {
  late final TextEditingController _emailController;
  late final TextEditingController _nameController;
  late final TextEditingController _notesController;
  late final TextEditingController _groupController;
  String _selectedGroup = '';
  bool _saved = false;

  @override
  void initState() {
    super.initState();
    _emailController = TextEditingController(text: widget.contact.email);
    _nameController = TextEditingController(text: widget.contact.name);
    _notesController = TextEditingController(text: widget.contact.notes);
    _selectedGroup = widget.contact.groupName;
    _groupController = TextEditingController();
  }

  @override
  void dispose() {
    _emailController.dispose();
    _nameController.dispose();
    _notesController.dispose();
    _groupController.dispose();
    super.dispose();
  }

  void _save() {
    if (_saved) return;
    _saved = true;
    final name = _nameController.text.trim();
    final notes = _notesController.text.trim();

    if (name.isEmpty) {
      _saved = false;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.enterName), duration: const Duration(seconds: 2)),
      );
      return;
    }

    widget.onSave(AddressBookEntry(
      id: widget.contact.id,
      email: widget.contact.email,
      name: name,
      groupName: _selectedGroup,
      notes: notes,
    ));
  }

  Widget _buildLabel(String text) {
    return Text(text, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500));
  }

  Widget _buildField(TextEditingController controller, String hint, {int maxLines = 1, bool readOnly = false}) {
    return TextField(
      controller: controller,
      maxLines: maxLines,
      readOnly: readOnly,
      decoration: InputDecoration(
        hintText: hint,
        hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        filled: readOnly,
        fillColor: readOnly ? Colors.grey[100] : null,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(32),
      child: SingleChildScrollView(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _buildLabel(AppStrings.contactEmail),
            const SizedBox(height: 6),
            _buildField(_emailController, '', readOnly: true),
            const SizedBox(height: 16),

            _buildLabel(AppStrings.contactName),
            const SizedBox(height: 6),
            _buildField(_nameController, AppStrings.enterName),
            const SizedBox(height: 16),

            _buildLabel(AppStrings.isZh ? '分组' : 'Group'),
            const SizedBox(height: 6),
            Row(
              children: [
                Expanded(
                  child: DropdownButtonFormField<String>(
                    value: _selectedGroup.isNotEmpty && widget.existingGroups.contains(_selectedGroup)
                      ? _selectedGroup : null,
                    decoration: InputDecoration(
                      hintText: AppStrings.isZh ? '选择分组' : 'Select group',
                      hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    ),
                    items: [
                      ...widget.existingGroups.map((g) => DropdownMenuItem(
                        value: g,
                        child: Text(g, style: const TextStyle(fontSize: 13)),
                      )),
                      if (_selectedGroup.isNotEmpty && !widget.existingGroups.contains(_selectedGroup))
                        DropdownMenuItem(
                          value: _selectedGroup,
                          child: Text(_selectedGroup, style: const TextStyle(fontSize: 13)),
                        ),
                    ],
                    onChanged: (value) {
                      setState(() { _selectedGroup = value ?? ''; });
                    },
                  ),
                ),
                const SizedBox(width: 8),
                SizedBox(
                  width: 200,
                  child: TextField(
                    controller: _groupController,
                    decoration: InputDecoration(
                      hintText: AppStrings.isZh ? '或输入新分组' : 'Or type new group',
                      hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    ),
                    onSubmitted: (value) {
                      final trimmed = value.trim();
                      if (trimmed.isNotEmpty) {
                        setState(() { _selectedGroup = trimmed; _groupController.clear(); });
                      }
                    },
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  onPressed: () {
                    final trimmed = _groupController.text.trim();
                    if (trimmed.isNotEmpty) {
                      setState(() { _selectedGroup = trimmed; _groupController.clear(); });
                    }
                  },
                  icon: const Icon(Icons.add_circle, size: 22),
                  color: Theme.of(context).colorScheme.primary,
                ),
              ],
            ),
            if (_selectedGroup.isNotEmpty)
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: Chip(
                  label: Text(_selectedGroup, style: const TextStyle(fontSize: 12)),
                  onDeleted: () { setState(() { _selectedGroup = ''; }); },
                ),
              ),
            const SizedBox(height: 16),

            _buildLabel(AppStrings.contactNotes),
            const SizedBox(height: 6),
            _buildField(_notesController, AppStrings.enterNotes, maxLines: 3),
            const SizedBox(height: 24),

            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                TextButton(
                  onPressed: widget.onDelete,
                  style: TextButton.styleFrom(foregroundColor: Colors.red),
                  child: Text(AppStrings.delete),
                ),
                const SizedBox(width: 8),
                ElevatedButton(
                  onPressed: _save,
                  child: Text(AppStrings.save),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
