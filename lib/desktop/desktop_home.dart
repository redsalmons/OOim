import 'package:flutter/material.dart';
import 'modules/email_module.dart';
import 'modules/contacts_module.dart';
import 'modules/settings_module.dart';
import 'app_theme.dart';

class DesktopHome extends StatefulWidget {
  const DesktopHome({super.key});

  @override
  State<DesktopHome> createState() => _DesktopHomeState();
}

class _DesktopHomeState extends State<DesktopHome> {
  int _selectedModule = 0;
  final GlobalKey<EmailModuleState> _emailKey = GlobalKey<EmailModuleState>();

  late final List<Widget> _modules = [
    EmailModule(key: _emailKey),
    const ContactsModule(),
    const SettingsModule(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Row(
        children: [
          _buildSidebar(),
          Expanded(
            child: IndexedStack(
              index: _selectedModule,
              children: _modules,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSidebar() {
    return Container(
      width: 84,
      color: AppTheme.sidebarColor,
      child: Column(
        children: [
          const SizedBox(height: 24),
          _buildSidebarAvatar(),
          const SizedBox(height: 32),
          _buildSidebarItem(Icons.email_rounded, '邮件', 0),
          const SizedBox(height: 8),
          _buildSidebarItem(Icons.contacts_rounded, '通讯录', 1),
          const SizedBox(height: 8),
          _buildSidebarItem(Icons.settings_rounded, '设置', 2),
          const Spacer(),
        ],
      ),
    );
  }

  Widget _buildSidebarAvatar() {
    return Container(
      width: 48,
      height: 48,
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: AppTheme.avatarGradient,
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        borderRadius: BorderRadius.circular(14),
      ),
      child: const Center(
        child: Text(
          '我',
          style: TextStyle(color: Colors.white, fontSize: 18, fontWeight: FontWeight.w600),
        ),
      ),
    );
  }

  Widget _buildSidebarItem(IconData icon, String label, int index) {
    final isSelected = _selectedModule == index;
    final primary = Theme.of(context).colorScheme.primary;
    return GestureDetector(
      onTap: () {
        setState(() {
          _selectedModule = index;
        });
        if (index == 0) {
          print('Email icon tapped, emailKey.currentState=${_emailKey.currentState}');
          _emailKey.currentState?.reloadFromDb();
        }
      },
      child: Container(
        width: 64,
        padding: const EdgeInsets.symmetric(vertical: 10),
        decoration: BoxDecoration(
          color: isSelected ? primary.withValues(alpha: 0.18) : Colors.transparent,
          borderRadius: BorderRadius.circular(14),
        ),
        child: Column(
          children: [
            Icon(
              icon,
              color: isSelected ? primary : Colors.grey[500],
              size: 24,
            ),
            const SizedBox(height: 4),
            Text(
              label,
              style: TextStyle(
                color: isSelected ? primary : Colors.grey[500],
                fontSize: 11,
                fontWeight: isSelected ? FontWeight.w600 : FontWeight.normal,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
