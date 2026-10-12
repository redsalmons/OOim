import 'dart:isolate';

import 'package:flutter/material.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/email_core.dart' as native;
import 'package:oim_core/native/email_providers.dart';

/// Top-level worker for `Isolate.spawn`: must not capture instance state.
/// authority() blocks up to ~120s waiting for the OAuth redirect on
/// 127.0.0.1:9871, so it runs off the UI isolate. The browser-launch
/// notification is delivered to the callback registered on the main
/// isolate (same isolate group), which opens the system browser.
void _authorityWorker(List<dynamic> args) {
  final send = args[0] as SendPort;
  final configIndex = args[1] as int;
  send.send(native.EmailCore.oemailimAuthority(configIndex));
}

/// Mobile account management: list accounts stored in oim.conf, add/edit
/// per-provider fields, trigger Outlook OAuth, delete accounts.
class AccountsPage extends StatefulWidget {
  const AccountsPage({super.key, required this.configPath});

  final String configPath;

  @override
  State<AccountsPage> createState() => _AccountsPageState();
}

class _AccountsPageState extends State<AccountsPage> {
  final List<EmailAccountConfig> _accounts = [];
  late String _localDataPath;

  @override
  void initState() {
    super.initState();
    _localDataPath = widget.configPath.replaceAll('/config/oim.conf', '/data');
    final loaded = native.EmailCore.loadConfig(widget.configPath);
    if (loaded != null) {
      if (loaded.localDataPath.isNotEmpty) {
        _localDataPath = loaded.localDataPath;
      }
      _accounts.addAll(loaded.accounts.map(_fromNativeAccount));
    }
  }

  EmailAccountConfig _fromNativeAccount(native.EmailAccountData data) {
    return EmailAccountConfig(
      type: data.type,
      email: data.email,
      authCode: data.authCode,
      smtpServer: data.smtpServer,
      smtpPort: data.smtpPort > 0 ? data.smtpPort.toString() : '',
      imapServer: data.imapServer,
      imapPort: data.imapPort > 0 ? data.imapPort.toString() : '',
      accountType: data.accountType,
      authorized: data.authorized,
      id: data.id,
      uid: data.uid,
      phrase: data.phrase,
      folderSize: data.folderSize,
      isDefault: data.isDefault,
    );
  }

  native.EmailAccountData _toNativeAccount(EmailAccountConfig account) {
    return native.EmailAccountData(
      type: account.type,
      email: account.email.trim(),
      authCode: account.authCode.trim(),
      smtpServer: account.smtpServer.trim(),
      smtpPort: int.tryParse(account.smtpPort.trim()) ?? 0,
      imapServer: account.imapServer.trim(),
      imapPort: int.tryParse(account.imapPort.trim()) ?? 0,
      accountType: account.accountType.trim(),
      authorized: account.authorized,
      id: account.id,
      uid: account.uid,
      phrase: account.phrase,
      folderSize: account.folderSize,
      isDefault: account.isDefault,
    );
  }

  bool _persistConfig() {
    return native.EmailCore.saveConfig(
      widget.configPath,
      _localDataPath,
      _accounts.map(_toNativeAccount).toList(),
    );
  }

  void _showMessage(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Future<void> _addAccount() async {
    if (_accounts.length >= 5) {
      _showMessage(AppStrings.maxAccountsReached);
      return;
    }
    final type = await showModalBottomSheet<String>(
      context: context,
      builder: (context) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (final t in EmailProviders.supportedTypes)
              ListTile(
                title: Text(EmailProviders.displayNameFor(t)),
                onTap: () => Navigator.of(context).pop(t),
              ),
          ],
        ),
      ),
    );
    if (type == null || !mounted) return;
    final account = EmailProviders.newAccount(type);
    final saved = await Navigator.of(context).push<bool>(
      MaterialPageRoute(
        builder: (_) => AccountFormPage(account: account),
      ),
    );
    if (saved == true) {
      setState(() => _accounts.add(account));
      _showMessage(_persistConfig() ? AppStrings.configSaved : AppStrings.saveFailed);
    }
  }

  Future<void> _editAccount(int index) async {
    final saved = await Navigator.of(context).push<bool>(
      MaterialPageRoute(
        builder: (_) => AccountFormPage(account: _accounts[index]),
      ),
    );
    if (saved == true) {
      setState(() {});
      _showMessage(_persistConfig() ? AppStrings.configSaved : AppStrings.saveFailed);
    }
  }

  Future<void> _deleteAccount(int index) async {
    final account = _accounts[index];
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        content: Text(account.email.isNotEmpty ? account.email : account.displayName),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: Text(AppStrings.cancel),
          ),
          TextButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: Text(AppStrings.delete, style: const TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
    if (confirmed != true) return;
    if (account.email.isNotEmpty) {
      native.EmailCore.deleteAccountData(account.email, _localDataPath);
    }
    setState(() => _accounts.removeAt(index));
    _persistConfig();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text(AppStrings.accountManagement)),
      floatingActionButton: FloatingActionButton(
        onPressed: _addAccount,
        child: const Icon(Icons.add),
      ),
      body: _accounts.isEmpty
          ? Center(
              child: Text(
                AppStrings.noEmailConfig,
                style: const TextStyle(color: Colors.grey),
                textAlign: TextAlign.center,
              ),
            )
          : ListView.separated(
              itemCount: _accounts.length,
              separatorBuilder: (_, __) => const Divider(height: 1),
              itemBuilder: (context, i) {
                final a = _accounts[i];
                return ListTile(
                  leading: CircleAvatar(
                    child: Text(a.displayName.isNotEmpty
                        ? a.displayName[0].toUpperCase()
                        : '?'),
                  ),
                  title: Text(
                    a.email.isNotEmpty ? a.email : a.displayName,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                  subtitle: Text(
                    '${a.displayName} · ${a.authorized ? AppStrings.authorized : AppStrings.unauthorized}',
                  ),
                  trailing: IconButton(
                    icon: const Icon(Icons.delete_outline, color: Colors.red),
                    onPressed: () => _deleteAccount(i),
                  ),
                  onTap: () => _editAccount(i),
                );
              },
            ),
    );
  }
}

/// Form page for a single account: provider-specific fields plus the
/// authorize action. Pops with `true` when the account should be persisted.
class AccountFormPage extends StatefulWidget {
  const AccountFormPage({super.key, required this.account});

  final EmailAccountConfig account;

  @override
  State<AccountFormPage> createState() => _AccountFormPageState();
}

class _AccountFormPageState extends State<AccountFormPage> {
  final Map<String, TextEditingController> _controllers = {};
  bool _authorizing = false;

  EmailAccountConfig get account => widget.account;

  @override
  void dispose() {
    for (final c in _controllers.values) {
      c.dispose();
    }
    super.dispose();
  }

  String _valueFor(String key) {
    switch (key) {
      case 'email':
        return account.email;
      case 'auth_code':
        return account.authCode;
      case 'smtp_server':
        return account.smtpServer;
      case 'smtp_port':
        return account.smtpPort;
      case 'imap_server':
        return account.imapServer;
      case 'imap_port':
        return account.imapPort;
      case 'account_type':
        return account.accountType;
      default:
        return '';
    }
  }

  void _setValue(String key, String value) {
    value = value.trim();
    switch (key) {
      case 'email':
        account.email = value;
        break;
      case 'auth_code':
        account.authCode = value;
        break;
      case 'smtp_server':
        account.smtpServer = value;
        break;
      case 'smtp_port':
        account.smtpPort = value;
        break;
      case 'imap_server':
        account.imapServer = value;
        break;
      case 'imap_port':
        account.imapPort = value;
        break;
      case 'account_type':
        account.accountType = value;
        break;
    }
  }

  void _showMessage(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  void _save() {
    for (final field in EmailProviders.fieldsFor(account.type)) {
      if (field.required && _valueFor(field.key).trim().isEmpty) {
        _showMessage('${field.label} ${AppStrings.fieldCannotBeEmpty}');
        return;
      }
    }
    Navigator.of(context).pop(true);
  }

  Future<void> _authorize() async {
    if (account.type != 'outlook.com') {
      // 163/QQ/Gmail use auth codes entered by hand; mirror the desktop flow
      // and just mark the account authorized.
      setState(() => account.authorized = true);
      _showMessage(AppStrings.authorizeSuccess);
      return;
    }

    setState(() => _authorizing = true);
    _showMessage(AppStrings.authorizing);
    try {
      final configIndex = native.EmailCore.oemailimAddOutlookEmail();
      if (configIndex < 0) {
        _showMessage(AppStrings.createConfigFailed);
        return;
      }
      final port = ReceivePort();
      await Isolate.spawn(_authorityWorker, [port.sendPort, configIndex]);
      final result = await port.first == true;
      port.close();
      if (result) {
        final email = native.EmailCore.oemailimGetEmail(configIndex);
        final refreshToken = native.EmailCore.oemailimGetRefreshToken(configIndex);
        if (email.isNotEmpty && refreshToken.isNotEmpty) {
          setState(() {
            account.email = email;
            account.authCode = refreshToken;
            account.authorized = true;
          });
          _showMessage(AppStrings.authorizeSuccess);
        } else {
          _showMessage(AppStrings.authorizeSuccessNoEmail);
        }
      } else {
        _showMessage(AppStrings.authorizeFailed);
      }
    } catch (e) {
      _showMessage('${AppStrings.authorizeException}: $e');
    } finally {
      if (mounted) setState(() => _authorizing = false);
    }
  }

  Widget _buildField(ConfigField field) {
    switch (field.kind) {
      case FieldKind.accountType:
        return DropdownButtonFormField<String>(
          initialValue: account.accountType,
          decoration: InputDecoration(
            labelText: field.label,
            border: const OutlineInputBorder(),
          ),
          items: const [
            DropdownMenuItem(value: 'personal', child: Text('personal')),
            DropdownMenuItem(value: 'enterprise', child: Text('enterprise')),
          ],
          onChanged: (value) {
            if (value != null) _setValue(field.key, value);
          },
        );
      case FieldKind.authStatus:
        return Row(
          children: [
            Expanded(
              child: Text(
                account.authorized ? AppStrings.authorized : AppStrings.unauthorized,
                style: TextStyle(
                  color: account.authorized ? Colors.green : Colors.grey,
                ),
              ),
            ),
            if (_authorizing)
              const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            else
              OutlinedButton(
                onPressed: account.authorized ? null : _authorize,
                child: Text(AppStrings.authorize),
              ),
          ],
        );
      case FieldKind.text:
      case FieldKind.port:
        final controller = _controllers.putIfAbsent(
          field.key,
          () => TextEditingController(text: _valueFor(field.key)),
        );
        return TextField(
          controller: controller,
          keyboardType:
              field.kind == FieldKind.port ? TextInputType.number : TextInputType.text,
          decoration: InputDecoration(
            labelText: field.label,
            border: const OutlineInputBorder(),
          ),
          onChanged: (value) => _setValue(field.key, value),
        );
    }
  }

  @override
  Widget build(BuildContext context) {
    final fields = EmailProviders.fieldsFor(account.type);
    return Scaffold(
      appBar: AppBar(
        title: Text(account.displayName),
        actions: [
          TextButton(
            onPressed: _authorizing ? null : _save,
            child: Text(AppStrings.save),
          ),
        ],
      ),
      body: ListView.separated(
        padding: const EdgeInsets.all(16),
        itemCount: fields.length,
        separatorBuilder: (_, __) => const SizedBox(height: 12),
        itemBuilder: (context, i) => _buildField(fields[i]),
      ),
    );
  }
}
