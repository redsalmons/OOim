import 'dart:io';
import 'dart:convert';
import 'dart:isolate';
import 'package:flutter/material.dart';
import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:path_provider/path_provider.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:crypto/crypto.dart';
import 'package:http/http.dart' as http;
import '../../native/email_core.dart' as native;

class ConfigField {
  final String label;
  final String key;
  final bool required;
  final String defaultValue;
  final FieldKind kind;

  const ConfigField(this.label, this.key, this.required, this.defaultValue, this.kind);
}

enum FieldKind { text, port, accountType, authStatus }

class EmailAccountConfig {
  String type;
  String email;
  String authCode;
  String smtpServer;
  String smtpPort;
  String imapServer;
  String imapPort;
  String accountType;
  bool authorized;
  String id;
  int uid;
  String phrase;
  int folderSize;

  EmailAccountConfig({
    required this.type,
    this.email = '',
    this.authCode = '',
    this.smtpServer = '',
    this.smtpPort = '',
    this.imapServer = '',
    this.imapPort = '',
    this.accountType = 'personal',
    this.authorized = false,
    this.id = '',
    this.uid = 0,
    this.phrase = '',
    this.folderSize = 0,
  });

  String get displayName {
    switch (type) {
      case '163.com':
        return '网易163邮箱';
      case 'gmail.com':
        return 'Gmail';
      case 'outlook.com':
        return 'Outlook';
      default:
        return type;
    }
  }
}

class EmailProviders {
  static const List<String> supportedTypes = ['163.com', 'gmail.com', 'outlook.com'];

  static String displayNameFor(String type) {
    switch (type) {
      case '163.com':
        return '网易163邮箱';
      case 'gmail.com':
        return 'Gmail';
      case 'outlook.com':
        return 'Outlook';
      default:
        return type;
    }
  }

  static List<ConfigField> fieldsFor(String type) {
    switch (type) {
      case '163.com':
        return const [
          ConfigField('邮箱地址', 'email', true, '', FieldKind.text),
          ConfigField('授权码', 'auth_code', true, '', FieldKind.text),
          ConfigField('SMTP服务器', 'smtp_server', false, 'smtp.163.com', FieldKind.text),
          ConfigField('SMTP端口', 'smtp_port', false, '465', FieldKind.port),
          ConfigField('IMAP服务器', 'imap_server', false, 'imap.163.com', FieldKind.text),
          ConfigField('IMAP端口', 'imap_port', false, '993', FieldKind.port),
        ];
      case 'gmail.com':
        return const [
          ConfigField('邮箱地址', 'email', false, '', FieldKind.text),
          ConfigField('SMTP服务器', 'smtp_server', false, 'smtp.gmail.com', FieldKind.text),
          ConfigField('SMTP端口', 'smtp_port', false, '587', FieldKind.port),
          ConfigField('IMAP服务器', 'imap_server', false, 'imap.gmail.com', FieldKind.text),
          ConfigField('IMAP端口', 'imap_port', false, '993', FieldKind.port),
          ConfigField('授权状态', 'auth_status', false, 'unauthorized', FieldKind.authStatus),
        ];
      case 'outlook.com':
        return const [
          ConfigField('邮箱地址', 'email', false, '', FieldKind.text),
          ConfigField('账户类型', 'account_type', false, 'personal', FieldKind.accountType),
          ConfigField('SMTP服务器', 'smtp_server', false, 'smtp-mail.outlook.com', FieldKind.text),
          ConfigField('SMTP端口', 'smtp_port', false, '587', FieldKind.port),
          ConfigField('IMAP服务器', 'imap_server', false, 'outlook.office365.com', FieldKind.text),
          ConfigField('IMAP端口', 'imap_port', false, '993', FieldKind.port),
          ConfigField('授权状态', 'auth_status', false, 'unauthorized', FieldKind.authStatus),
        ];
      default:
        return const [];
    }
  }

  static EmailAccountConfig newAccount(String type) {
    final account = EmailAccountConfig(type: type);
    for (final field in fieldsFor(type)) {
      switch (field.key) {
        case 'smtp_server':
          account.smtpServer = field.defaultValue;
          break;
        case 'smtp_port':
          account.smtpPort = field.defaultValue;
          break;
        case 'imap_server':
          account.imapServer = field.defaultValue;
          break;
        case 'imap_port':
          account.imapPort = field.defaultValue;
          break;
        case 'account_type':
          account.accountType = field.defaultValue;
          break;
      }
    }
    return account;
  }
}

class EmailConfigDialog extends StatefulWidget {
  final String configPath;
  final bool standalone;
  final Future<void> Function()? onDone;

  const EmailConfigDialog({
    super.key,
    required this.configPath,
    this.standalone = false,
    this.onDone,
  });

  @override
  State<EmailConfigDialog> createState() => _EmailConfigDialogState();
}

class _EmailConfigDialogState extends State<EmailConfigDialog> {
  final TextEditingController _localPathController = TextEditingController(
    text: '${Directory.current.path}/data',
  );

  String _selectedNewType = EmailProviders.supportedTypes.first;
  final List<EmailAccountConfig> _accounts = [];
  int _selectedTabIndex = -1;

  final Map<String, TextEditingController> _controllers = {};

  @override
  void initState() {
    super.initState();
    native.EmailCore.initialize();
    _loadExistingConfig();
  }

  void _loadExistingConfig() {
    if (!native.EmailCore.configExists(widget.configPath)) {
      return;
    }
    final loaded = native.EmailCore.loadConfig(widget.configPath);
    if (loaded == null) {
      return;
    }
    setState(() {
      if (loaded.localDataPath.isNotEmpty) {
        _localPathController.text = loaded.localDataPath;
      }
      _accounts.clear();
      _accounts.addAll(loaded.accounts.map(_fromNativeAccount));
      _selectedTabIndex = _accounts.isEmpty ? -1 : 0;
    });
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
    );
  }

  native.EmailAccountData _toNativeAccount(EmailAccountConfig account) {
    return native.EmailAccountData(
      type: account.type,
      email: account.email,
      authCode: account.authCode,
      smtpServer: account.smtpServer,
      smtpPort: int.tryParse(account.smtpPort) ?? 0,
      imapServer: account.imapServer,
      imapPort: int.tryParse(account.imapPort) ?? 0,
      accountType: account.accountType,
      authorized: account.authorized,
      id: account.id,
      uid: account.uid,
      phrase: account.phrase,
      folderSize: account.folderSize,
    );
  }

  bool _persistConfig() {
    final file = File(widget.configPath);
    file.parent.createSync(recursive: true);
    return native.EmailCore.saveConfig(
      widget.configPath,
      _localPathController.text,
      _accounts.map(_toNativeAccount).toList(),
    );
  }

  @override
  void dispose() {
    _localPathController.dispose();
    for (final c in _controllers.values) {
      c.dispose();
    }
    super.dispose();
  }

  void _addAccount() {
    if (_accounts.length >= 5) {
      _showMessage('最多支持添加5个邮箱配置');
      return;
    }
    setState(() {
      _accounts.add(EmailProviders.newAccount(_selectedNewType));
      _selectedTabIndex = _accounts.length - 1;
    });
  }

  void _deleteAccount(int index) {
    _writeToFile('=== 删除按钮被点击 === index=$index');
    _writeToFile('删除前账户数量: ${_accounts.length}');
    setState(() {
      _accounts.removeAt(index);
      if (_accounts.isEmpty) {
        _selectedTabIndex = -1;
      } else if (_selectedTabIndex >= _accounts.length) {
        _selectedTabIndex = _accounts.length - 1;
      }
    });
    _writeToFile('删除后账户数量: ${_accounts.length}');
  }

  void _saveAccount(int index) {
    final account = _accounts[index];
    final fields = EmailProviders.fieldsFor(account.type);
    for (final field in fields) {
      if (field.required) {
        final value = _valueFor(account, field.key);
        if (value.trim().isEmpty) {
          _showMessage('${field.label} 不能为空');
          return;
        }
      }
    }
    final ok = _persistConfig();
    _showMessage(ok ? '配置已保存' : '保存失败，请重试', success: ok);
  }

  void _authorize(EmailAccountConfig account) async {
    _writeToFile('=== _authorize called ===');
    _writeToFile('Account type: ${account.type}');
    
    // For Outlook, trigger OAuth flow via native layer
    if (account.type == 'outlook.com') {
      _writeToFile('Processing Outlook account');
      // Find the config index for this account
      final accountIndex = _accounts.indexOf(account);
      _writeToFile('Account index: $accountIndex');
      _writeToFile('Total accounts: ${_accounts.length}');
      
      if (accountIndex >= 0) {
        _writeToFile('Calling native authority for account index: $accountIndex');
        _showMessage('正在授权，请在浏览器中完成授权...');
        
        // Use Future.delayed to allow UI to update before blocking call
        await Future.delayed(Duration(milliseconds: 100));
        
        try {
          // Create a new Outlook email config (no config file needed)
          native.EmailCore.logWrite('[Dart] Calling oemailimAddOutlookEmail');
          final configIndex = native.EmailCore.oemailimAddOutlookEmail();
          native.EmailCore.logWrite('[Dart] oemailimAddOutlookEmail returned configIndex=$configIndex');
          if (configIndex < 0) {
            _writeToFile('oemailimAddOutlookEmail failed: $configIndex');
            _showMessage('创建邮箱配置失败');
            return;
          }

          // Call authority to trigger OAuth flow (starts HTTP server in C++)
          native.EmailCore.logWrite('[Dart] About to call oemailimAuthority($configIndex)');
          final result = native.EmailCore.oemailimAuthority(configIndex);
          native.EmailCore.logWrite('[Dart] oemailimAuthority returned $result');
          _writeToFile('Native authority result: $result');
          
          if (result) {
            // Get email address and refresh token from native layer
            final email = native.EmailCore.oemailimGetEmail(configIndex);
            final refreshToken = native.EmailCore.oemailimGetRefreshToken(configIndex);
            _writeToFile('Retrieved email: $email');
            _writeToFile('Retrieved refresh_token length: ${refreshToken.length}');
            
            if (email.isNotEmpty && refreshToken.isNotEmpty) {
              setState(() {
                account.email = email;
                account.authCode = refreshToken;
                account.authorized = true;
              });
              _writeToFile('Saving email and refresh_token to config...');
              _persistConfig();
              _showMessage('授权成功', success: true);
            } else {
              _writeToFile('Email or refresh_token is empty');
              _showMessage('授权成功（但未获取到邮箱地址或刷新令牌）', success: true);
            }
          } else {
            _showMessage('授权失败，请重试');
          }
        } catch (e) {
          _writeToFile('Native authority exception: $e');
          _showMessage('授权异常: $e');
        }
      } else {
        _writeToFile('Account index not found');
        _showMessage('账户索引错误');
      }
    } else {
      // For 163, just mark as authorized (uses auth code)
      _writeToFile('Processing 163 account');
      setState(() {
        account.authorized = true;
      });
      _showMessage('授权成功', success: true);
    }
    _writeToFile('=== _authorize completed ===');
  }

  String _generateRandomString(int length) {
    const chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~';
    final random = DateTime.now().millisecondsSinceEpoch;
    final sb = StringBuffer();
    for (int i = 0; i < length; i++) {
      sb.write(chars[(random + i) % chars.length]);
    }
    return sb.toString();
  }

  String _generateCodeVerifier() {
    final random = List<int>.generate(32, (_) => DateTime.now().millisecondsSinceEpoch % 256);
    return _base64UrlEncode(random);
  }

  String _generateCodeChallenge(String verifier) {
    // SHA256 hash the verifier
    final bytes = utf8.encode(verifier);
    final hash = sha256.convert(bytes);
    return _base64UrlEncode(hash.bytes);
  }

  String _base64UrlEncode(List<int> bytes) {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
    final sb = StringBuffer();
    for (int i = 0; i < bytes.length; i += 3) {
      final b0 = bytes[i];
      final b1 = i + 1 < bytes.length ? bytes[i + 1] : 0;
      final b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
      
      final triple = (b0 << 16) | (b1 << 8) | b2;
      sb.write(chars[(triple >> 18) & 0x3F]);
      sb.write(chars[(triple >> 12) & 0x3F]);
      if (i + 1 < bytes.length) sb.write(chars[(triple >> 6) & 0x3F]);
      if (i + 2 < bytes.length) sb.write(chars[triple & 0x3F]);
    }
    return sb.toString();
  }

  String _valueFor(EmailAccountConfig account, String key) {
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

  void _setValue(EmailAccountConfig account, String key, String value) {
    setState(() {
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
    });
  }

  void _writeToFile(String message) {
    native.EmailCore.logWrite('[Dart] $message');
  }

  void _showMessage(String message, {bool success = false}) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(message),
        backgroundColor: success ? Colors.green : null,
        duration: const Duration(seconds: 2),
      ),
    );
  }

  Future<void> _finishConfig() async {
    final file = File(widget.configPath);
    await file.parent.create(recursive: true);
    _persistConfig();
    if (widget.onDone != null) {
      await widget.onDone!();
    } else if (mounted) {
      Navigator.of(context).pop();
    }
  }

  Widget _buildContent() {
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (!widget.standalone) ...[
            Stack(
              alignment: Alignment.center,
              children: [
                const Align(
                  alignment: Alignment.center,
                  child: Text(
                    '邮箱配置',
                    style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                  ),
                ),
                Align(
                  alignment: Alignment.centerRight,
                  child: IconButton(
                    icon: const Icon(Icons.close),
                    onPressed: () => Navigator.of(context).pop(),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
          ],
          _buildLocalPathRow(),
          const Divider(height: 24),
          _buildAddAccountRow(),
          const SizedBox(height: 12),
          Expanded(child: _buildBody()),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (widget.standalone) {
      return Scaffold(
        appBar: AppBar(
          title: const Text('邮箱配置'),
          centerTitle: true,
          automaticallyImplyLeading: false,
          actions: [
            TextButton(
              onPressed: _finishConfig,
              child: const Text('完成', style: TextStyle(color: Colors.white)),
            ),
            const SizedBox(width: 12),
          ],
        ),
        body: _buildContent(),
      );
    }

    return Dialog(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
      child: SizedBox(
        width: 620,
        height: 520,
        child: _buildContent(),
      ),
    );
  }

  Widget _buildLocalPathRow() {
    return Row(
      children: [
        const SizedBox(
          width: 120,
          child: Text('本地数据存储位置', textAlign: TextAlign.right),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: TextField(
            controller: _localPathController,
            decoration: const InputDecoration(
              isDense: true,
              border: OutlineInputBorder(),
              contentPadding: EdgeInsets.symmetric(horizontal: 8, vertical: 8),
            ),
          ),
        ),
        const SizedBox(width: 8),
        OutlinedButton(
          onPressed: () {
            _showMessage('请手动输入本地存储路径');
          },
          child: const Text('浏览'),
        ),
      ],
    );
  }

  Widget _buildAddAccountRow() {
    return Row(
      children: [
        const SizedBox(
          width: 120,
          child: Text('邮箱类型', textAlign: TextAlign.right),
        ),
        const SizedBox(width: 8),
        DropdownButton<String>(
          value: _selectedNewType,
          items: EmailProviders.supportedTypes
              .map((type) => DropdownMenuItem(
                    value: type,
                    child: Text(EmailProviders.displayNameFor(type)),
                  ))
              .toList(),
          onChanged: (value) {
            if (value != null) {
              setState(() {
                _selectedNewType = value;
              });
            }
          },
        ),
        const Spacer(),
        ElevatedButton.icon(
          onPressed: _addAccount,
          icon: const Icon(Icons.add, size: 18),
          label: const Text('添加'),
        ),
      ],
    );
  }

  Widget _buildBody() {
    if (_accounts.isEmpty) {
      return Center(
        child: Text(
          '暂无邮箱配置，请选择类型并点击添加',
          style: TextStyle(color: Colors.grey[600]),
        ),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _buildTabBar(),
        const SizedBox(height: 12),
        Expanded(child: _buildTabContent()),
      ],
    );
  }

  Widget _buildTabBar() {
    return SizedBox(
      height: 36,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        itemCount: _accounts.length,
        separatorBuilder: (context, index) => const SizedBox(width: 8),
        itemBuilder: (context, index) {
          final isSelected = _selectedTabIndex == index;
          final account = _accounts[index];
          return ChoiceChip(
            label: Text('${account.displayName} ${index + 1}'),
            selected: isSelected,
            onSelected: (_) {
              setState(() {
                _selectedTabIndex = index;
              });
            },
          );
        },
      ),
    );
  }

  Widget _buildTabContent() {
    if (_selectedTabIndex < 0 || _selectedTabIndex >= _accounts.length) {
      return const SizedBox.shrink();
    }

    final index = _selectedTabIndex;
    final account = _accounts[index];
    final fields = EmailProviders.fieldsFor(account.type);

    return SingleChildScrollView(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          for (final field in fields) _buildFieldRow(account, field),
          const SizedBox(height: 16),
          Row(
            mainAxisAlignment: MainAxisAlignment.end,
            children: [
              TextButton(
                onPressed: () => _deleteAccount(index),
                child: const Text('删除', style: TextStyle(color: Colors.red)),
              ),
              const SizedBox(width: 8),
              ElevatedButton(
                onPressed: () => _saveAccount(index),
                child: const Text('保存'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildFieldRow(EmailAccountConfig account, ConfigField field) {
    Widget input;

    switch (field.kind) {
      case FieldKind.accountType:
        input = DropdownButtonFormField<String>(
          value: account.accountType,
          decoration: const InputDecoration(
            isDense: true,
            border: OutlineInputBorder(),
            contentPadding: EdgeInsets.symmetric(horizontal: 8, vertical: 8),
          ),
          items: const [
            DropdownMenuItem(value: 'personal', child: Text('personal')),
            DropdownMenuItem(value: 'enterprise', child: Text('enterprise')),
          ],
          onChanged: (value) {
            if (value != null) {
              _setValue(account, field.key, value);
            }
          },
        );
        break;
      case FieldKind.authStatus:
        input = Row(
          children: [
            Expanded(
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 10),
                decoration: BoxDecoration(
                  border: Border.all(color: Colors.grey[400]!),
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Text(
                  account.authorized ? '已授权' : '未授权',
                  style: TextStyle(
                    color: account.authorized ? Colors.green : Colors.grey[700],
                  ),
                ),
              ),
            ),
            const SizedBox(width: 8),
            OutlinedButton(
              onPressed: account.authorized ? null : () {
                _writeToFile('=== 授权按钮被点击 === type=${account.type}');
                _authorize(account);
              },
              child: const Text('授权'),
            ),
          ],
        );
        break;
      case FieldKind.text:
      case FieldKind.port:
        final controllerKey = '${account.hashCode}_${field.key}';
        final controller = _controllers.putIfAbsent(
          controllerKey,
          () => TextEditingController(text: _valueFor(account, field.key)),
        );
        input = TextField(
          controller: controller,
          keyboardType: field.kind == FieldKind.port ? TextInputType.number : TextInputType.text,
          decoration: const InputDecoration(
            isDense: true,
            border: OutlineInputBorder(),
            contentPadding: EdgeInsets.symmetric(horizontal: 8, vertical: 8),
          ),
          onChanged: (value) => _setValue(account, field.key, value),
        );
        break;
    }

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 120,
            child: Padding(
              padding: const EdgeInsets.only(top: 10),
              child: Text(field.label, textAlign: TextAlign.right),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(child: input),
        ],
      ),
    );
  }
}
