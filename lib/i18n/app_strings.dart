import 'dart:ui';

class AppStrings {
  static bool get isZh {
    final locale = PlatformDispatcher.instance.locale;
    return locale.languageCode == 'zh';
  }

  static String get appTitle => isZh ? 'OIM' : 'OIM';
  static String get mail => isZh ? '邮件' : 'Mail';
  static String get settings => isZh ? '设置' : 'Settings';
  static String get me => isZh ? '我' : 'Me';
  static String get contacts => isZh ? '通讯录' : 'Contacts';
  static String get delete => isZh ? '删除' : 'Delete';
  static String get cancel => isZh ? '取消' : 'Cancel';
  static String get create => isZh ? '创建' : 'Create';
  static String get save => isZh ? '保存' : 'Save';
  static String get edit => isZh ? '编辑' : 'Edit';
  static String get add => isZh ? '添加' : 'Add';
  static String get browse => isZh ? '浏览' : 'Browse';
  static String get done => isZh ? '完成' : 'Done';
  static String get authorize => isZh ? '授权' : 'Authorize';
  static String get send => isZh ? '发送' : 'Send';
  static String get search => isZh ? '搜索' : 'Search';
  static String get reply => isZh ? '回复' : 'Reply';
  static String get replyAll => isZh ? '回复全部' : 'Reply All';
  static String get forward => isZh ? '转发' : 'Forward';
  static String get conversation => isZh ? '会话' : 'Conversation';
  static String get emailDetail => isZh ? '邮件详情' : 'Email Detail';

  // Email list
  static String get noEmails => isZh ? '暂无邮件' : 'No emails';
  static String get noConversations => isZh ? '暂无会话' : 'No conversations';
  static String get inbox => isZh ? '收件箱' : 'Inbox';
  static String get sent => isZh ? '已发送' : 'Sent';
  static String get unknownAccount => isZh ? '未知邮箱' : 'Unknown';
  static String get noSubject => isZh ? '无主题' : 'No Subject';
  static String get andMore => isZh ? '人' : ' more';

  // Settings
  static String get account => isZh ? '账户' : 'Account';
  static String get accountManagement => isZh ? '账户管理' : 'Account Management';
  static String get accountManagementDesc => isZh ? '添加、编辑或删除邮箱账户' : 'Add, edit or remove email accounts';
  static String get accountSecurity => isZh ? '账户安全' : 'Account Security';
  static String get accountSecurityDesc => isZh ? '密码、两步验证等' : 'Password, 2FA, etc.';
  static String get dataSync => isZh ? '数据同步' : 'Data Sync';
  static String get dataSyncDesc => isZh ? '管理云端数据同步' : 'Manage cloud data sync';
  static String get username => isZh ? '用户名' : 'Username';
  static String get appearance => isZh ? '外观' : 'Appearance';
  static String get darkMode => isZh ? '深色模式' : 'Dark Mode';
  static String get darkModeDesc => isZh ? '启用深色主题' : 'Enable dark theme';
  static String get language => isZh ? '语言' : 'Language';
  static String get fontSize => isZh ? '字体大小' : 'Font Size';
  static String get fontSizeMedium => isZh ? '中' : 'Medium';

  // Create session dialog
  static String get newSession => isZh ? '新建会话' : 'New Session';
  static String get sessionTitle => isZh ? '会话标题' : 'Session Title';
  static String get enterSessionTitle => isZh ? '输入会话标题' : 'Enter session title';
  static String get selectAccount => isZh ? '选择要使用的账户' : 'Select account to use';
  static String get sessionMembers => isZh ? '会话成员' : 'Session Members';
  static String get enterEmail => isZh ? '输入邮箱地址' : 'Enter email address';
  static String get noMembers => isZh ? '暂未添加成员' : 'No members added';
  static String get pleaseEnterTitle => isZh ? '请输入会话标题' : 'Please enter session title';
  static String get pleaseSelectAccount => isZh ? '请选择账户' : 'Please select an account';
  static String get pleaseAddMember => isZh ? '请添加至少一个会话成员' : 'Please add at least one member';
  static String get cannotLoadConfig => isZh ? '无法加载账户配置' : 'Cannot load account config';
  static String get accountNotFound => isZh ? '未找到选中账户的配置' : 'Selected account config not found';
  static String get createSessionFailed => isZh ? '创建会话失败' : 'Failed to create session';
  static String get sessionCreatedSuccess => isZh ? '会话创建成功' : 'Session created successfully';
  static String get emailSendFailed => isZh ? '邮件发送失败' : 'Email send failed';

  // Encryption
  static String get encryptionMethod => isZh ? '加密方式' : 'Encryption Method';
  static String get encryptionNone => isZh ? '不加密' : 'None';
  static String get encryptionStandard => isZh ? '普通加密' : 'Standard Encryption';
  static String get startNewSession => isZh ? '开始一个新的会话' : 'Start a new session';
  static String get keyExchange => isZh ? '密钥交换' : 'Key Exchange';

  // Conversation view
  static String get newConversation => isZh ? '新会话' : 'New Conversation';
  static String get sessionCreatedWaiting => isZh ? '会话已创建，等待邮件到达' : 'Session created, waiting for emails';
  static String get conversationMembers => isZh ? '会话成员' : 'Members';
  static String get sendMessageHint => isZh ? '发送消息...' : 'Send a message...';
  static String get noRecipients => isZh ? '没有可发送的收件人' : 'No recipients to send to';
  static String get sending => isZh ? '正在发送...' : 'Sending...';
  static String get messageSent => isZh ? '消息已发送' : 'Message sent';
  static String get sendFailed => isZh ? '发送失败' : 'Send failed';
  static String get attachment => isZh ? '附件' : 'Attachment';
  static String get downloading => isZh ? '下载中...' : 'Downloading...';
  static String get createEmailInstanceFailed => isZh ? '创建邮箱实例失败' : 'Failed to create email instance';
  static String get exceptionLabel => isZh ? '异常' : 'Exception';

  // Email detail
  static String get emailDownloading => isZh ? '邮件内容下载中...' : 'Downloading email content...';
  static String get noContent => isZh ? '无内容' : 'No content';
  static String get attachmentsCount => isZh ? '个附件' : 'attachments';
  static String get replySent => isZh ? '回复已发送' : 'Reply sent';

  // Email config dialog
  static String get emailConfig => isZh ? '邮箱配置' : 'Email Configuration';
  static String get localDataPath => isZh ? '本地数据存储位置' : 'Local data path';
  static String get emailType => isZh ? '邮箱类型' : 'Email type';
  static String get maxAccountsReached => isZh ? '最多支持添加5个邮箱配置' : 'Maximum 5 email accounts supported';
  static String get noEmailConfig => isZh ? '暂无邮箱配置，请选择类型并点击添加' : 'No email config, select type and click Add';
  static String get fieldCannotBeEmpty => isZh ? '不能为空' : 'cannot be empty';
  static String get configSaved => isZh ? '配置已保存' : 'Config saved';
  static String get saveFailed => isZh ? '保存失败，请重试' : 'Save failed, please retry';
  static String get authorizing => isZh ? '正在授权，请在浏览器中完成授权...' : 'Authorizing, please complete in browser...';
  static String get createConfigFailed => isZh ? '创建邮箱配置失败' : 'Failed to create email config';
  static String get authorizeSuccess => isZh ? '授权成功' : 'Authorization successful';
  static String get authorizeSuccessNoEmail => isZh ? '授权成功（但未获取到邮箱地址或刷新令牌）' : 'Authorized (but no email or token received)';
  static String get authorizeFailed => isZh ? '授权失败，请重试' : 'Authorization failed, please retry';
  static String get authorizeException => isZh ? '授权异常' : 'Authorization exception';
  static String get accountIndexError => isZh ? '账户索引错误' : 'Account index error';
  static String get authorized => isZh ? '已授权' : 'Authorized';
  static String get unauthorized => isZh ? '未授权' : 'Unauthorized';
  static String get manualPathHint => isZh ? '请手动输入本地存储路径' : 'Please enter local storage path manually';

  // Config fields
  static String get emailAddress => isZh ? '邮箱地址' : 'Email Address';
  static String get authCode => isZh ? '授权码' : 'Auth Code';
  static String get smtpServer => isZh ? 'SMTP服务器' : 'SMTP Server';
  static String get smtpPort => isZh ? 'SMTP端口' : 'SMTP Port';
  static String get imapServer => isZh ? 'IMAP服务器' : 'IMAP Server';
  static String get imapPort => isZh ? 'IMAP端口' : 'IMAP Port';
  static String get accountType => isZh ? '账户类型' : 'Account Type';
  static String get authStatus => isZh ? '授权状态' : 'Auth Status';

  // Provider names
  static String get netease163 => isZh ? '网易163邮箱' : 'NetEase 163 Mail';
  static String get qqMail => isZh ? 'QQ邮箱' : 'QQ Mail';

  // Email utils
  static String get today => isZh ? '今天' : 'Today';
  static String get yesterday => isZh ? '昨天' : 'Yesterday';
  static String get unknown => isZh ? '未知' : 'Unknown';
  static List<String> get weekdays => isZh
      ? ['周一', '周二', '周三', '周四', '周五', '周六', '周日']
      : ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

  // 163 unsafe login
  static String get unsafeLogin163 => isZh
      ? '163 IMAP 报告不安全登录。请在163邮箱设置中重新生成授权码。'
      : '163 IMAP reported unsafe login. Please regenerate the auth code in 163 mail settings.';

  // Contacts
  static String get searchContacts => isZh ? '搜索联系人' : 'Search contacts';
  static String get allContacts => isZh ? '全部联系人' : 'All Contacts';
  static String get addContact => isZh ? '添加联系人' : 'Add Contact';
  static String get contactName => isZh ? '姓名' : 'Name';
  static String get contactEmail => isZh ? '邮箱' : 'Email';
  static String get contactCategory => isZh ? '分类' : 'Category';
  static String get contactNotes => isZh ? '备注' : 'Notes';
  static String get enterName => isZh ? '请输入姓名' : 'Enter name';
  static String get enterNotes => isZh ? '请输入备注' : 'Enter notes';
  static String get selectOrInputCategory => isZh ? '选择或输入分类' : 'Select or input category';
  static String get contactSaved => isZh ? '联系人已保存' : 'Contact saved';
}
