import 'package:flutter/material.dart';
import 'package:oim_core/backup_page.dart';
import 'package:oim_core/i18n/app_strings.dart';

import 'accounts_page.dart';
import 'contacts_page.dart';
import 'new_session_page.dart';

class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.configPath});

  final String configPath;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text(AppStrings.settings)),
      body: ListView(children: [
        ListTile(
          leading: const Icon(Icons.forum_outlined),
          title: Text(AppStrings.newSession),
          subtitle: Text(AppStrings.startNewSession),
          trailing: const Icon(Icons.chevron_right),
          onTap: () => Navigator.of(context).push(MaterialPageRoute(
            builder: (_) => NewSessionPage(configPath: configPath),
          )),
        ),
        ListTile(
          leading: const Icon(Icons.manage_accounts_outlined),
          title: Text(AppStrings.accountManagement),
          subtitle: Text(AppStrings.accountManagementDesc),
          trailing: const Icon(Icons.chevron_right),
          onTap: () => Navigator.of(context).push(MaterialPageRoute(
            builder: (_) => AccountsPage(configPath: configPath),
          )),
        ),
        ListTile(
          leading: const Icon(Icons.contacts_outlined),
          title: Text(AppStrings.contacts),
          trailing: const Icon(Icons.chevron_right),
          onTap: () => Navigator.of(context).push(MaterialPageRoute(
            builder: (_) => const ContactsPage(),
          )),
        ),
        ListTile(
          leading: const Icon(Icons.settings_backup_restore),
          title: Text(AppStrings.isZh ? '备份与恢复' : 'Backup and restore'),
          trailing: const Icon(Icons.chevron_right),
          onTap: () => Navigator.of(context).push(MaterialPageRoute(
            builder: (_) => const BackupPage(),
          )),
        ),
      ]),
    );
  }
}
