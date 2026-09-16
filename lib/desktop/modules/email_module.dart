import 'dart:io';
import 'dart:convert';
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
import 'package:emoji_picker_flutter/emoji_picker_flutter.dart';
import 'package:desktop_drop/desktop_drop.dart';
import 'package:path_provider/path_provider.dart';
import '../../native/email_core.dart' as native;
import '../../native/email_background_service.dart';
import 'email_utils.dart';
import 'email_module_base.dart';
import 'email_list_view.dart';
import 'email_detail_view.dart';
import 'conversation_view.dart';
import 'eml_parser.dart';
import '../../i18n/app_strings.dart';

export 'email_module_base.dart';

class EmailModuleState extends State<EmailModule>
    with EmailListViewMixin, EmailDetailViewMixin, ConversationViewMixin {
  static const String _myAddress = 'me@oim.local';

  int _selectedEmail = 0;
  String? _selectedConversationMessageId;
  bool _isConversationView = false;
  bool _showConversationPanel = false;
  bool _showEmojiPicker = false;
  bool _isDragging = false;
  final TextEditingController _searchController = TextEditingController();
  late final RichTextReplyController _replyController;
  final List<DroppedFile> _droppedFiles = [];

  late native.EmailInbox _inbox;
  final Set<int> _unreadIndices = {0, 1};
  List<native.EmailMessage> _emails = [];
  String _searchQuery = '';

  List<native.EmailMessage> _conversationEmails = [];
  List<native.EmailMessage> _inboxEmails = [];
  List<native.EmailMessage> _sentEmails = [];

  // Group messaging state
  List<Map<String, dynamic>> _groupList = [];
  String? _selectedGroupId;
  bool _isGroupView = false;
  bool _showGroupMembers = false;
  List<native.EmailMessage> _groupMessages = [];

  final Set<String> _collapsedSections = {};
  final Set<String> _collapsedGroups = {};

  final Map<String, int> _configIndexMap = {};

  String _emailDataPath = '';
  String _dbPath = '';
  String _configPath = '';

  double _listWidth = 288;

  EmailBackgroundService? _bgService;

  // --- Mixin property implementations ---
  @override
  List<native.EmailMessage> get emails => _emails;
  @override
  List<native.EmailMessage> get conversationEmails => _conversationEmails;
  @override
  Set<String> get collapsedSections => _collapsedSections;
  @override
  Set<String> get collapsedGroups => _collapsedGroups;
  @override
  double get listWidth => _listWidth;
  @override
  set listWidth(double v) => _listWidth = v;
  @override
  String get searchQuery => _searchQuery;
  @override
  set searchQuery(String v) => _searchQuery = v;
  @override
  TextEditingController get searchController => _searchController;
  @override
  List<DroppedFile> get droppedFiles => _droppedFiles;
  @override
  TextEditingController get replyController => _replyController;
  @override
  String get configPath => _configPath;
  @override
  String get emailDataPath => _emailDataPath;
  @override
  Map<String, int> get configIndexMap => _configIndexMap;
  @override
  VoidCallback? get onRefresh => _loadEmailsFromDb;
  @override
  int get selectedEmail => _selectedEmail;
  @override
  set selectedEmail(int v) => _selectedEmail = v;
  @override
  String? get selectedConversationMessageId => _selectedConversationMessageId;
  @override
  set selectedConversationMessageId(String? v) => _selectedConversationMessageId = v;
  @override
  bool get isConversationView => _isConversationView;
  @override
  set isConversationView(bool v) => _isConversationView = v;
  @override
  Set<int> get unreadIndices => _unreadIndices;
  @override
  bool get showConversationPanel => _showConversationPanel;
  @override
  set showConversationPanel(bool v) => _showConversationPanel = v;
  @override
  bool get showEmojiPicker => _showEmojiPicker;
  @override
  set showEmojiPicker(bool v) => _showEmojiPicker = v;

  // Group state implementations
  @override
  List<Map<String, dynamic>> get groupList => _groupList;
  @override
  String? get selectedGroupId => _selectedGroupId;
  @override
  set selectedGroupId(String? v) => _selectedGroupId = v;
  @override
  bool get isGroupView => _isGroupView;
  @override
  set isGroupView(bool v) => _isGroupView = v;
  @override
  List<native.EmailMessage> get groupMessages => _groupMessages;
  @override
  void loadGroupList() {
    final allGroups = <Map<String, dynamic>>[];
    final seen = <String>{};
    for (final account in _configAccounts()) {
      try {
        final result = native.EmailCore.groupList(account);
        final json = jsonDecode(result);
        if (json['status'] == 'success') {
          for (final g in json['groups']) {
            final gid = g['group_id']?.toString() ?? '';
            if (gid.isNotEmpty && seen.add(gid)) {
              allGroups.add(g as Map<String, dynamic>);
            }
          }
        }
      } catch (e) {
        native.EmailCore.logWrite('[Group] loadGroupList error: $e');
      }
    }
    setState(() => _groupList = allGroups);
  }
  @override
  void loadGroupMessages(String groupId) {
    final messages = <native.EmailMessage>[];
    for (final account in _configAccounts()) {
      try {
        final result = native.EmailCore.queryLocalemail(account);
        final json = jsonDecode(result);
        if (json['status'] == 'success') {
          final emails = json['emails'] as List;
          for (final e in emails) {
            final sessionId = e['session_id']?.toString() ?? '';
            if (sessionId == 'group_$groupId') {
              final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
              final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
              messages.add(native.EmailMessage(
                sender: e['from'] ?? e['from_addr'] ?? e['sender'] ?? '',
                recipient: account,
                subject: e['subject'] ?? '',
                body: e['bodystructure'] ?? '',
                timestamp: e['date'] ?? '',
                uuid: e['uuid']?.toString() ?? '',
                flags: flags is List ? flags.cast<String>() : [],
                isAnswered: isAnswered,
                inReplyTo: e['in_reply_to']?.toString() ?? '',
                messageId: e['message_id']?.toString() ?? '',
                folder: e['folder']?.toString() ?? 'INBOX',
                isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
                sessionId: sessionId,
                rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
                toAddr: e['to_addr']?.toString() ?? '',
                file: e['file']?.toString() ?? '',
                account: e['account']?.toString() ?? '',
                groupId: groupId,
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
              ));
            }
          }
        }
      } catch (e) {
        native.EmailCore.logWrite('[Group] loadGroupMessages error: $e');
      }
    }
    // Order by the in_reply_to chain (rowid only as tie-breaker)
    setState(() => _groupMessages = sortByReplyChain(messages));
  }

  List<String> _configAccounts() {
    final config = native.EmailCore.loadConfig(_configPath);
    return config?.accounts
            .where((a) => a.email.isNotEmpty)
            .map((a) => a.email)
            .toList() ??
        <String>[];
  }

  @override
  void initState() {
    super.initState();
    _replyController = RichTextReplyController(droppedFiles: _droppedFiles);
    _replyController.onChangedCallback = () => setState(() {});
    native.EmailCore.initialize();
    _inbox = native.EmailInbox(capacity: 50);
    _initPaths();
    
    // Periodically check for email sent notifications
    Timer.periodic(const Duration(milliseconds: 500), (timer) {
      if (!mounted) {
        timer.cancel();
        return;
      }
      final sentNotifications = native.EmailCore.getEmailSentNotifications();
      for (final notification in sentNotifications) {
        _logToFile('Email sent notification: ${notification}');
        final sessionId = notification['session_id'] as String?;
        // If we're in conversation view and the notification matches the current session, reload that session
        if (_isConversationView && sessionId != null && sessionId == _selectedConversationMessageId) {
          _logToFile('Reloading conversation: $sessionId');
          // Reload emails from database
          _loadEmailsFromDb();
        } else {
          // Otherwise reload all emails
          _loadEmailsFromDb();
        }
      }
    });

    // Periodically refresh conversation view for file transfer progress updates
    Timer.periodic(const Duration(seconds: 2), (timer) {
      if (!mounted) {
        timer.cancel();
        return;
      }
      if (_isConversationView) {
        setState(() {});
      }
    });
  }

  Future<void> _initPaths() async {
    final appDir = await getApplicationSupportDirectory();
    final configDir = Directory('${appDir.path}/config');
    final dataDir = Directory('${appDir.path}/data');

    if (!configDir.existsSync()) {
      configDir.createSync(recursive: true);
    }

    if (!dataDir.existsSync()) {
      dataDir.createSync(recursive: true);
    }

    setState(() {
      _configPath = '${configDir.path}/oim.conf';
      _dbPath = '${dataDir.path}/emails.db';
      _emailDataPath = dataDir.path;
    });

    _logToFile('Config path: $_configPath');
    _logToFile('Database path: $_dbPath');

    final dbInitResult = native.EmailCore.initDatabase(_dbPath);
    _logToFile('Database init result: $dbInitResult');

    // Generate session records for all existing accounts
    _logToFile('Generating session records for all accounts...');
    try {
      final config = native.EmailCore.loadConfig(_configPath);
      if (config != null) {
        for (final account in config.accounts) {
          if (account.email.isNotEmpty) {
            final result = native.EmailCore.generateSessions(account.email);
            _logToFile('generateSessions for ${account.email} result: $result');
          }
        }
      }
    } catch (e) {
      _logToFile('Error generating sessions: $e');
    }

    if (_inbox.count == 0) {
      _seedSampleEmails();
    }

    await _initLibemail();
  }

  Future<void> _initLibemail() async {
    _logToFile('_initLibemail START');

    // Clear EML cache to force fresh parsing with updated decryption logic
    clearEmlCache();
    _logToFile('Cleared EML cache');

    // Clear memory data to force reload from database with file field
    _emails.clear();
    _conversationEmails.clear();
    _inboxEmails.clear();
    _sentEmails.clear();
    _logToFile('Cleared memory data');

    _logToFile('About to call _loadEmailsFromDb...');
    await _loadEmailsFromDb();
    _logToFile('_loadEmailsFromDb done');

    _logToFile('About to start background service...');
    _startBackgroundService();
    _logToFile('Background service started');

    _logToFile('_initLibemail END');
  }

  void _startBackgroundService() {
    if (_bgService != null) return;
    _logToFile('Starting background email service...');
    _bgService = EmailBackgroundService(
      onMessage: (msg) async {
        switch (msg.type) {
          case 'new_emails':
            _logToFile('Background: new emails for ${msg.account}, count=${msg.data?['count']}');
            clearEmlCache();
            await _loadEmailsFromDb();
            if (_selectedGroupId != null) {
              loadGroupMessages(_selectedGroupId!);
            }
            break;
          case 'email_sent':
            _logToFile('Background: email sent for ${msg.account}, message_id=${msg.data?['message_id']}');
            clearEmlCache();
            await _loadEmailsFromDb();
            if (_selectedGroupId != null) {
              loadGroupMessages(_selectedGroupId!);
            }
            break;
          case 'log':
            _logToFile('Background: ${msg.data?['msg']}');
            break;
          case 'error':
            _logToFile('Background error: ${msg.data?['error']} for ${msg.account}');
            if (msg.data?['error'] == '163_unsafe_login' && mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(
                  content: Text(AppStrings.unsafeLogin163),
                  backgroundColor: Colors.red,
                  duration: const Duration(seconds: 10),
                ),
              );
            }
            break;
          case 'child_exited':
            _logToFile('Background: child exited for ${msg.account}');
            break;
          case 'bodies_downloaded':
            _logToFile('Background: downloaded ${msg.data?['count']} email bodies for ${msg.account}');
            if (msg.data?['file_completes'] != null) {
              final completes = msg.data!['file_completes'] as List;
              for (final fc in completes) {
                _logToFile('Background: file download complete, file_id=${fc['file_id']}');
              }
            }
            clearEmlCache();
            await _loadEmailsFromDb();
            if (_selectedGroupId != null) {
              loadGroupMessages(_selectedGroupId!);
            }
            break;
          default:
            _logToFile('Background: ${msg.type} ${msg.data}');
        }
      },
    );
    _bgService!.start(_configPath);
  }

  @override
  void fetchEmailsFromAccounts() async {
    _logToFile('=== _fetchEmailsFromAccounts START ===');
    final config = native.EmailCore.loadConfig(_configPath);
    _logToFile('loadConfig result: ${config != null}, accounts: ${config?.accounts.length ?? 0}');
    if (config == null || config.accounts.isEmpty) {
      _logToFile('No config or no accounts found, trying direct connection with known account');
      try {
        final configFile = File(_configPath);
        if (configFile.existsSync()) {
          final content = configFile.readAsStringSync();
          final decoded = jsonDecode(content);
          final accounts = decoded['accounts'] as List;
          for (final account in accounts) {
            final email = account['email'] as String? ?? '';
            final authCode = account['auth_code'] as String? ?? '';
            if (email.isNotEmpty && authCode.isNotEmpty) {
              _logToFile('Direct connect: $email');
              await _connectAndFetchEmails(email, authCode);
            }
          }
        } else {
          _logToFile('Config file does not exist: $_configPath');
        }
      } catch (e) {
        _logToFile('Fallback config read error: $e');
      }
      return;
    }

    final allParsedEmails = <native.EmailMessage>[];
    final allConversationRoots = <native.EmailMessage>[];

    for (final account in config.accounts) {
      _logToFile('Processing account: ${account.email}');
      if (account.email.isNotEmpty && account.authCode.isNotEmpty) {
        final result = await _connectAndFetchEmails(account.email, account.authCode);
        if (result != null) {
          allParsedEmails.addAll(result.$1);
          allConversationRoots.addAll(result.$2);
        }
      } else {
        _logToFile('Account missing required fields');
      }
    }

    final nonSentEmails = allParsedEmails.where((e) => e.folder != 'Sent' && e.folder != 'SENT').toList();
    final sentEmails = allParsedEmails.where((e) => e.folder == 'Sent' || e.folder == 'SENT').toList();

    setState(() {
      _emails = allParsedEmails;
      _conversationEmails = allConversationRoots;
      _inboxEmails = nonSentEmails;
      _sentEmails = sentEmails;
      if (_selectedEmail >= _emails.length) {
        _selectedEmail = _emails.isEmpty ? 0 : _emails.length - 1;
      }
    });
    _logToFile('UI updated with ${_emails.length} emails (${_conversationEmails.length} conversations)');

    _logToFile('=== _fetchEmailsFromAccounts END ===');
  }

  void _logToFile(String msg) {
    native.EmailCore.logWrite('[Dart] $msg');
  }

  Future<(List<native.EmailMessage>, List<native.EmailMessage>)?> _connectAndFetchEmails(String email, String authCode) async {
    try {
      _logToFile('=== _connectAndFetchEmails START for $email ===');

      int configIndex;
      if (_configIndexMap.containsKey(email)) {
        configIndex = _configIndexMap[email]!;
        _logToFile('Reusing existing configIndex=$configIndex for $email');
      } else {
        configIndex = native.EmailCore.oemailimOpenNewEmail("163.com");
        _logToFile('oemailimOpenNewEmail result: $configIndex');
        if (configIndex < 0) {
          _logToFile('Failed to open new email: $configIndex');
          return null;
        }
        _configIndexMap[email] = configIndex;
      }

      // Load config to get SMTP server settings
      final config = native.EmailCore.loadConfig(_configPath);
      if (config != null && config.accounts.isNotEmpty) {
        for (final account in config.accounts) {
          if (account.email == email && account.smtpServer.isNotEmpty) {
            _logToFile('Setting SMTP server: ${account.smtpServer}:${account.smtpPort}');
            native.EmailCore.oemailimSetSmtpServer(configIndex, account.smtpServer, account.smtpPort);
            break;
          }
        }
      }

      final fetchResult = await compute(fetchEmailsInIsolate, {
        'email': email,
        'authCode': authCode,
        'configIndex': configIndex,
        'storageDir': _emailDataPath,
      });
      _logToFile('fetchAndStoreEmails result: $fetchResult');

      try {
        final fetchDecoded = jsonDecode(fetchResult);
        if (fetchDecoded['status'] == 'failed') {
          if (fetchDecoded['error'] == '163_unsafe_login') {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text(AppStrings.unsafeLogin163),
                backgroundColor: Colors.red,
                duration: const Duration(seconds: 10),
              ),
            );
          }
          _logToFile('Fetch failed: ${fetchDecoded['error']}');
          return null;
        }
      } catch (e) {
        _logToFile('Failed to parse fetch result: $e');
      }

      _logToFile('Querying localemail table...');
      final queryResult = native.EmailCore.queryLocalemail(email);
      _logToFile('queryLocalemail result: $queryResult');

      // Generate session records for existing emails
      _logToFile('Generating session records for existing emails...');
      final generateSessionsResult = native.EmailCore.generateSessions(email);
      _logToFile('generateSessions result: $generateSessionsResult');

      try {
        final decoded = jsonDecode(queryResult);
        if (decoded['status'] == 'success') {
          final emails = decoded['emails'] as List;
          _logToFile('Found ${emails.length} emails in localemail table');
          
          final parsedEmails = emails.map((e) {
            final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
            final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
            final fileField = e['file']?.toString() ?? '';
            _logToFile('Parsed email: uuid=${e['uuid']}, file=$fileField, islocal=${e['islocal']}');
            return native.EmailMessage(
              sender: e['from'] ?? e['sender'] ?? '',
              recipient: email,
              subject: e['subject'] ?? '',
              body: e['bodystructure'] ?? '',
              timestamp: e['date'] ?? '',
              uuid: e['uuid']?.toString() ?? '',
              flags: flags is List ? flags.cast<String>() : [],
              isAnswered: isAnswered,
              inReplyTo: e['in_reply_to']?.toString() ?? '',
              messageId: e['message_id']?.toString() ?? '',
              folder: e['folder']?.toString() ?? 'INBOX',
              isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
              sessionId: e['session_id']?.toString() ?? '',
              rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
              toAddr: e['to_addr']?.toString() ?? '',
              file: fileField,
              account: e['account']?.toString() ?? '',
                groupId: e['group_id']?.toString() ?? '',
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
              visible: e['visible'] is int ? e['visible'] : (int.tryParse(e['visible']?.toString() ?? '1') ?? 1),
            );
          }).toList();
          
          // Query conversation roots from database
          final threadRootsResult = native.EmailCore.queryThreadRoots(email);
          final threadRootsDecoded = jsonDecode(threadRootsResult);
          final conversationRoots = <native.EmailMessage>[];
          if (threadRootsDecoded['status'] == 'success') {
            final threadRoots = threadRootsDecoded['emails'] as List;
            conversationRoots.addAll(threadRoots.map((e) {
              final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
              final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
              return native.EmailMessage(
                sender: e['from'] ?? e['from_addr'] ?? e['sender'] ?? '',
                recipient: email,
                subject: e['subject'] ?? '',
                body: e['bodystructure'] ?? '',
                timestamp: e['date'] ?? '',
                uuid: e['uuid']?.toString() ?? '',
                flags: flags is List ? flags.cast<String>() : [],
                isAnswered: isAnswered,
                inReplyTo: e['in_reply_to']?.toString() ?? '',
                messageId: e['message_id']?.toString() ?? '',
                folder: e['folder']?.toString() ?? 'INBOX',
                isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
                sessionId: e['session_id']?.toString() ?? '',
                rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
                toAddr: e['to_addr']?.toString() ?? '',
                file: e['file']?.toString() ?? '',
                account: e['account']?.toString() ?? '',
                groupId: e['group_id']?.toString() ?? '',
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
                visible: e['visible'] is int ? e['visible'] : (int.tryParse(e['visible']?.toString() ?? '1') ?? 1),
              );
            }).toList());
          }

          _logToFile('Parsed ${parsedEmails.length} emails, ${conversationRoots.length} conversation roots for $email');
          return (parsedEmails, conversationRoots);
        }
      } catch (e) {
        _logToFile('Failed to parse localemail query result: $e');
      }

      _logToFile('=== _connectAndFetchEmails END ===');
    } catch (e, stack) {
      _logToFile('Exception in _connectAndFetchEmails: $e');
      _logToFile('Stack: $stack');
    }
    return null;
  }

  void reloadFromDb() {
    _logToFile('reloadFromDb called, configPath=$_configPath');
    final config = native.EmailCore.loadConfig(_configPath);
    if (config == null) {
      _logToFile('reloadFromDb: config is null');
      return;
    }

    // Generate session records for all accounts
    for (final account in config.accounts) {
      if (account.email.isNotEmpty) {
        try {
          native.EmailCore.generateSessions(account.email);
        } catch (e) {
          _logToFile('reloadFromDb: generateSessions error for ${account.email}: $e');
        }
      }
    }

    final allEmails = <native.EmailMessage>[];

    for (final account in config.accounts) {
      if (account.email.isNotEmpty) {
        _logToFile('reloadFromDb: querying localemail for ${account.email}');
        final queryResult = native.EmailCore.queryLocalemail(account.email);
        _logToFile('reloadFromDb: queryLocalemail result=$queryResult');

        try {
          final decoded = jsonDecode(queryResult);
          if (decoded['status'] == 'success') {
            final emails = decoded['emails'] as List;
            final parsedEmails = emails.map((e) {
              final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
              final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
              return native.EmailMessage(
                sender: e['from'] ?? e['from_addr'] ?? e['sender'] ?? '',
                recipient: account.email,
                subject: e['subject'] ?? '',
                body: e['bodystructure'] ?? '',
                timestamp: e['date'] ?? '',
                uuid: e['uuid']?.toString() ?? '',
                flags: flags is List ? flags.cast<String>() : [],
                isAnswered: isAnswered,
                inReplyTo: e['in_reply_to']?.toString() ?? '',
                messageId: e['message_id']?.toString() ?? '',
                folder: e['folder']?.toString() ?? 'INBOX',
                isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
                sessionId: e['session_id']?.toString() ?? '',
                rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
                toAddr: e['to_addr']?.toString() ?? '',
                file: e['file']?.toString() ?? '',
                account: e['account']?.toString() ?? '',
                groupId: e['group_id']?.toString() ?? '',
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
                visible: e['visible'] is int ? e['visible'] : (int.tryParse(e['visible']?.toString() ?? '1') ?? 1),
              );
            }).toList();
            allEmails.addAll(parsedEmails);
          }
        } catch (e) {
          _logToFile('reloadFromDb: failed to parse result: $e');
        }
      }
    }

    final nonSentEmails = allEmails.where((e) => e.folder != 'Sent' && e.folder != 'SENT').toList();
    final sentEmails = allEmails.where((e) => e.folder == 'Sent' || e.folder == 'SENT').toList();
    // Query conversation roots from database for each account
    final conversationRoots = <native.EmailMessage>[];
    for (final account in config.accounts) {
      if (account.email.isNotEmpty) {
        try {
          final threadRootsResult = native.EmailCore.queryThreadRoots(account.email);
          final threadRootsDecoded = jsonDecode(threadRootsResult);
          if (threadRootsDecoded['status'] == 'success') {
            final threadRoots = threadRootsDecoded['emails'] as List;
            for (final e in threadRoots) {
              final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
              final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
              conversationRoots.add(native.EmailMessage(
                sender: e['from'] ?? e['from_addr'] ?? e['sender'] ?? '',
                recipient: account.email,
                subject: e['subject'] ?? '',
                body: e['bodystructure'] ?? '',
                timestamp: e['date'] ?? '',
                uuid: e['uuid']?.toString() ?? '',
                flags: flags is List ? flags.cast<String>() : [],
                isAnswered: isAnswered,
                inReplyTo: e['in_reply_to']?.toString() ?? '',
                messageId: e['message_id']?.toString() ?? '',
                folder: e['folder']?.toString() ?? 'INBOX',
                isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
                sessionId: e['session_id']?.toString() ?? '',
                rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
                toAddr: e['to_addr']?.toString() ?? '',
                file: e['file']?.toString() ?? '',
                account: e['account']?.toString() ?? '',
                groupId: e['group_id']?.toString() ?? '',
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
              ));
            }
          }
        } catch (e) {
          _logToFile('reloadFromDb: queryThreadRoots error for ${account.email}: $e');
        }
      }
    }

    setState(() {
      _emails = allEmails;
      _conversationEmails = conversationRoots;
      _inboxEmails = nonSentEmails;
      _sentEmails = sentEmails;
      if (_selectedEmail >= _emails.length) {
        _selectedEmail = _emails.isEmpty ? 0 : _emails.length - 1;
      }
    });
    _logToFile('reloadFromDb: UI updated with ${_emails.length} total emails (${_conversationEmails.length} conversations, ${_inboxEmails.length} inbox, ${_sentEmails.length} sent)');

    // Load group list
    loadGroupList();
  }

  Future<void> _loadEmailsFromDb() async {
    _logToFile('=== _loadEmailsFromDb START ===');
    _logToFile('[EmailModule] _loadEmailsFromDb called - reloading from database');
    final config = native.EmailCore.loadConfig(_configPath);
    if (config == null || config.accounts.isEmpty) {
      _logToFile('No config or no accounts found');
      return;
    }

    _logToFile('_loadEmailsFromDb: config loaded, accounts=${config.accounts.length}');

    // Yield to let UI render before heavy work
    await Future.delayed(Duration.zero);

    // Generate session records for all accounts
    for (final account in config.accounts) {
      if (account.email.isNotEmpty) {
        try {
          native.EmailCore.generateSessions(account.email);
        } catch (e) {
          _logToFile('_loadEmailsFromDb: generateSessions error for ${account.email}: $e');
        }
      }
    }

    List<native.EmailMessage> allEmails = [];

    for (final account in config.accounts) {
      if (account.email.isNotEmpty) {
        _logToFile('_loadEmailsFromDb: querying localemail for ${account.email}');
        final jsonStr = native.EmailCore.queryLocalemail(account.email);

        try {
          final decoded = jsonDecode(jsonStr);
          if (decoded['status'] == 'success') {
            final emails = decoded['emails'] as List;
            for (final e in emails) {
              final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
              final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
              allEmails.add(native.EmailMessage(
                sender: e['from'] ?? e['from_addr'] ?? e['sender'] ?? '',
                recipient: account.email,
                subject: e['subject'] ?? '',
                body: e['bodystructure'] ?? '',
                timestamp: e['date'] ?? '',
                uuid: e['uuid']?.toString() ?? '',
                flags: flags is List ? flags.cast<String>() : [],
                isAnswered: isAnswered,
                inReplyTo: e['in_reply_to']?.toString() ?? '',
                messageId: e['message_id']?.toString() ?? '',
                folder: e['folder']?.toString() ?? 'INBOX',
                isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
                sessionId: e['session_id']?.toString() ?? '',
                rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
                toAddr: e['to_addr']?.toString() ?? '',
                file: e['file']?.toString() ?? '',
                account: e['account']?.toString() ?? '',
                groupId: e['group_id']?.toString() ?? '',
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
              ));
            }
          }
        } catch (e) {
          _logToFile('_loadEmailsFromDb: parse error: $e');
        }
        // Yield between accounts
        await Future.delayed(Duration.zero);
      }
    }

    final nonSentEmails = allEmails.where((e) => e.folder != 'Sent' && e.folder != 'SENT').toList();
    final sentEmails = allEmails.where((e) => e.folder == 'Sent' || e.folder == 'SENT').toList();

    // Query conversation roots from database for each account
    final conversationRoots = <native.EmailMessage>[];
    for (final account in config.accounts) {
      if (account.email.isNotEmpty) {
        try {
          final threadRootsResult = native.EmailCore.queryThreadRoots(account.email);
          _logToFile('_loadEmailsFromDb: queryThreadRoots for ${account.email}: $threadRootsResult');
          final threadRootsDecoded = jsonDecode(threadRootsResult);
          if (threadRootsDecoded['status'] == 'success') {
            final threadRoots = threadRootsDecoded['emails'] as List;
            for (final e in threadRoots) {
              final flags = (e['flags'] is String && (e['flags'] as String).isNotEmpty) ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
              final isAnswered = flags is List && flags.any((f) => f == '\\Answered');
              conversationRoots.add(native.EmailMessage(
                sender: e['from'] ?? e['from_addr'] ?? e['sender'] ?? '',
                recipient: account.email,
                subject: e['subject'] ?? '',
                body: e['bodystructure'] ?? '',
                timestamp: e['date'] ?? '',
                uuid: e['uuid']?.toString() ?? '',
                flags: flags is List ? flags.cast<String>() : [],
                isAnswered: isAnswered,
                inReplyTo: e['in_reply_to']?.toString() ?? '',
                messageId: e['message_id']?.toString() ?? '',
                folder: e['folder']?.toString() ?? 'INBOX',
                isLocal: e['islocal'] is int ? e['islocal'] : (int.tryParse(e['islocal']?.toString() ?? '0') ?? 0),
                sessionId: e['session_id']?.toString() ?? '',
                rowid: e['rowid'] is int ? e['rowid'] : (int.tryParse(e['rowid']?.toString() ?? '0') ?? 0),
                toAddr: e['to_addr']?.toString() ?? '',
                file: e['file']?.toString() ?? '',
                account: e['account']?.toString() ?? '',
                groupId: e['group_id']?.toString() ?? '',
                xMailer: e['x_mailer']?.toString() ?? '',
                isSent: e['is_sent'] is int ? e['is_sent'] : (int.tryParse(e['is_sent']?.toString() ?? '0') ?? 0),
              ));
            }
          }
        } catch (e) {
          _logToFile('_loadEmailsFromDb: queryThreadRoots error for ${account.email}: $e');
        }
        await Future.delayed(Duration.zero);
      }
    }

    setState(() {
      _emails = allEmails;
      _conversationEmails = conversationRoots;
      _inboxEmails = nonSentEmails;
      _sentEmails = sentEmails;
    });
    _logToFile('_loadEmailsFromDb: loaded ${_emails.length} total emails (${_conversationEmails.length} conversations, ${_inboxEmails.length} inbox, ${_sentEmails.length} sent)');

    // Load group list
    loadGroupList();
  }

  void _seedSampleEmails() {
    _inbox.add(
      sender: '张三',
      recipient: _myAddress,
      subject: '项目进度汇报',
      body: '您好，\n\n附件是本周的项目进度报告，请查收。如果有任何问题，请随时联系我。\n\n谢谢！\n张三',
    );
    _inbox.add(
      sender: '李四',
      recipient: _myAddress,
      subject: '会议邀请',
      body: '您好，\n\n诚邀您参加明天下午3点的产品评审会议。\n\n会议地点：3楼会议室A\n会议时间：明天 15:00-17:00\n\n请准时参加。\n\n李四',
    );
    _inbox.add(
      sender: '系统通知',
      recipient: _myAddress,
      subject: '账户安全提醒',
      body: '尊敬的用户，\n\n您的账户在新设备上登录，如非本人操作，请立即修改密码。\n\n登录地点：北京市\n\n系统管理员',
    );
    _inbox.add(
      sender: '王五',
      recipient: _myAddress,
      subject: '文档分享',
      body: '您好，\n\n分享一份技术文档给您，希望对您有帮助。\n\n文档链接：https://example.com/docs\n\n王五',
    );
  }

  @override
  void saveEmails() {
    final file = File(_emailDataPath);
    file.parent.createSync(recursive: true);
    _inbox.save(_emailDataPath);
  }

  @override
  void refreshEmails() {
    _loadEmailsFromDb();
  }

  @override
  void dispose() {
    _bgService?.stop();
    native.EmailCore.closeDatabase();
    _searchController.dispose();
    _replyController.dispose();
    _inbox.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (isGroupView && selectedGroupId != null) {
      return Row(
        children: [
          buildEmailList(),
          buildDraggableDivider(),
          Expanded(child: buildGroupConversationView()),
        ],
      );
    }
    if (isConversationView && selectedConversationMessageId != null) {
      return Row(
        children: [
          buildEmailList(),
          buildDraggableDivider(),
          buildConversationDetail(),
        ],
      );
    }
    return Row(
      children: [
        buildEmailList(),
        buildDraggableDivider(),
        buildEmailDetail(),
      ],
    );
  }

  Widget buildGroupConversationView() {
    final groupId = selectedGroupId!;
    final messages = groupMessages;

    // Find group info
    Map<String, dynamic>? groupInfo;
    for (final g in groupList) {
      if (g['group_id'] == groupId) {
        groupInfo = g;
        break;
      }
    }
    final subject = groupInfo?['subject'] as String? ?? '群组';
    final members = (groupInfo?['members'] as List?)?.cast<String>() ?? [];
    final owner = groupInfo?['owner'] as String? ?? '';
    final ready = groupInfo?['ready'] as bool? ?? false;
    final groupAccount = groupInfo?['account'] as String? ?? '';

    final conversation = Container(
      color: Colors.white,
      child: Column(
        children: [
          // Header
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: const Color(0xFFF5F5F5),
              border: Border(bottom: BorderSide(color: Colors.grey[300]!)),
            ),
            child: Row(
              children: [
                Icon(Icons.group, color: Colors.green[700], size: 24),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(subject, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
                      Text('${members.length}人', style: TextStyle(fontSize: 12, color: Colors.grey[500])),
                    ],
                  ),
                ),
                if (!ready)
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                    decoration: BoxDecoration(color: Colors.orange[100], borderRadius: BorderRadius.circular(4)),
                    child: Text('等待密钥分发', style: TextStyle(fontSize: 12, color: Colors.orange[700])),
                  ),
                const SizedBox(width: 8),
                IconButton(
                  icon: const Icon(Icons.refresh),
                  onPressed: () => loadGroupMessages(groupId),
                  tooltip: '刷新',
                ),
                IconButton(
                  icon: const Icon(Icons.more_horiz),
                  onPressed: () => setState(() => _showGroupMembers = true),
                  tooltip: '成员',
                ),
              ],
            ),
          ),
          // Messages
          Expanded(
            child: messages.isEmpty
                ? Center(child: Text('暂无消息', style: TextStyle(color: Colors.grey[400])))
                : ListView.builder(
                    itemCount: messages.length,
                    itemBuilder: (context, index) => _buildGroupMessageBubble(messages[index], members),
                  ),
          ),
          // Input bar
          _buildGroupInputBar(groupId, ready, members, groupAccount),
        ],
      ),
    );

    final memberPanel = Container(
      width: 240,
      decoration: BoxDecoration(
        color: const Color(0xFFF7F7F7),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.15), blurRadius: 12, offset: const Offset(-2, 0))],
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: Row(
              children: [
                Text('群成员', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500, color: Colors.grey[600])),
                const SizedBox(width: 8),
                Text('(${members.length})', style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    groupId,
                    style: TextStyle(fontSize: 11, color: Colors.grey[500]),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close, size: 20),
                  tooltip: '收起',
                  padding: EdgeInsets.zero,
                  onPressed: () => setState(() => _showGroupMembers = false),
                ),
              ],
            ),
          ),
          Expanded(
            child: ListView.builder(
              itemCount: members.length,
              itemBuilder: (context, index) {
                final m = members[index];
                final isOwner = m == owner;
                final isMe = m.toLowerCase() == groupAccount.toLowerCase();
                return ListTile(
                  dense: true,
                  leading: CircleAvatar(
                    radius: 16,
                    backgroundColor: isOwner ? Colors.orange[100] : Colors.blue[100],
                    child: Text(m.isNotEmpty ? m[0].toUpperCase() : '?',
                        style: TextStyle(fontSize: 12, color: isOwner ? Colors.orange[700] : Colors.blue[700])),
                  ),
                  title: Text(m, style: TextStyle(fontSize: 12, color: Colors.grey[800]), overflow: TextOverflow.ellipsis),
                  trailing: isOwner
                      ? Container(
                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                          decoration: BoxDecoration(color: Colors.orange[50], borderRadius: BorderRadius.circular(4)),
                          child: Text('群主', style: TextStyle(fontSize: 10, color: Colors.orange[700])),
                        )
                      : isMe
                          ? Container(
                              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                              decoration: BoxDecoration(color: Colors.green[50], borderRadius: BorderRadius.circular(4)),
                              child: Text('我', style: TextStyle(fontSize: 10, color: Colors.green[700])),
                            )
                          : null,
                );
              },
            ),
          ),
        ],
      ),
    );

    return Stack(
      children: [
        conversation,
        Positioned(
          top: 0,
          right: 0,
          bottom: 0,
          child: AnimatedSlide(
            duration: const Duration(milliseconds: 200),
            curve: Curves.easeOut,
            offset: _showGroupMembers ? Offset.zero : const Offset(1, 0),
            child: memberPanel,
          ),
        ),
      ],
    );
  }

  Widget _buildGroupMessageBubble(native.EmailMessage msg, List<String> members) {
    final isMe = msg.isSent == 1;

    // Resolve display body from the .eml file (handshake -> 🤝, group msg -> decrypted text)
    bool isHandshake = native.XMailer.isKeyExchange(msg.xMailer);
    String displayBody = '';
    if (msg.file.isNotEmpty) {
      final emlPath = '$_emailDataPath/${msg.account}/${msg.file}.eml';
      final parsed = parseEmlFile(emlPath, account: msg.account, sessionId: msg.sessionId, fromAddr: msg.sender, xMailer: msg.xMailer, isSent: msg.isSent);
      isHandshake = isHandshake || parsed.isHandshakeMessage;
      displayBody = parsed.textBody;
    } else if (!isHandshake) {
      displayBody = AppStrings.isZh ? '[下载中...]' : '[Downloading...]';
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        mainAxisAlignment: isMe ? MainAxisAlignment.end : MainAxisAlignment.start,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (!isMe) ...[
            CircleAvatar(
              radius: 18,
              backgroundColor: Colors.blue[100],
              child: Text(msg.sender.isNotEmpty ? msg.sender[0].toUpperCase() : '?',
                  style: TextStyle(color: Colors.blue[700])),
            ),
            const SizedBox(width: 8),
          ],
          Flexible(
            child: Column(
              crossAxisAlignment: isMe ? CrossAxisAlignment.end : CrossAxisAlignment.start,
              children: [
                if (!isMe)
                  Text(msg.sender, style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: BoxDecoration(
                    color: isMe ? const Color(0xFF95EC69) : Colors.white,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.grey[300]!),
                  ),
                  child: isHandshake
                      ? const Text('🤝', style: TextStyle(fontSize: 20))
                      : SelectableText(displayBody, style: const TextStyle(fontSize: 14)),
                ),
                const SizedBox(height: 2),
                Text(msg.timestamp, style: TextStyle(fontSize: 11, color: Colors.grey[400])),
              ],
            ),
          ),
          if (isMe) ...[
            const SizedBox(width: 8),
            CircleAvatar(
              radius: 18,
              backgroundColor: Colors.green[100],
              child: Text('我', style: TextStyle(color: Colors.green[700], fontSize: 12)),
            ),
          ],
        ],
      ),
    );
  }

  Widget _buildGroupInputBar(String groupId, bool ready, List<String> members, String groupAccount) {
    final myEmail = groupAccount.isNotEmpty ? groupAccount : (native.EmailCore.loadConfig(_configPath)?.accounts.firstWhere((a) => a.email.isNotEmpty).email ?? '');

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: const Color(0xFFF7F7F7),
        border: Border(
          top: BorderSide(color: Colors.grey[300]!, width: 0.5),
        ),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              Expanded(
                child: DropTarget(
                  onDragDone: (detail) {
                    setState(() {
                      for (final file in detail.files) {
                        final path = file.path;
                        final name = path.split('/').last;
                        int size = 0;
                        try {
                          size = File(path).lengthSync();
                        } catch (_) {}
                        _droppedFiles.add(DroppedFile(name: name, path: path, size: size));
                        final cursor = _replyController.selection.baseOffset;
                        final text = _replyController.text;
                        final newText = text.substring(0, cursor.clamp(0, text.length)) +
                            '\uFFFC' +
                            text.substring(cursor.clamp(0, text.length));
                        _replyController.value = TextEditingValue(
                          text: newText,
                          selection: TextSelection.collapsed(offset: cursor + 1),
                        );
                      }
                      _isDragging = false;
                    });
                  },
                  onDragEntered: (detail) {
                    setState(() => _isDragging = true);
                  },
                  onDragExited: (detail) {
                    setState(() => _isDragging = false);
                  },
                  child: Container(
                    constraints: const BoxConstraints(maxHeight: 200),
                    decoration: BoxDecoration(
                      color: Colors.white,
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Container(
                      decoration: BoxDecoration(
                        borderRadius: BorderRadius.circular(4),
                        border: Border.all(
                          color: _isDragging ? const Color(0xFF07C160) : Colors.grey[300]!,
                          width: 1,
                        ),
                      ),
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(4),
                        child: SingleChildScrollView(
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Theme(
                                data: Theme.of(context).copyWith(
                                  hoverColor: Colors.transparent,
                                  highlightColor: Colors.transparent,
                                ),
                                child: TextField(
                                  controller: _replyController,
                                  maxLines: 5,
                                  minLines: 2,
                                  enabled: ready,
                                  onTap: () {
                                    if (_showEmojiPicker) {
                                      setState(() => _showEmojiPicker = false);
                                    }
                                  },
                                  decoration: InputDecoration(
                                    hintText: ready ? AppStrings.sendMessageHint : '等待密钥分发完成...',
                                    hintStyle: TextStyle(fontSize: 14, color: Colors.grey[400]),
                                    border: InputBorder.none,
                                    enabledBorder: InputBorder.none,
                                    focusedBorder: InputBorder.none,
                                    disabledBorder: InputBorder.none,
                                    contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                                    isDense: true,
                                  ),
                                ),
                              ),
                              Padding(
                                padding: const EdgeInsets.only(left: 4, bottom: 4),
                                child: Align(
                                  alignment: Alignment.centerLeft,
                                  child: Row(
                                    mainAxisSize: MainAxisSize.min,
                                    children: [
                                      IconButton(
                                        icon: Icon(Icons.folder_open, size: 20, color: Colors.grey[600]),
                                        onPressed: () {},
                                        constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                                        padding: EdgeInsets.zero,
                                      ),
                                      IconButton(
                                        icon: Icon(
                                          _showEmojiPicker ? Icons.emoji_emotions : Icons.emoji_emotions_outlined,
                                          size: 20,
                                          color: _showEmojiPicker ? const Color(0xFF07C160) : Colors.grey[600],
                                        ),
                                        onPressed: ready ? () {
                                          setState(() {
                                            _showEmojiPicker = !_showEmojiPicker;
                                          });
                                        } : null,
                                        constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                                        padding: EdgeInsets.zero,
                                      ),
                                    ],
                                  ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              TextButton(
                style: TextButton.styleFrom(
                  backgroundColor: const Color(0xFF07C160),
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
                ),
                onPressed: ready ? () {
                  if (_showEmojiPicker) {
                    setState(() => _showEmojiPicker = false);
                  }
                  _sendGroupMessage(groupId, myEmail, members);
                } : null,
                child: Text(AppStrings.send, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
              ),
            ],
          ),
          if (_showEmojiPicker)
            SizedBox(
              height: 250,
              child: EmojiPicker(
                onEmojiSelected: (category, emoji) {
                  if (emoji != null) {
                    final text = _replyController.text;
                    final selection = _replyController.selection;
                    final cursorPos = selection.baseOffset < 0 ? text.length : selection.baseOffset;
                    final newText = text.substring(0, cursorPos) + emoji.emoji + text.substring(cursorPos);
                    _replyController.value = TextEditingValue(
                      text: newText,
                      selection: TextSelection.collapsed(offset: cursorPos + emoji.emoji.length),
                    );
                  }
                },
                config: Config(
                  height: 250,
                  checkPlatformCompatibility: true,
                  emojiViewConfig: EmojiViewConfig(
                    emojiSizeMax: 28,
                    backgroundColor: Colors.white,
                  ),
                  categoryViewConfig: CategoryViewConfig(
                    indicatorColor: const Color(0xFF07C160),
                    iconColorSelected: const Color(0xFF07C160),
                    backgroundColor: Colors.white,
                  ),
                  searchViewConfig: SearchViewConfig(
                    backgroundColor: Colors.white,
                    buttonIconColor: Colors.grey[600] ?? Colors.grey,
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }

  void _sendGroupMessage(String groupId, String myEmail, List<String> members) {
    final text = _replyController.text.trim();
    if (text.isEmpty) return;

    native.EmailCore.logWrite('[GROUP_SEND] groupId=$groupId, text=$text');

    // Reply chain: in_reply_to = local x_message_id of the last message in this group.
    final chain = groupMessages.where((m) => m.messageId.isNotEmpty).toList();
    if (chain.isEmpty) {
      native.EmailCore.logWrite('[GROUP_SEND] no root message in group=$groupId, cannot chain');
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('群组尚未建立消息链，无法发送'), duration: Duration(seconds: 3)),
      );
      return;
    }
    final inReplyTo = chain.last.messageId;

    // MLS: group_send_message encrypts the plaintext and queues the task internally.
    final result = native.EmailCore.groupSendMessage(myEmail, groupId, text, inReplyTo);
    native.EmailCore.logWrite('[GROUP_SEND] groupSendMessage result: $result');

    Map<String, dynamic> resultJson;
    try {
      resultJson = jsonDecode(result);
    } catch (e) {
      native.EmailCore.logWrite('[GROUP_SEND] parse error: $e');
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('发送失败: $e'), duration: const Duration(seconds: 3)),
      );
      return;
    }

    if (resultJson['status'] != 'success') {
      final err = resultJson['error'] ?? 'unknown';
      native.EmailCore.logWrite('[GROUP_SEND] failed: $err');
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('发送失败: $err'), duration: const Duration(seconds: 3)),
      );
      return;
    }

    final rc = resultJson['task_id'] as int? ?? 0;
    if (rc > 0) {
      _replyController.clear();
      setState(() {});
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('消息已发送'), duration: Duration(seconds: 1)),
      );
      loadGroupMessages(groupId);
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('发送失败: rc=$rc'), duration: const Duration(seconds: 3)),
      );
    }
  }
}
