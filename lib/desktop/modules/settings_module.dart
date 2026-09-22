import 'dart:io';
import 'dart:convert';
import '../../native/email_core.dart' as native;
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import '../dialogs/email_config_dialog.dart';
import '../../i18n/app_strings.dart';
import '../../main.dart' show appThemeMode, appTextScale, kTextScaleSmall, kTextScaleMedium, kTextScaleLarge;
import '../app_theme.dart';

class SettingsModule extends StatefulWidget {
  const SettingsModule({super.key});

  @override
  State<SettingsModule> createState() => _SettingsModuleState();
}

class _SettingsModuleState extends State<SettingsModule> {
  bool _notifications = true;
  bool _autoUpdate = true;
  String _language = AppStrings.isZh ? '简体中文' : 'English';
  String _configPath = '';
  String _username = '';
  String _defaultEmail = '';
  bool _editingName = false;
  final TextEditingController _nameController = TextEditingController();
  late final FocusNode _nameFocusNode = FocusNode()
    ..addListener(() {
      // Blur: non-empty commits, empty abandons.
      if (!_nameFocusNode.hasFocus && _editingName) _commitNameEdit();
    });

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
    _loadProfile();
  }

  String get _prefsPath =>
      '${File(_configPath).parent.path}/user_prefs.json';

  Future<void> _loadProfile() async {
    final cfg = native.EmailCore.loadConfig(_configPath);
    final def = cfg?.accounts.where((a) => a.isDefault).firstOrNull ??
        (cfg != null && cfg.accounts.isNotEmpty ? cfg.accounts.first : null);
    String name = '';
    try {
      final f = File(_prefsPath);
      if (f.existsSync()) {
        final j = jsonDecode(f.readAsStringSync());
        name = j['username']?.toString() ?? '';
      }
    } catch (_) {}
    if (name.isEmpty && def != null) name = def.email.split('@').first;
    if (!mounted) return;
    setState(() {
      _username = name;
      _defaultEmail = def?.email ?? '';
    });
  }

  void _commitNameEdit() {
    final v = _nameController.text.trim();
    setState(() => _editingName = false);
    if (v.isEmpty || v == _username) return;
    _username = v;
    setState(() {});
    try {
      File(_prefsPath).writeAsStringSync(jsonEncode({'username': v}));
    } catch (_) {}
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
            Text(
              AppStrings.settings,
              style: const TextStyle(fontSize: 28, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 24),
            Expanded(
              child: ListView(
                children: [
                  _buildAccountSection(),
                  const SizedBox(height: 24),
                  _buildAppearanceSection(),
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
            Text(
              AppStrings.account,
              style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildAccountItem(),
            const Divider(),
            _buildSettingsItem(
              Icons.manage_accounts,
              AppStrings.accountManagement,
              AppStrings.accountManagementDesc,
              () => _showAccountConfigDialog(),
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
          child: Center(
            child: Text(
              _username.isNotEmpty ? _username[0].toUpperCase() : AppStrings.me,
              style: const TextStyle(color: Colors.white, fontSize: 24),
            ),
          ),
        ),
        const SizedBox(width: 16),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _editingName
                  ? SizedBox(
                      height: 26,
                      child: TextField(
                        controller: _nameController,
                        focusNode: _nameFocusNode,
                        autofocus: true,
                        style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500),
                        decoration: const InputDecoration(
                          isDense: true,
                          contentPadding: EdgeInsets.zero,
                          border: InputBorder.none,
                        ),
                        onSubmitted: (_) => _commitNameEdit(),
                        onEditingComplete: _commitNameEdit,
                      ),
                    )
                  : GestureDetector(
                      onTap: () {
                        setState(() {
                          _nameController.text = _username;
                          _editingName = true;
                        });
                      },
                      child: Text(
                        _username.isEmpty ? AppStrings.username : _username,
                        style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500),
                      ),
                    ),
              const SizedBox(height: 4),
              Text(
                _defaultEmail,
                style: TextStyle(fontSize: 14, color: context.oim.textSecondary),
              ),
            ],
          ),
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
            Text(
              AppStrings.appearance,
              style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildThemeModeItem(),
            const Divider(),
            _buildSettingsItem(
              Icons.language,
              AppStrings.language,
              _language,
              () {},
            ),
            _buildFontSizeItem(),
          ],
        ),
      ),
    );
  }

  Widget _buildThemeModeItem() {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 12),
      child: Row(
        children: [
          Icon(Icons.brightness_6, size: 24, color: context.oim.textSecondary),
          const SizedBox(width: 16),
          Expanded(
            child: Text(AppStrings.themeMode, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w500)),
          ),
          ValueListenableBuilder<ThemeMode>(
            valueListenable: appThemeMode,
            builder: (context, mode, _) => SegmentedButton<ThemeMode>(
              showSelectedIcon: false,
              style: ButtonStyle(
                visualDensity: VisualDensity.compact,
                textStyle: WidgetStatePropertyAll(const TextStyle(fontSize: 12)),
              ),
              segments: [
                ButtonSegment(value: ThemeMode.system, label: Text(AppStrings.themeFollowSystem)),
                ButtonSegment(value: ThemeMode.light, label: Text(AppStrings.themeLight)),
                ButtonSegment(value: ThemeMode.dark, label: Text(AppStrings.themeDark)),
              ],
              selected: {mode},
              onSelectionChanged: (sel) => appThemeMode.value = sel.first,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildFontSizeItem() {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 12),
      child: Row(
        children: [
          Icon(Icons.text_fields, size: 24, color: context.oim.textSecondary),
          const SizedBox(width: 16),
          Expanded(
            child: Text(AppStrings.fontSize, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w500)),
          ),
          ValueListenableBuilder<double>(
            valueListenable: appTextScale,
            builder: (context, scale, _) => SegmentedButton<double>(
              showSelectedIcon: false,
              style: const ButtonStyle(
                visualDensity: VisualDensity.compact,
                textStyle: WidgetStatePropertyAll(TextStyle(fontSize: 12)),
              ),
              segments: [
                ButtonSegment(value: kTextScaleSmall, label: Text(AppStrings.fontSizeSmall)),
                ButtonSegment(value: kTextScaleMedium, label: Text(AppStrings.fontSizeMedium)),
                ButtonSegment(value: kTextScaleLarge, label: Text(AppStrings.fontSizeLarge)),
              ],
              selected: {scale},
              onSelectionChanged: (sel) => appTextScale.value = sel.first,
            ),
          ),
        ],
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
            Icon(icon, size: 24, color: context.oim.textSecondary),
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
                      style: TextStyle(fontSize: 13, color: context.oim.textSecondary),
                    ),
                ],
              ),
            ),
            Icon(Icons.chevron_right, color: context.oim.textMuted, size: 20),
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
          Icon(icon, size: 24, color: context.oim.textSecondary),
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
                  style: TextStyle(fontSize: 13, color: context.oim.textSecondary),
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
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (context) => EmailConfigDialog(
          configPath: _configPath,
          asPage: true,
        ),
      ),
    );
  }
}
