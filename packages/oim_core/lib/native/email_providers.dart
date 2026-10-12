import '../i18n/app_strings.dart';

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
  bool isDefault;

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
    this.isDefault = false,
  });

  String get displayName {
    switch (type) {
      case '163.com':
        return AppStrings.netease163;
      case 'qq.com':
        return AppStrings.qqMail;
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
  static const List<String> supportedTypes = ['163.com', 'qq.com', 'gmail.com', 'outlook.com'];

  static String displayNameFor(String type) {
    switch (type) {
      case '163.com':
        return AppStrings.netease163;
      case 'qq.com':
        return AppStrings.qqMail;
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
        return [
          ConfigField(AppStrings.emailAddress, 'email', true, '', FieldKind.text),
          ConfigField(AppStrings.authCode, 'auth_code', true, '', FieldKind.text),
          ConfigField(AppStrings.smtpServer, 'smtp_server', false, 'smtp.163.com', FieldKind.text),
          ConfigField(AppStrings.smtpPort, 'smtp_port', false, '465', FieldKind.port),
          ConfigField(AppStrings.imapServer, 'imap_server', false, 'imap.163.com', FieldKind.text),
          ConfigField(AppStrings.imapPort, 'imap_port', false, '993', FieldKind.port),
        ];
      case 'qq.com':
        return [
          ConfigField(AppStrings.emailAddress, 'email', true, '', FieldKind.text),
          ConfigField(AppStrings.authCode, 'auth_code', true, '', FieldKind.text),
          ConfigField(AppStrings.smtpServer, 'smtp_server', false, 'smtp.qq.com', FieldKind.text),
          ConfigField(AppStrings.smtpPort, 'smtp_port', false, '465', FieldKind.port),
          ConfigField(AppStrings.imapServer, 'imap_server', false, 'imap.qq.com', FieldKind.text),
          ConfigField(AppStrings.imapPort, 'imap_port', false, '993', FieldKind.port),
        ];
      case 'gmail.com':
        return [
          ConfigField(AppStrings.emailAddress, 'email', false, '', FieldKind.text),
          ConfigField(AppStrings.smtpServer, 'smtp_server', false, 'smtp.gmail.com', FieldKind.text),
          ConfigField(AppStrings.smtpPort, 'smtp_port', false, '587', FieldKind.port),
          ConfigField(AppStrings.imapServer, 'imap_server', false, 'imap.gmail.com', FieldKind.text),
          ConfigField(AppStrings.imapPort, 'imap_port', false, '993', FieldKind.port),
          ConfigField(AppStrings.authStatus, 'auth_status', false, 'unauthorized', FieldKind.authStatus),
        ];
      case 'outlook.com':
        return [
          ConfigField(AppStrings.emailAddress, 'email', false, '', FieldKind.text),
          ConfigField(AppStrings.accountType, 'account_type', false, 'personal', FieldKind.accountType),
          ConfigField(AppStrings.smtpServer, 'smtp_server', false, 'smtp-mail.outlook.com', FieldKind.text),
          ConfigField(AppStrings.smtpPort, 'smtp_port', false, '587', FieldKind.port),
          ConfigField(AppStrings.imapServer, 'imap_server', false, 'outlook.office365.com', FieldKind.text),
          ConfigField(AppStrings.imapPort, 'imap_port', false, '993', FieldKind.port),
          ConfigField(AppStrings.authStatus, 'auth_status', false, 'unauthorized', FieldKind.authStatus),
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
