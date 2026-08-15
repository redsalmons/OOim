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
  late final List<String> _groups;
  Contact? _selectedContact;
  bool _isNewContact = false;
  bool _saving = false;

  @override
  void initState() {
    super.initState();
    _groups = [
      AppStrings.allContacts,
      AppStrings.isZh ? '我的好友' : 'Friends',
      AppStrings.isZh ? '同事' : 'Colleagues',
      AppStrings.isZh ? '家人' : 'Family',
      AppStrings.isZh ? '重要客户' : 'VIP Clients',
    ];
    _selectedGroup = _groups[0];
    _loadContactsFromDb();
  }

  void reloadFromDb() {
    _loadContactsFromDb();
  }

  void _loadContactsFromDb() {
    final jsonStr = native.EmailCore.contactQueryAll();
    if (jsonStr == null) return;
    try {
      final list = jsonDecode(jsonStr) as List;
      setState(() {
        _contacts.clear();
        for (final item in list) {
          final m = item as Map<String, dynamic>;
          final cats = (m['categories'] as String? ?? '').split(',').where((s) => s.trim().isNotEmpty).map((s) => s.trim()).toList();
          _contacts.add(Contact(
            id: m['id'] as int? ?? 0,
            name: m['name'] as String? ?? '',
            phone: '',
            email: m['email'] as String? ?? '',
            avatar: (m['name'] as String? ?? '?').isNotEmpty ? (m['name'] as String? ?? '?')[0] : '?',
            categories: cats,
            status: 'offline',
            notes: m['notes'] as String? ?? '',
            key: m['key'] as String? ?? '',
          ));
          _allCategories.addAll(cats);
        }
        for (final cat in _allCategories) {
          if (!_groups.contains(cat)) {
            _groups.add(cat);
          }
        }
      });
    } catch (e) {
      print('Failed to load contacts: $e');
    }
  }

  final List<Contact> _contacts = [];
  final Set<String> _allCategories = {};

  void _addNewContact() {
    setState(() {
      final newContact = Contact(
        id: 0,
        name: '',
        phone: '',
        email: '',
        avatar: '?',
        categories: [],
        status: 'offline',
        notes: '',
        key: '',
      );
      _contacts.insert(0, newContact);
      _selectedContact = newContact;
      _isNewContact = true;
    });
  }

  void _cancelNewContact() {
    setState(() {
      _contacts.removeWhere((c) => c.id == 0 && _isNewContact);
      _selectedContact = null;
      _isNewContact = false;
    });
  }

  void _deleteContact() {
    final contact = _selectedContact;
    if (contact == null || contact.id == 0) return;
    final result = native.EmailCore.contactDelete(contact.id);
    if (result == 0) {
      setState(() {
        _selectedContact = null;
      });
      _loadContactsFromDb();
      setState(() {
        _selectedContact = _contacts.isNotEmpty ? _contacts.first : null;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(AppStrings.isZh ? '联系人已删除' : 'Contact deleted'),
          duration: const Duration(seconds: 2),
        ),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('${AppStrings.isZh ? '删除失败' : 'Delete failed'}: $result'),
          duration: const Duration(seconds: 2),
        ),
      );
    }
  }

  void _saveNewContact(Contact contact) {
    if (_saving) return;
    setState(() {
      _saving = true;
    });
    final id = native.EmailCore.contactAdd(
      email: contact.email,
      name: contact.name,
      categories: contact.categories.join(','),
      notes: contact.notes,
      key: contact.key,
    );
    if (id > 0) {
      setState(() {
        _isNewContact = false;
        _selectedContact = null;
        _saving = false;
      });
      _loadContactsFromDb();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(AppStrings.contactSaved),
          duration: const Duration(seconds: 2),
        ),
      );
    } else {
      setState(() {
        _saving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('${AppStrings.saveFailed}: $id'),
          duration: const Duration(seconds: 2),
        ),
      );
    }
  }

  List<Contact> get _filteredContacts {
    if (_selectedGroup == _groups[0]) {
      return _contacts;
    }
    return _contacts.where((contact) => contact.categories.contains(_selectedGroup)).toList();
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
                return _buildContactItem(contact, index);
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
      child: Row(
        children: [
          Expanded(
            child: Container(
              height: 36,
              decoration: BoxDecoration(
                color: Colors.white,
                borderRadius: BorderRadius.circular(10),
              ),
              child: TextField(
                controller: _searchController,
                decoration: InputDecoration(
                  hintText: AppStrings.searchContacts,
                  hintStyle: const TextStyle(fontSize: 14),
                  prefixIcon: const Icon(Icons.search, size: 20, color: Colors.grey),
                  border: InputBorder.none,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                ),
              ),
            ),
          ),
          const SizedBox(width: 8),
          Container(
            width: 36,
            height: 36,
            decoration: BoxDecoration(
              color: Theme.of(context).colorScheme.primary,
              borderRadius: BorderRadius.circular(10),
            ),
            child: IconButton(
              onPressed: _addNewContact,
              icon: const Icon(Icons.add, size: 20, color: Colors.white),
              padding: EdgeInsets.zero,
              constraints: const BoxConstraints(minWidth: 36, minHeight: 36),
            ),
          ),
        ],
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

  Widget _buildContactItem(Contact contact, int index) {
    final isSelected = _selectedContact == contact;
    final primary = Theme.of(context).colorScheme.primary;
    return GestureDetector(
      onTap: () {
        setState(() {
          _selectedContact = contact;
          if (contact.id != 0 || !_isNewContact) {
            _isNewContact = false;
          }
        });
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        decoration: BoxDecoration(
          color: isSelected ? primary.withValues(alpha: 0.08) : Colors.transparent,
          border: Border(
            bottom: BorderSide(color: Colors.grey[200]!, width: 0.5),
          ),
        ),
        child: Row(
          children: [
            _buildAvatar(contact.avatar, contact.status),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    contact.name.isEmpty ? AppStrings.enterName : contact.name,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w500,
                      color: contact.name.isEmpty ? Colors.grey[400] : null,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    contact.email,
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.grey[600],
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ],
              ),
            ),
            Icon(Icons.chevron_right, color: Colors.grey[400], size: 20),
          ],
        ),
      ),
    );
  }

  Widget _buildAvatar(String name, String status) {
    Color statusColor;
    switch (status) {
      case 'online':
        statusColor = Colors.green;
        break;
      case 'busy':
        statusColor = Colors.orange;
        break;
      default:
        statusColor = Colors.grey;
    }

    return Stack(
      children: [
        Container(
          width: 45,
          height: 45,
          decoration: BoxDecoration(
            gradient: const LinearGradient(
              colors: [Color(0xFFFF9966), Color(0xFFFF5E62)],
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
            ),
            borderRadius: BorderRadius.circular(22.5),
          ),
          child: Center(
            child: Text(
              name,
              style: const TextStyle(color: Colors.white, fontSize: 16),
            ),
          ),
        ),
        Positioned(
          bottom: 0,
          right: 0,
          child: Container(
            width: 12,
            height: 12,
            decoration: BoxDecoration(
              color: statusColor,
              border: Border.all(color: Colors.white, width: 2),
              borderRadius: BorderRadius.circular(6),
            ),
          ),
        ),
      ],
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
          : _ContactEditPanel(
              key: ValueKey(_selectedContact!.id == 0 ? 'new_${_selectedContact.hashCode}' : 'contact_${_selectedContact!.id}'),
              contact: _selectedContact!,
              existingCategories: _groups.skip(1).toList(),
              isNew: _isNewContact,
              onSave: _saveNewContact,
              onDelete: _isNewContact ? _cancelNewContact : _deleteContact,
            ),
      ),
    );
  }
}

class Contact {
  final int id;
  final String name;
  final String phone;
  final String email;
  final String avatar;
  final List<String> categories;
  final String status;
  final String notes;
  final String key;

  Contact({
    required this.id,
    required this.name,
    required this.phone,
    required this.email,
    required this.avatar,
    required this.categories,
    required this.status,
    this.notes = '',
    this.key = '',
  });
}

class _ContactEditPanel extends StatefulWidget {
  final Contact contact;
  final List<String> existingCategories;
  final bool isNew;
  final void Function(Contact) onSave;
  final VoidCallback onDelete;

  const _ContactEditPanel({
    super.key,
    required this.contact,
    required this.existingCategories,
    required this.isNew,
    required this.onSave,
    required this.onDelete,
  });

  @override
  State<_ContactEditPanel> createState() => _ContactEditPanelState();
}

class _ContactEditPanelState extends State<_ContactEditPanel> {
  late final TextEditingController _emailController;
  late final TextEditingController _nameController;
  late final TextEditingController _notesController;
  final _categoryController = TextEditingController();
  late List<String> _selectedCategories;
  bool _saved = false;

  @override
  void initState() {
    super.initState();
    _emailController = TextEditingController(text: widget.contact.email);
    _nameController = TextEditingController(text: widget.contact.name);
    _notesController = TextEditingController(text: widget.contact.notes);
    _selectedCategories = List.from(widget.contact.categories);
  }

  @override
  void dispose() {
    _emailController.dispose();
    _nameController.dispose();
    _notesController.dispose();
    _categoryController.dispose();
    super.dispose();
  }

  void _addCategory(String cat) {
    final trimmed = cat.trim();
    if (trimmed.isNotEmpty && !_selectedCategories.contains(trimmed)) {
      setState(() {
        _selectedCategories.add(trimmed);
        _categoryController.clear();
      });
    }
  }

  void _removeCategory(String cat) {
    setState(() {
      _selectedCategories.remove(cat);
    });
  }

  void _save() {
    if (_saved) return;
    _saved = true;
    final email = _emailController.text.trim();
    final name = _nameController.text.trim();
    final notes = _notesController.text.trim();

    if (name.isEmpty) {
      _saved = false;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.enterName), duration: const Duration(seconds: 2)),
      );
      return;
    }
    if (email.isEmpty) {
      _saved = false;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.enterEmail), duration: const Duration(seconds: 2)),
      );
      return;
    }

    final avatar = name.isNotEmpty ? name[0] : '?';
    final contact = Contact(
      id: widget.contact.id,
      name: name,
      phone: '',
      email: email,
      avatar: avatar,
      categories: _selectedCategories.isNotEmpty ? List.from(_selectedCategories) : [AppStrings.isZh ? '我的好友' : 'Friends'],
      status: 'offline',
      notes: notes,
      key: widget.contact.key,
    );

    widget.onSave(contact);
  }

  Widget _buildLabel(String text) {
    return Text(text, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500));
  }

  Widget _buildField(TextEditingController controller, String hint, {int maxLines = 1}) {
    return TextField(
      controller: controller,
      maxLines: maxLines,
      decoration: InputDecoration(
        hintText: hint,
        hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
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
            _buildField(_emailController, AppStrings.enterEmail),
            const SizedBox(height: 16),

            _buildLabel(AppStrings.contactName),
            const SizedBox(height: 6),
            _buildField(_nameController, AppStrings.enterName),
            const SizedBox(height: 16),

            _buildLabel(AppStrings.contactCategory),
            const SizedBox(height: 6),
            if (widget.existingCategories.isNotEmpty)
              Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: Wrap(
                  spacing: 6,
                  runSpacing: 4,
                  children: widget.existingCategories.map((cat) {
                    final isSelected = _selectedCategories.contains(cat);
                    return FilterChip(
                      label: Text(cat, style: const TextStyle(fontSize: 12)),
                      selected: isSelected,
                      onSelected: (selected) {
                        setState(() {
                          if (selected) {
                            _selectedCategories.add(cat);
                          } else {
                            _selectedCategories.remove(cat);
                          }
                        });
                      },
                    );
                  }).toList(),
                ),
              ),
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _categoryController,
                    decoration: InputDecoration(
                      hintText: AppStrings.selectOrInputCategory,
                      hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    ),
                    onSubmitted: _addCategory,
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  onPressed: () => _addCategory(_categoryController.text),
                  icon: const Icon(Icons.add_circle, size: 22),
                  color: Theme.of(context).colorScheme.primary,
                ),
              ],
            ),
            if (_selectedCategories.any((c) => !widget.existingCategories.contains(c)))
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: Wrap(
                  spacing: 6,
                  runSpacing: 4,
                  children: _selectedCategories
                      .where((c) => !widget.existingCategories.contains(c))
                      .map((cat) => Chip(
                            label: Text(cat, style: const TextStyle(fontSize: 12)),
                            deleteIcon: const Icon(Icons.close, size: 16),
                            onDeleted: () => _removeCategory(cat),
                          ))
                      .toList(),
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
                  style: TextButton.styleFrom(
                    foregroundColor: Colors.red,
                  ),
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
