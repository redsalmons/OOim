import 'dart:io';
import 'dart:convert';
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';
import '../../native/email_core.dart' as native;
import '../../native/email_background_service.dart';
import 'email_utils.dart';
import 'email_module_base.dart';
import 'email_list_view.dart';
import 'email_detail_view.dart';
import 'conversation_view.dart';
import '../../i18n/app_strings.dart';

export 'email_module_base.dart';

class EmailModuleState extends State<EmailModule>
    with EmailListViewMixin, EmailDetailViewMixin, ConversationViewMixin {
  static const String _myAddress = 'me@oim.local';

  int _selectedEmail = 0;
  String? _selectedConversationMessageId;
  bool _isConversationView = false;
  bool _showConversationPanel = false;
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
            await _loadEmailsFromDb();
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
            await _loadEmailsFromDb();
            break;
          case 'email_sent':
            _logToFile('Background: email sent, session_id=${msg.data?['session_id']}, email_id=${msg.data?['email_id']}');
            // Reload emails to show the newly sent email
            await _loadEmailsFromDb();
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
            final flags = e['flags'] is String ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
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
            );
          }).toList();
          
          // Query conversation roots from database
          final threadRootsResult = native.EmailCore.queryThreadRoots(email);
          final threadRootsDecoded = jsonDecode(threadRootsResult);
          final conversationRoots = <native.EmailMessage>[];
          if (threadRootsDecoded['status'] == 'success') {
            final threadRoots = threadRootsDecoded['emails'] as List;
            conversationRoots.addAll(threadRoots.map((e) {
              final flags = e['flags'] is String ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
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
              final flags = e['flags'] is String ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
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
              final flags = e['flags'] is String ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
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
              final flags = e['flags'] is String ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
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
              final flags = e['flags'] is String ? jsonDecode(e['flags']) : (e['flags'] is List ? e['flags'] : []);
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
}
