import 'package:flutter/material.dart';

class ContactsModule extends StatefulWidget {
  const ContactsModule({super.key});

  @override
  State<ContactsModule> createState() => _ContactsModuleState();
}

class _ContactsModuleState extends State<ContactsModule> {
  final TextEditingController _searchController = TextEditingController();
  String _selectedGroup = '全部联系人';

  final List<String> _groups = [
    '全部联系人',
    '我的好友',
    '同事',
    '家人',
    '重要客户',
  ];

  final List<Contact> _contacts = [
    Contact(
      name: '张三',
      phone: '138****1234',
      email: 'zhangsan@example.com',
      avatar: '张',
      group: '我的好友',
      status: '在线',
    ),
    Contact(
      name: '李四',
      phone: '139****5678',
      email: 'lisi@example.com',
      avatar: '李',
      group: '同事',
      status: '离线',
    ),
    Contact(
      name: '王五',
      phone: '137****9012',
      email: 'wangwu@example.com',
      avatar: '王',
      group: '重要客户',
      status: '忙碌',
    ),
    Contact(
      name: '赵六',
      phone: '136****3456',
      email: 'zhaoliu@example.com',
      avatar: '赵',
      group: '我的好友',
      status: '在线',
    ),
    Contact(
      name: '孙七',
      phone: '135****7890',
      email: 'sunqi@example.com',
      avatar: '孙',
      group: '同事',
      status: '离线',
    ),
    Contact(
      name: '周八',
      phone: '134****2345',
      email: 'zhouba@example.com',
      avatar: '周',
      group: '家人',
      status: '在线',
    ),
    Contact(
      name: '吴九',
      phone: '133****6789',
      email: 'wujiu@example.com',
      avatar: '吴',
      group: '重要客户',
      status: '离线',
    ),
    Contact(
      name: '郑十',
      phone: '132****0123',
      email: 'zhengshi@example.com',
      avatar: '郑',
      group: '同事',
      status: '在线',
    ),
  ];

  List<Contact> get _filteredContacts {
    if (_selectedGroup == '全部联系人') {
      return _contacts;
    }
    return _contacts.where((contact) => contact.group == _selectedGroup).toList();
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
                return _buildContactItem(_filteredContacts[index]);
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
          decoration: const InputDecoration(
            hintText: '搜索联系人',
            hintStyle: TextStyle(fontSize: 14),
            prefixIcon: Icon(Icons.search, size: 20, color: Colors.grey),
            border: InputBorder.none,
            contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
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

  Widget _buildContactItem(Contact contact) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
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
                  contact.name,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w500,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  contact.group,
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey[600],
                  ),
                ),
              ],
            ),
          ),
          Icon(Icons.chevron_right, color: Colors.grey[400], size: 20),
        ],
      ),
    );
  }

  Widget _buildAvatar(String name, String status) {
    Color statusColor;
    switch (status) {
      case '在线':
        statusColor = Colors.green;
        break;
      case '忙碌':
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
        child: Center(
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
                '选择联系人查看详情',
                style: TextStyle(
                  fontSize: 16,
                  color: Colors.grey[600],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class Contact {
  final String name;
  final String phone;
  final String email;
  final String avatar;
  final String group;
  final String status;

  Contact({
    required this.name,
    required this.phone,
    required this.email,
    required this.avatar,
    required this.group,
    required this.status,
  });
}
