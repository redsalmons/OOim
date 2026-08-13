import 'dart:io';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import '../dialogs/email_config_dialog.dart';

class SettingsModule extends StatefulWidget {
  const SettingsModule({super.key});

  @override
  State<SettingsModule> createState() => _SettingsModuleState();
}

class _SettingsModuleState extends State<SettingsModule> {
  bool _darkMode = false;
  bool _notifications = true;
  bool _autoUpdate = true;
  String _language = '简体中文';
  String _configPath = '';

  @override
  void initState() {
    super.initState();
    _initConfigPath();
  }

  Future<void> _initConfigPath() async {
    // Must match main.dart: sandboxed application support directory
    final appDir = await getApplicationSupportDirectory();
    if (!mounted) return;
    setState(() {
      _configPath = '${appDir.path}/config/oim.conf';
    });
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Theme.of(context).scaffoldBackgroundColor,
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              '设置',
              style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 24),
            Expanded(
              child: ListView(
                children: [
                  _buildAccountSection(),
                  const SizedBox(height: 24),
                  _buildAppearanceSection(),
                  const SizedBox(height: 24),
                  _buildNotificationSection(),
                  const SizedBox(height: 24),
                  _buildPrivacySection(),
                  const SizedBox(height: 24),
                  _buildAboutSection(),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildAccountSection() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              '账户',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildAccountItem(),
            const Divider(),
            _buildSettingsItem(
              Icons.manage_accounts,
              '账户管理',
              '添加、编辑或删除邮箱账户',
              () => _showAccountConfigDialog(),
            ),
            _buildSettingsItem(
              Icons.security,
              '账户安全',
              '密码、两步验证等',
              () {},
            ),
            _buildSettingsItem(
              Icons.cloud,
              '数据同步',
              '管理云端数据同步',
              () {},
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildAccountItem() {
    return Row(
      children: [
        Container(
          width: 60,
          height: 60,
          decoration: BoxDecoration(
            color: Theme.of(context).colorScheme.primary,
            borderRadius: BorderRadius.circular(18),
          ),
          child: const Center(
            child: Text(
              '我',
              style: TextStyle(color: Colors.white, fontSize: 24),
            ),
          ),
        ),
        const SizedBox(width: 16),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                '用户名',
                style: TextStyle(fontSize: 16, fontWeight: FontWeight.w500),
              ),
              const SizedBox(height: 4),
              Text(
                'user@example.com',
                style: TextStyle(fontSize: 14, color: Colors.grey[600]),
              ),
            ],
          ),
        ),
        TextButton(
          onPressed: () {},
          child: const Text('编辑'),
        ),
      ],
    );
  }

  Widget _buildAppearanceSection() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              '外观',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSwitchItem(
              Icons.dark_mode,
              '深色模式',
              '启用深色主题',
              _darkMode,
              (value) {
                setState(() {
                  _darkMode = value;
                });
              },
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.language,
              '语言',
              _language,
              () {},
            ),
            _buildSettingsItem(
              Icons.text_fields,
              '字体大小',
              '中',
              () {},
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildNotificationSection() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              '通知',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSwitchItem(
              Icons.notifications,
              '启用通知',
              '接收应用通知',
              _notifications,
              (value) {
                setState(() {
                  _notifications = value;
                });
              },
            ),
            const Divider(),
            _buildSwitchItem(
              Icons.email,
              '邮件通知',
              '新邮件提醒',
              _notifications,
              (value) {
                setState(() {
                  _notifications = value;
                });
              },
            ),
            const Divider(),
            _buildSwitchItem(
              Icons.notifications,
              '消息通知',
              '新消息提醒',
              _notifications,
              (value) {
                setState(() {
                  _notifications = value;
                });
              },
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildPrivacySection() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              '隐私与安全',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSettingsItem(
              Icons.lock,
              '隐私设置',
              '管理您的隐私选项',
              () {},
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.block,
              '屏蔽列表',
              '管理屏蔽的用户',
              () {},
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.history,
              '清除数据',
              '清除本地缓存数据',
              () {},
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildAboutSection() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              '关于',
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSettingsItem(
              Icons.info,
              '版本信息',
              'OIM v1.0.0',
              () {},
            ),
            const Divider(),
            _buildSwitchItem(
              Icons.system_update,
              '自动更新',
              '自动检查更新',
              _autoUpdate,
              (value) {
                setState(() {
                  _autoUpdate = value;
                });
              },
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.help,
              '帮助与反馈',
              '获取帮助或提供反馈',
              () {},
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.description,
              '用户协议',
              '查看用户协议',
              () {},
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildSettingsItem(
    IconData icon,
    String title,
    String subtitle, [
    VoidCallback? onTap,
  ]) {
    return InkWell(
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 12),
        child: Row(
          children: [
            Icon(icon, size: 24, color: Colors.grey[700]),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: const TextStyle(fontSize: 15),
                  ),
                  if (subtitle.isNotEmpty)
                    Text(
                      subtitle,
                      style: TextStyle(fontSize: 13, color: Colors.grey[600]),
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

  Widget _buildSwitchItem(
    IconData icon,
    String title,
    String subtitle,
    bool value,
    ValueChanged<bool> onChanged,
  ) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 12),
      child: Row(
        children: [
          Icon(icon, size: 24, color: Colors.grey[700]),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: const TextStyle(fontSize: 15),
                ),
                Text(
                  subtitle,
                  style: TextStyle(fontSize: 13, color: Colors.grey[600]),
                ),
              ],
            ),
          ),
          Switch(
            value: value,
            onChanged: onChanged,
          ),
        ],
      ),
    );
  }

  void _showAccountConfigDialog() {
    if (_configPath.isEmpty) return;
    showDialog(
      context: context,
      builder: (context) => EmailConfigDialog(
        configPath: _configPath,
        onDone: () async {
          // Optionally trigger a reload of the email module
          // This would require a callback or event system
        },
      ),
    );
  }
}
