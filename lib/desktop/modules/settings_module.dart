import 'dart:io';
import 'dart:convert';
import '../../native/email_core.dart' as native;
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import '../dialogs/email_config_dialog.dart';
import '../../i18n/app_strings.dart';
import '../../main.dart' show appThemeMode, appTextScale, kTextScaleSmall, kTextScaleMedium, kTextScaleLarge;
import '../../i18n/app_strings.dart' show appLocale;
import '../app_theme.dart';
import '../user_prefs.dart';

class SettingsModule extends StatefulWidget {
  const SettingsModule({super.key});

  @override
  State<SettingsModule> createState() => _SettingsModuleState();
}

class _SettingsModuleState extends State<SettingsModule> {
  bool _notifications = true;
  bool _autoUpdate = true;
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

  void _persistPref(String key, dynamic value) =>
      writeUserPref(_configPath, key, value);

  Future<void> _loadProfile() async {
    final cfg = native.EmailCore.loadConfig(_configPath);
    final def = cfg?.accounts.where((a) => a.isDefault).firstOrNull ??
        (cfg != null && cfg.accounts.isNotEmpty ? cfg.accounts.first : null);
    var name = readUserPrefs(_configPath)['username']?.toString() ?? '';
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
    _persistPref('username', v);
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
            _buildLanguageItem(),
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
              onSelectionChanged: (sel) {
                appThemeMode.value = sel.first;
                _persistPref('themeMode', sel.first.name);
              },
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildLanguageItem() {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 12),
      child: Row(
        children: [
          Icon(Icons.language, size: 24, color: context.oim.textSecondary),
          const SizedBox(width: 16),
          Expanded(
            child: Text(AppStrings.language, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w500)),
          ),
          ValueListenableBuilder<Locale?>(
            valueListenable: appLocale,
            builder: (context, loc, _) => SegmentedButton<String>(
              showSelectedIcon: false,
              style: const ButtonStyle(
                visualDensity: VisualDensity.compact,
                textStyle: WidgetStatePropertyAll(TextStyle(fontSize: 12)),
              ),
              segments: [
                ButtonSegment(value: 'system', label: Text(AppStrings.followSystem)),
                const ButtonSegment(value: 'zh', label: Text('中文')),
                const ButtonSegment(value: 'en', label: Text('English')),
              ],
              selected: {loc?.languageCode ?? 'system'},
              onSelectionChanged: (sel) {
                appLocale.value =
                    sel.first == 'system' ? null : Locale(sel.first);
                _persistPref('locale', sel.first);
              },
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
              onSelectionChanged: (sel) {
                appTextScale.value = sel.first;
                _persistPref('textScale', sel.first);
              },
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
            Text(
              AppStrings.notifications,
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSwitchItem(
              Icons.notifications,
              AppStrings.enableNotifications,
              AppStrings.enableNotificationsDesc,
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
              AppStrings.emailNotification,
              AppStrings.emailNotificationDesc,
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
              AppStrings.messageNotification,
              AppStrings.messageNotificationDesc,
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
            Text(
              AppStrings.privacySecurity,
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSettingsItem(
              Icons.lock,
              AppStrings.privacySettings,
              AppStrings.privacySettingsDesc,
              () {},
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.block,
              AppStrings.blockedList,
              AppStrings.blockedListDesc,
              () {},
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.history,
              AppStrings.clearData,
              AppStrings.clearDataDesc,
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
            Text(
              AppStrings.about,
              style: TextStyle(fontSize: 18, fontWeight: FontWeight.w500),
            ),
            const SizedBox(height: 16),
            _buildSettingsItem(
              Icons.info,
              AppStrings.versionInfo,
              'OIM v1.0.0',
              () {},
            ),
            const Divider(),
            _buildSwitchItem(
              Icons.system_update,
              AppStrings.autoUpdate,
              AppStrings.autoUpdateDesc,
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
              AppStrings.helpFeedback,
              AppStrings.helpFeedbackDesc,
              () {},
            ),
            const Divider(),
            _buildSettingsItem(
              Icons.description,
              AppStrings.userAgreement,
              AppStrings.userAgreementDesc,
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
