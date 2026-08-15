import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';
import 'package:ffi/ffi.dart';
import 'package:url_launcher/url_launcher.dart';

// Callback function type for native notifications
typedef NativeCallback = Void Function(Int32, Int32, Pointer<Utf8>);

// Global callback reference
Pointer<NativeFunction<NativeCallback>>? _nativeCallbackPointer;

void _handleNativeNotification(int configIndex, int messageType, Pointer<Utf8> jsonStr) {
  final json = jsonStr.toDartString();
  print('=== Native notification received ===');
  print('configIndex: $configIndex');
  print('messageType: $messageType');
  print('json: $json');
  print('================================');
  
  // Handle browser launch notification (messageType = 1)
  if (messageType == 1) {
    try {
      final jsonData = jsonDecode(json);
      final url = jsonData['url'] as String?;
      print('Browser launch URL: $url');
      if (url != null) {
        launchUrl(Uri.parse(url), mode: LaunchMode.externalApplication).then((result) {
          print('Browser launch result: $result');
        });
      }
    } catch (e) {
      print('Failed to parse browser launch notification: $e');
    }
  }
  // Handle new email notification (messageType = 3)
  else if (messageType == 3) {
    try {
      final jsonData = jsonDecode(json);
      final account = jsonData['account'] as String?;
      final count = jsonData['count'] as int?;
      final emails = jsonData['emails'] as List?;
      final folder = jsonData['folder'] as String?;
      
      print('New email notification: account=$account, count=$count, folder=$folder');
      
      // Store the notification for the UI to pick up
      // This will be processed by the background service
      if (account != null && count != null && count > 0) {
        _newEmailNotifications.add(jsonData);
        print('Added new email notification to queue: $jsonData');
      }
    } catch (e) {
      print('Failed to parse new email notification: $e');
    }
  }
  // Handle email sent notification (messageType = 5)
  else if (messageType == 5) {
    try {
      final jsonData = jsonDecode(json);
      final sessionId = jsonData['session_id'] as String?;
      final emailId = jsonData['email_id'] as String?;
      final messageId = jsonData['message_id'] as String?;
      
      print('Email sent notification: session_id=$sessionId, email_id=$emailId, message_id=$messageId');
      
      // Store the notification for the UI to pick up
      if (sessionId != null) {
        _emailSentNotifications.add(jsonData);
        print('Added email sent notification to queue: $jsonData');
      }
    } catch (e) {
      print('Failed to parse email sent notification: $e');
    }
  }
}

// Queue for new email notifications
final List<Map<String, dynamic>> _newEmailNotifications = <Map<String, dynamic>>[];

// Queue for email sent notifications
final List<Map<String, dynamic>> _emailSentNotifications = <Map<String, dynamic>>[];

// Get pending new email notifications
List<Map<String, dynamic>> getNewEmailNotifications() {
  final notifications = List<Map<String, dynamic>>.from(_newEmailNotifications);
  _newEmailNotifications.clear();
  return notifications;
}

// Get pending email sent notifications
List<Map<String, dynamic>> getEmailSentNotifications() {
  final notifications = List<Map<String, dynamic>>.from(_emailSentNotifications);
  _emailSentNotifications.clear();
  return notifications;
}

// ---------------------------------------------------------------------------
// Native struct definitions (must mirror email/email_core.h exactly)
// ---------------------------------------------------------------------------

final class _NativeEmail extends Struct {
  external Pointer<Utf8> sender;
  external Pointer<Utf8> recipient;
  external Pointer<Utf8> subject;
  external Pointer<Utf8> body;
  external Pointer<Utf8> timestamp;
}

final class _NativeEmailHandler extends Opaque {}

final class _NativeEmailAccountConfig extends Struct {
  external Pointer<Utf8> type;
  external Pointer<Utf8> email;
  external Pointer<Utf8> authCode;
  external Pointer<Utf8> smtpServer;
  @Int32()
  external int smtpPort;
  external Pointer<Utf8> imapServer;
  @Int32()
  external int imapPort;
  external Pointer<Utf8> accountType;
  @Int32()
  external int authorized;
  external Pointer<Utf8> id;
  @Int32()
  external int uid;
  external Pointer<Utf8> phrase;
  @Int64()
  external int folderSize;
}

// ---------------------------------------------------------------------------
// Native function signatures
// ---------------------------------------------------------------------------

typedef _HandlerCreateNative = Pointer<_NativeEmailHandler> Function(Int32 capacity);
typedef _HandlerCreateDart = Pointer<_NativeEmailHandler> Function(int capacity);

typedef _HandlerDestroyNative = Void Function(Pointer<_NativeEmailHandler>);
typedef _HandlerDestroyDart = void Function(Pointer<_NativeEmailHandler>);

typedef _EmailAddNative = Int32 Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>,
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmailAddDart = int Function(
    Pointer<_NativeEmailHandler>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _EmailRemoveNative = Int32 Function(Pointer<_NativeEmailHandler>, Int32);
typedef _EmailRemoveDart = int Function(Pointer<_NativeEmailHandler>, int);

typedef _EmailGetNative = Pointer<_NativeEmail> Function(Pointer<_NativeEmailHandler>, Int32);
typedef _EmailGetDart = Pointer<_NativeEmail> Function(Pointer<_NativeEmailHandler>, int);

typedef _EmailCountNative = Int32 Function(Pointer<_NativeEmailHandler>);
typedef _EmailCountDart = int Function(Pointer<_NativeEmailHandler>);

typedef _EmailSendNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmailSendDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _EmailSendWithHeadersNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmailSendWithHeadersDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _EmailSendViaSmtpNative = Int32 Function(
    Pointer<Utf8>, Int32, Pointer<Utf8>, Pointer<Utf8>,
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmailSendViaSmtpDart = int Function(
    Pointer<Utf8>, int, Pointer<Utf8>, Pointer<Utf8>,
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _EmailReceiveNative = Int32 Function(Pointer<_NativeEmailHandler>);
typedef _EmailReceiveDart = int Function(Pointer<_NativeEmailHandler>);

typedef _EmailSearchNative = Int32 Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>,
    Pointer<Pointer<_NativeEmail>>, Pointer<Int32>);
typedef _EmailSearchDart = int Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>,
    Pointer<Pointer<_NativeEmail>>, Pointer<Int32>);

typedef _EmailFreeNative = Void Function(Pointer<_NativeEmail>);
typedef _EmailFreeDart = void Function(Pointer<_NativeEmail>);

typedef _GetVersionNative = Pointer<Utf8> Function();
typedef _GetVersionDart = Pointer<Utf8> Function();

typedef _InitSystemNative = Int32 Function();
typedef _InitSystemDart = int Function();

typedef _EmailCoreInitializeNative = Int32 Function(Pointer<Utf8>);
typedef _EmailCoreInitializeDart = int Function(Pointer<Utf8>);

typedef _EmailLoggerInitNative = Int32 Function(Pointer<Utf8>);
typedef _EmailLoggerInitDart = int Function(Pointer<Utf8>);

typedef _ShutdownSystemNative = Void Function();
typedef _ShutdownSystemDart = void Function();

typedef _ConfigSaveNative = Int32 Function(Pointer<Utf8> path, Pointer<Utf8> localDataPath,
    Pointer<_NativeEmailAccountConfig> accounts, Int32 count);
typedef _ConfigSaveDart = int Function(Pointer<Utf8> path, Pointer<Utf8> localDataPath,
    Pointer<_NativeEmailAccountConfig> accounts, int count);

typedef _ConfigLoadNative = Int32 Function(
    Pointer<Utf8> path,
    Pointer<Pointer<Utf8>> localDataPath,
    Pointer<Pointer<_NativeEmailAccountConfig>> accounts,
    Pointer<Int32> count);
typedef _ConfigLoadDart = int Function(
    Pointer<Utf8> path,
    Pointer<Pointer<Utf8>> localDataPath,
    Pointer<Pointer<_NativeEmailAccountConfig>> accounts,
    Pointer<Int32> count);

typedef _ConfigExistsNative = Int32 Function(Pointer<Utf8> path);
typedef _ConfigExistsDart = int Function(Pointer<Utf8> path);

typedef _ConfigFreeNative = Void Function(Pointer<_NativeEmailAccountConfig>, Int32);
typedef _ConfigFreeDart = void Function(Pointer<_NativeEmailAccountConfig>, int);

typedef _EmailSaveNative = Int32 Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>);
typedef _EmailSaveDart = int Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>);

typedef _EmailLoadNative = Int32 Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>);
typedef _EmailLoadDart = int Function(Pointer<_NativeEmailHandler>, Pointer<Utf8>);

typedef _EmailDbInitNative = Int32 Function(Pointer<Utf8>);
typedef _EmailDbInitDart = int Function(Pointer<Utf8>);

typedef _EmailDbCloseNative = Void Function();
typedef _EmailDbCloseDart = void Function();

typedef _ImapCreateNative = Pointer<Void> Function(Pointer<Utf8>, Int32, Pointer<Utf8>, Pointer<Utf8>);
typedef _ImapCreateDart = Pointer<Void> Function(Pointer<Utf8>, int, Pointer<Utf8>, Pointer<Utf8>);

typedef _ImapDestroyNative = Void Function(Pointer<Void>);
typedef _ImapDestroyDart = void Function(Pointer<Void>);

typedef _ImapFetchInboxNative = Int32 Function(Pointer<Void>);
typedef _ImapFetchInboxDart = int Function(Pointer<Void>);

typedef _EmailSetCredentialsNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>);
typedef _EmailSetCredentialsDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>);

typedef _EmailConnectNative = Int32 Function(Int32);
typedef _EmailConnectDart = int Function(int);

typedef _EmailListNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailListDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailGetContentNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailGetContentDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailFetchAndStoreNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailFetchAndStoreDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailQueryLocalemailNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailQueryLocalemailDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailQueryThreadRootsNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailQueryThreadRootsDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);
typedef _EmailQueryThreadNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailQueryThreadDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);
typedef _EmailGenerateSessionsNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailGenerateSessionsDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailCreateSessionNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32, Pointer<Utf8>, Int32);
typedef _EmailCreateSessionDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Utf8>, int);

typedef _EmailCodeQueryByAccountNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailCodeQueryByAccountDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailQuerySessionIndexUuidNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailQuerySessionIndexUuidDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailAddEmailToSessionNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32, Pointer<Utf8>, Int32);
typedef _EmailAddEmailToSessionDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Utf8>, int);

typedef _EmailInsertSentEmailNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailInsertSentEmailDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailGetMaxUidNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailGetMaxUidDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _OemailimSystemOpenNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer);
typedef _OemailimSystemOpenDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer);

typedef _OemailimSetCallbackNative = Void Function(Pointer);
typedef _OemailimSetCallbackDart = void Function(Pointer);

typedef _OemailimOpenNewEmailNative = Int32 Function(Pointer<Utf8>);
typedef _OemailimOpenNewEmailDart = int Function(Pointer<Utf8>);

typedef _OemailimGoNative = Int32 Function(Int32);
typedef _OemailimGoDart = int Function(int);

typedef _OemailimAuthorityNative = Int32 Function(Int32);
typedef _OemailimAuthorityDart = int Function(int);

typedef _OemailimAddOutlookEmailNative = Int32 Function();
typedef _OemailimAddOutlookEmailDart = int Function();

typedef _OemailimGetEmailNative = Int32 Function(Int32, Pointer<Uint8>, Int32);
typedef _OemailimGetEmailDart = int Function(int, Pointer<Uint8>, int);

typedef _OemailimGetRefreshTokenNative = Int32 Function(Int32, Pointer<Uint8>, Int32);
typedef _OemailimGetRefreshTokenDart = int Function(int, Pointer<Uint8>, int);

typedef _OemailimSetImapServerNative = Int32 Function(Int32, Pointer<Utf8>, Int32);
typedef _OemailimSetImapServerDart = int Function(int, Pointer<Utf8>, int);

typedef _OemailimSetSmtpServerNative = Int32 Function(Int32, Pointer<Utf8>, Int32);
typedef _OemailimSetSmtpServerDart = int Function(int, Pointer<Utf8>, int);

typedef _OemailimSetRefreshTokenNative = Int32 Function(Int32, Pointer<Utf8>);
typedef _OemailimSetRefreshTokenDart = int Function(int, Pointer<Utf8>);

typedef _OemailimRefreshTokenNative = Int32 Function(Int32);
typedef _OemailimRefreshTokenDart = int Function(int);

typedef _OemailimSystemCloseNative = Void Function(Int32);
typedef _OemailimSystemCloseDart = void Function(int);

typedef _OemailimEmailListNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _OemailimEmailListDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _OemailimEmailSelectNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _OemailimEmailSelectDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailIdleWaitNative = Int32 Function(Int32, Pointer<Utf8>, Int32);
typedef _EmailIdleWaitDart = int Function(int, Pointer<Utf8>, int);

typedef _EmailFindSentFolderNative = Int32 Function(Int32, Pointer<Utf8>, Int32);
typedef _EmailFindSentFolderDart = int Function(int, Pointer<Utf8>, int);

typedef _EmailSendViaConfigNative = Int32 Function(Int32, Pointer<Utf8>);
typedef _EmailSendViaConfigDart = int Function(int, Pointer<Utf8>);

typedef _EmailLastErrorNative = Int32 Function(Int32, Pointer<Utf8>, Int32);
typedef _EmailLastErrorDart = int Function(int, Pointer<Utf8>, int);

typedef _EmailDownloadPendingNative = Int32 Function(Int32, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailDownloadPendingDart = int Function(int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailCountPendingNative = Int32 Function(Pointer<Utf8>);
typedef _EmailCountPendingDart = int Function(Pointer<Utf8>);

typedef _EmailParseEmlNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailParseEmlDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailDecryptDataBodyNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _EmailDecryptDataBodyDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

typedef _EmailUpdateSessionReadNative = Int32 Function(Pointer<Utf8>);
typedef _EmailUpdateSessionReadDart = int Function(Pointer<Utf8>);

typedef _EmailQuerySessionUnreadNative = Int32 Function(Pointer<Utf8>);
typedef _EmailQuerySessionUnreadDart = int Function(Pointer<Utf8>);

typedef _EmailHideSessionNative = Int32 Function(Pointer<Utf8>);
typedef _EmailHideSessionDart = int Function(Pointer<Utf8>);

typedef _ContactAddNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _ContactAddDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);

typedef _ContactQueryAllNative = Pointer<Utf8> Function();
typedef _ContactQueryAllDart = Pointer<Utf8> Function();

typedef _ContactDeleteNative = Int32 Function(Int32);
typedef _ContactDeleteDart = int Function(int);

typedef _EmailLogWriteNative = Void Function(Pointer<Utf8>);
typedef _EmailLogWriteDart = void Function(Pointer<Utf8>);

// ---------------------------------------------------------------------------
// Library loading
// ---------------------------------------------------------------------------

DynamicLibrary _loadLibrary() {
  final candidates = <String>[
    'libemail_core.dylib',
    '${Directory.current.path}/email/build/libemail_core.dylib',
    '${File(Platform.resolvedExecutable).parent.path}/libemail_core.dylib',
    '${File(Platform.resolvedExecutable).parent.parent.path}/Frameworks/libemail_core.dylib',
  ];
  Object? lastError;
  for (final path in candidates) {
    try {
      return DynamicLibrary.open(path);
    } catch (e) {
      lastError = e;
    }
  }
  throw Exception('Failed to load libemail_core.dylib. Last error: $lastError');
}

final DynamicLibrary _lib = _loadLibrary();

final _handlerCreate = _lib.lookupFunction<_HandlerCreateNative, _HandlerCreateDart>('email_handler_create');
final _handlerDestroy = _lib.lookupFunction<_HandlerDestroyNative, _HandlerDestroyDart>('email_handler_destroy');
final _emailAdd = _lib.lookupFunction<_EmailAddNative, _EmailAddDart>('email_add');
final _emailRemove = _lib.lookupFunction<_EmailRemoveNative, _EmailRemoveDart>('email_remove');
final _emailGet = _lib.lookupFunction<_EmailGetNative, _EmailGetDart>('email_get');
final _emailCount = _lib.lookupFunction<_EmailCountNative, _EmailCountDart>('email_count');
final _emailSend = _lib.lookupFunction<_EmailSendNative, _EmailSendDart>('email_send');
final _emailSendWithHeaders = _lib.lookupFunction<_EmailSendWithHeadersNative, _EmailSendWithHeadersDart>('email_send_with_headers');
final _emailSendViaSmtp = _lib.lookupFunction<_EmailSendViaSmtpNative, _EmailSendViaSmtpDart>('email_send_via_smtp');
final _emailReceive = _lib.lookupFunction<_EmailReceiveNative, _EmailReceiveDart>('email_receive');
final _emailSearch = _lib.lookupFunction<_EmailSearchNative, _EmailSearchDart>('email_search');
final _emailFree = _lib.lookupFunction<_EmailFreeNative, _EmailFreeDart>('email_free');
final _getVersion = _lib.lookupFunction<_GetVersionNative, _GetVersionDart>('get_version');
final _initSystem = _lib.lookupFunction<_InitSystemNative, _InitSystemDart>('initialize_email_system');
final _emailCoreInitialize = _lib.lookupFunction<_EmailCoreInitializeNative, _EmailCoreInitializeDart>('email_core_initialize');
final _emailLoggerInit = _lib.lookupFunction<_EmailLoggerInitNative, _EmailLoggerInitDart>('email_logger_init');
final _shutdownSystem =
    _lib.lookupFunction<_ShutdownSystemNative, _ShutdownSystemDart>('shutdown_email_system');
final _configSave = _lib.lookupFunction<_ConfigSaveNative, _ConfigSaveDart>('email_config_save');
final _configLoad = _lib.lookupFunction<_ConfigLoadNative, _ConfigLoadDart>('email_config_load');
final _configExists = _lib.lookupFunction<_ConfigExistsNative, _ConfigExistsDart>('email_config_exists');
final _configFree = _lib.lookupFunction<_ConfigFreeNative, _ConfigFreeDart>('email_config_free');
final _emailSave = _lib.lookupFunction<_EmailSaveNative, _EmailSaveDart>('email_save');
final _emailLoad = _lib.lookupFunction<_EmailLoadNative, _EmailLoadDart>('email_load');
final _emailDbInit = _lib.lookupFunction<_EmailDbInitNative, _EmailDbInitDart>('email_db_init');
final _emailDbClose = _lib.lookupFunction<_EmailDbCloseNative, _EmailDbCloseDart>('email_db_close');
final _imapCreate = _lib.lookupFunction<_ImapCreateNative, _ImapCreateDart>('imap_create');
final _imapDestroy = _lib.lookupFunction<_ImapDestroyNative, _ImapDestroyDart>('imap_destroy');
final _imapFetchInbox = _lib.lookupFunction<_ImapFetchInboxNative, _ImapFetchInboxDart>('imap_fetch_inbox');
final _oemailimSystemOpen = _lib.lookupFunction<_OemailimSystemOpenNative, _OemailimSystemOpenDart>('oemailim_system_open');
final _oemailimSetCallback = _lib.lookupFunction<_OemailimSetCallbackNative, _OemailimSetCallbackDart>('oemailim_set_callback');
final _oemailimOpenNewEmail = _lib.lookupFunction<_OemailimOpenNewEmailNative, _OemailimOpenNewEmailDart>('oemailim_open_new_email');
final _oemailimGo = _lib.lookupFunction<_OemailimGoNative, _OemailimGoDart>('oemailim_go');
final _oemailimAuthority = _lib.lookupFunction<_OemailimAuthorityNative, _OemailimAuthorityDart>('oemailim_authority');
final _oemailimAddOutlookEmail = _lib.lookupFunction<_OemailimAddOutlookEmailNative, _OemailimAddOutlookEmailDart>('oemailim_add_outlook_email');
final _oemailimGetEmail = _lib.lookupFunction<_OemailimGetEmailNative, _OemailimGetEmailDart>('oemailim_get_email');
final _oemailimGetRefreshToken = _lib.lookupFunction<_OemailimGetRefreshTokenNative, _OemailimGetRefreshTokenDart>('oemailim_get_refresh_token');
final _oemailimSetImapServer = _lib.lookupFunction<_OemailimSetImapServerNative, _OemailimSetImapServerDart>('oemailim_set_imap_server');
final _oemailimSetSmtpServer = _lib.lookupFunction<_OemailimSetSmtpServerNative, _OemailimSetSmtpServerDart>('oemailim_set_smtp_server');
final _oemailimSetRefreshToken = _lib.lookupFunction<_OemailimSetRefreshTokenNative, _OemailimSetRefreshTokenDart>('oemailim_set_refresh_token');
final _oemailimRefreshToken = _lib.lookupFunction<_OemailimRefreshTokenNative, _OemailimRefreshTokenDart>('oemailim_refresh_token');
final _oemailimSystemClose = _lib.lookupFunction<_OemailimSystemCloseNative, _OemailimSystemCloseDart>('oemailim_system_close');
final _oemailimEmailList = _lib.lookupFunction<_OemailimEmailListNative, _OemailimEmailListDart>('oemailim_email_list');
final _oemailimEmailSelect = _lib.lookupFunction<_OemailimEmailSelectNative, _OemailimEmailSelectDart>('oemailim_email_select');
final _emailSetCredentials = _lib.lookupFunction<_EmailSetCredentialsNative, _EmailSetCredentialsDart>('email_set_credentials');
final _emailConnect = _lib.lookupFunction<_EmailConnectNative, _EmailConnectDart>('email_connect');
final _emailList = _lib.lookupFunction<_EmailListNative, _EmailListDart>('email_list');
final _emailGetContent = _lib.lookupFunction<_EmailGetContentNative, _EmailGetContentDart>('email_get_content');
final _emailFetchAndStore = _lib.lookupFunction<_EmailFetchAndStoreNative, _EmailFetchAndStoreDart>('email_fetch_and_store');
final _emailQueryLocalemail = _lib.lookupFunction<_EmailQueryLocalemailNative, _EmailQueryLocalemailDart>('email_query_localemail');
final _emailQueryThreadRoots = _lib.lookupFunction<_EmailQueryThreadRootsNative, _EmailQueryThreadRootsDart>('email_query_thread_roots');
final _emailQueryThread = _lib.lookupFunction<_EmailQueryThreadNative, _EmailQueryThreadDart>('email_query_thread');
final _emailGenerateSessions = _lib.lookupFunction<_EmailGenerateSessionsNative, _EmailGenerateSessionsDart>('email_generate_sessions');
final _emailCreateSession = _lib.lookupFunction<_EmailCreateSessionNative, _EmailCreateSessionDart>('email_create_session');
final _emailCodeQueryByAccount = _lib.lookupFunction<_EmailCodeQueryByAccountNative, _EmailCodeQueryByAccountDart>('email_code_query_by_account');
final _emailQuerySessionIndexUuid = _lib.lookupFunction<_EmailQuerySessionIndexUuidNative, _EmailQuerySessionIndexUuidDart>('email_query_session_index_uuid');
final _emailAddEmailToSession = _lib.lookupFunction<_EmailAddEmailToSessionNative, _EmailAddEmailToSessionDart>('email_add_email_to_session');
final _emailInsertSentEmail = _lib.lookupFunction<_EmailInsertSentEmailNative, _EmailInsertSentEmailDart>('email_insert_sent_email');

final _emailGetMaxUid = _lib.lookupFunction<_EmailGetMaxUidNative, _EmailGetMaxUidDart>('email_get_max_uid');
final _emailIdleWait = _lib.lookupFunction<_EmailIdleWaitNative, _EmailIdleWaitDart>('email_idle_wait');
final _emailFindSentFolder = _lib.lookupFunction<_EmailFindSentFolderNative, _EmailFindSentFolderDart>('email_find_sent_folder');
final _emailSendViaConfig = _lib.lookupFunction<_EmailSendViaConfigNative, _EmailSendViaConfigDart>('email_send_via_config');
final _emailLastError = _lib.lookupFunction<_EmailLastErrorNative, _EmailLastErrorDart>('email_get_last_error');
final _emailLogWrite = _lib.lookupFunction<_EmailLogWriteNative, _EmailLogWriteDart>('email_log_write');
final _emailDownloadPending = _lib.lookupFunction<_EmailDownloadPendingNative, _EmailDownloadPendingDart>('email_download_pending_bodies');
final _emailCountPending = _lib.lookupFunction<_EmailCountPendingNative, _EmailCountPendingDart>('email_count_pending_bodies');
final _emailParseEml = _lib.lookupFunction<_EmailParseEmlNative, _EmailParseEmlDart>('email_parse_eml');
final _emailDecryptDataBody = _lib.lookupFunction<_EmailDecryptDataBodyNative, _EmailDecryptDataBodyDart>('email_decrypt_data_body');
final _emailUpdateSessionRead = _lib.lookupFunction<_EmailUpdateSessionReadNative, _EmailUpdateSessionReadDart>('email_update_session_read');
final _emailQuerySessionUnread = _lib.lookupFunction<_EmailQuerySessionUnreadNative, _EmailQuerySessionUnreadDart>('email_query_session_unread');
final _emailHideSession = _lib.lookupFunction<_EmailHideSessionNative, _EmailHideSessionDart>('email_hide_session');
final _contactAdd = _lib.lookupFunction<_ContactAddNative, _ContactAddDart>('contact_add');
final _contactQueryAll = _lib.lookupFunction<_ContactQueryAllNative, _ContactQueryAllDart>('contact_query_all');
final _contactDelete = _lib.lookupFunction<_ContactDeleteNative, _ContactDeleteDart>('contact_delete');

// ---------------------------------------------------------------------------
// Idiomatic Dart data classes
// ---------------------------------------------------------------------------

class EmailAccountData {
  String type;
  String email;
  String authCode;
  String smtpServer;
  int smtpPort;
  String imapServer;
  int imapPort;
  String accountType;
  bool authorized;
  String id;
  int uid;
  String phrase;
  int folderSize;

  EmailAccountData({
    required this.type,
    this.email = '',
    this.authCode = '',
    this.smtpServer = '',
    this.smtpPort = 0,
    this.imapServer = '',
    this.imapPort = 0,
    this.accountType = 'personal',
    this.authorized = false,
    this.id = '',
    this.uid = 0,
    this.phrase = '',
    this.folderSize = 0,
  });
}

class EmailConfigData {
  final String localDataPath;
  final List<EmailAccountData> accounts;

  EmailConfigData(this.localDataPath, this.accounts);
}

class EmailMessage {
  final String sender;
  final String recipient;
  final String subject;
  final String body;
  final String timestamp;
  final String uuid;
  final List<String> flags;
  final bool isAnswered;
  final String inReplyTo;
  final String messageId;
  final String folder;
  final int isLocal;
  final String sessionId;
  final int rowid;
  final String toAddr;
  final String file;
  final String account;

  EmailMessage({
    required this.sender,
    required this.recipient,
    required this.subject,
    required this.body,
    required this.timestamp,
    this.uuid = '',
    this.flags = const [],
    this.isAnswered = false,
    this.inReplyTo = '',
    this.messageId = '',
    this.folder = 'INBOX',
    this.isLocal = 0,
    this.sessionId = '',
    this.rowid = 0,
    this.toAddr = '',
    this.file = '',
    this.account = '',
  });

}

EmailMessage _emailMessageFromNative(_NativeEmail email) {
  return EmailMessage(
    sender: email.sender.toDartString(),
    recipient: email.recipient.toDartString(),
    subject: email.subject.toDartString(),
    body: email.body.toDartString(),
    timestamp: email.timestamp.toDartString(),
  );
}

// ---------------------------------------------------------------------------
// High-level API
// ---------------------------------------------------------------------------

class EmailCore {
  static bool _initialized = false;

  static void initialize() {
    if (_initialized) return;
    _initSystem();
    _initialized = true;
  }

  static bool initializeLibemail(String appSupportDir) {
    final appSupportDirPtr = appSupportDir.toNativeUtf8();
    try {
      final result = _emailCoreInitialize(appSupportDirPtr);
      if (result >= 0) {
        // Initialize database with contact table
        final dbPath = '$appSupportDir/data/emails.db';
        final dbPathPtr = dbPath.toNativeUtf8();
        try {
          _emailDbInit(dbPathPtr);
        } finally {
          malloc.free(dbPathPtr);
        }
      }
      return result >= 0;
    } finally {
      malloc.free(appSupportDirPtr);
    }
  }

  static bool loggerInit(String logDir) {
    final logDirPtr = logDir.toNativeUtf8();
    try {
      final result = _emailLoggerInit(logDirPtr);
      return result >= 0;
    } finally {
      malloc.free(logDirPtr);
    }
  }

  static void shutdown() {
    if (!_initialized) return;
    _shutdownSystem();
    _initialized = false;
  }

  static String get version => _getVersion().toDartString();

  static int oemailimSetImapServer(int configIndex, String server, int port) {
    final serverPtr = server.toNativeUtf8();
    try {
      return _oemailimSetImapServer(configIndex, serverPtr, port);
    } finally {
      malloc.free(serverPtr);
    }
  }

  static int oemailimSetSmtpServer(int configIndex, String server, int port) {
    final serverPtr = server.toNativeUtf8();
    try {
      return _oemailimSetSmtpServer(configIndex, serverPtr, port);
    } finally {
      malloc.free(serverPtr);
    }
  }

  static int setRefreshToken(int configIndex, String token) {
    final tokenPtr = token.toNativeUtf8();
    try {
      return _oemailimSetRefreshToken(configIndex, tokenPtr);
    } finally {
      malloc.free(tokenPtr);
    }
  }

  static int refreshToken(int configIndex) {
    return _oemailimRefreshToken(configIndex);
  }

  static bool configExists(String path) {
    final pathPtr = path.toNativeUtf8();
    try {
      return _configExists(pathPtr) == 1;
    } finally {
      malloc.free(pathPtr);
    }
  }

  static bool saveConfig(String path, String localDataPath, List<EmailAccountData> accounts) {
    final pathPtr = path.toNativeUtf8();
    final localPathPtr = localDataPath.toNativeUtf8();
    final arrayPtr = malloc<_NativeEmailAccountConfig>(accounts.isEmpty ? 1 : accounts.length);

    final allocatedStrings = <Pointer<Utf8>>[];
    try {
      for (int i = 0; i < accounts.length; i++) {
        final acc = accounts[i];
        final typePtr = acc.type.toNativeUtf8();
        final emailPtr = acc.email.toNativeUtf8();
        final authCodePtr = acc.authCode.toNativeUtf8();
        final smtpServerPtr = acc.smtpServer.toNativeUtf8();
        final imapServerPtr = acc.imapServer.toNativeUtf8();
        final accountTypePtr = acc.accountType.toNativeUtf8();
        final idPtr = acc.id.toNativeUtf8();
        final phrasePtr = acc.phrase.toNativeUtf8();
        allocatedStrings.addAll(
            [typePtr, emailPtr, authCodePtr, smtpServerPtr, imapServerPtr, accountTypePtr, idPtr, phrasePtr]);

        final entry = arrayPtr[i];
        entry.type = typePtr;
        entry.email = emailPtr;
        entry.authCode = authCodePtr;
        entry.smtpServer = smtpServerPtr;
        entry.smtpPort = acc.smtpPort;
        entry.imapServer = imapServerPtr;
        entry.imapPort = acc.imapPort;
        entry.accountType = accountTypePtr;
        entry.authorized = acc.authorized ? 1 : 0;
        entry.id = idPtr;
        entry.uid = acc.uid;
        entry.phrase = phrasePtr;
        entry.folderSize = acc.folderSize;
      }

      final result = _configSave(pathPtr, localPathPtr, arrayPtr, accounts.length);
      return result == 0;
    } finally {
      malloc.free(pathPtr);
      malloc.free(localPathPtr);
      malloc.free(arrayPtr);
      for (final ptr in allocatedStrings) {
        malloc.free(ptr);
      }
    }
  }

  static EmailConfigData? loadConfig(String path) {
    final pathPtr = path.toNativeUtf8();
    final localPathOut = malloc<Pointer<Utf8>>();
    final accountsOut = malloc<Pointer<_NativeEmailAccountConfig>>();
    final countOut = malloc<Int32>();

    try {
      final result = _configLoad(pathPtr, localPathOut, accountsOut, countOut);
      if (result != 0) {
        return null;
      }

      final localDataPath = localPathOut.value.toDartString();
      final count = countOut.value;
      final accountsPtr = accountsOut.value;

      final accounts = <EmailAccountData>[];
      for (int i = 0; i < count; i++) {
        final entry = accountsPtr[i];
        accounts.add(EmailAccountData(
          type: entry.type != nullptr ? entry.type.toDartString() : '',
          email: entry.email != nullptr ? entry.email.toDartString() : '',
          authCode: entry.authCode != nullptr ? entry.authCode.toDartString() : '',
          smtpServer: entry.smtpServer != nullptr ? entry.smtpServer.toDartString() : '',
          smtpPort: entry.smtpPort,
          imapServer: entry.imapServer != nullptr ? entry.imapServer.toDartString() : '',
          imapPort: entry.imapPort,
          accountType: entry.accountType != nullptr ? entry.accountType.toDartString() : '',
          authorized: entry.authorized != 0,
          id: entry.id != nullptr ? entry.id.toDartString() : '',
          uid: entry.uid,
          phrase: entry.phrase != nullptr ? entry.phrase.toDartString() : '',
          folderSize: entry.folderSize,
        ));
      }

      _configFree(accountsPtr, count);
      malloc.free(localPathOut.value);

      return EmailConfigData(localDataPath, accounts);
    } finally {
      malloc.free(pathPtr);
      malloc.free(localPathOut);
      malloc.free(accountsOut);
      malloc.free(countOut);
    }
  }

  static bool sendEmail({required String recipient, required String subject, required String body}) {
    final recipientPtr = recipient.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final bodyPtr = body.toNativeUtf8();
    try {
      return _emailSend(recipientPtr, subjectPtr, bodyPtr) == 0;
    } finally {
      malloc.free(recipientPtr);
      malloc.free(subjectPtr);
      malloc.free(bodyPtr);
    }
  }

  static bool sendEmailWithHeaders({
    required String recipient,
    required String subject,
    required String body,
    String inReplyTo = '',
  }) {
    final recipientPtr = recipient.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final bodyPtr = body.toNativeUtf8();
    final inReplyToPtr = inReplyTo.toNativeUtf8();
    try {
      return _emailSendWithHeaders(recipientPtr, subjectPtr, bodyPtr, inReplyToPtr) == 0;
    } finally {
      malloc.free(recipientPtr);
      malloc.free(subjectPtr);
      malloc.free(bodyPtr);
      malloc.free(inReplyToPtr);
    }
  }

  static bool sendViaSmtp({
    required String smtpServer,
    required int smtpPort,
    required String senderEmail,
    required String authCode,
    required String recipient,
    required String subject,
    required String body,
    String inReplyTo = '',
  }) {
    final smtpServerPtr = smtpServer.toNativeUtf8();
    final senderEmailPtr = senderEmail.toNativeUtf8();
    final authCodePtr = authCode.toNativeUtf8();
    final recipientPtr = recipient.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final bodyPtr = body.toNativeUtf8();
    final inReplyToPtr = inReplyTo.toNativeUtf8();
    try {
      return _emailSendViaSmtp(
        smtpServerPtr, smtpPort,
        senderEmailPtr, authCodePtr,
        recipientPtr, subjectPtr, bodyPtr, inReplyToPtr,
      ) == 0;
    } finally {
      malloc.free(smtpServerPtr);
      malloc.free(senderEmailPtr);
      malloc.free(authCodePtr);
      malloc.free(recipientPtr);
      malloc.free(subjectPtr);
      malloc.free(bodyPtr);
      malloc.free(inReplyToPtr);
    }
  }

  static bool initDatabase(String path) {
    final pathPtr = path.toNativeUtf8();
    try {
      return _emailDbInit(pathPtr) == 0;
    } finally {
      malloc.free(pathPtr);
    }
  }

  static void closeDatabase() {
    _emailDbClose();
  }

  static Pointer<Void> createImapConnection(String server, int port, String email, String authCode) {
    final serverPtr = server.toNativeUtf8();
    final emailPtr = email.toNativeUtf8();
    final authCodePtr = authCode.toNativeUtf8();
    try {
      return _imapCreate(serverPtr, port, emailPtr, authCodePtr);
    } finally {
      malloc.free(serverPtr);
      malloc.free(emailPtr);
      malloc.free(authCodePtr);
    }
  }

  static void destroyImapConnection(Pointer<Void> conn) {
    _imapDestroy(conn);
  }

  static bool fetchInbox(Pointer<Void> conn) {
    return _imapFetchInbox(conn) == 0;
  }

  static bool oemailimSystemOpen(String dataDir, String configDir, String logDir) {
    final dataDirPtr = dataDir.toNativeUtf8();
    final configDirPtr = configDir.toNativeUtf8();
    final logDirPtr = logDir.toNativeUtf8();
    
    // Set up native callback before opening system
    if (_nativeCallbackPointer == null) {
      _nativeCallbackPointer = Pointer.fromFunction<NativeCallback>(_handleNativeNotification);
      oemailimSetCallback(_nativeCallbackPointer!);
    }
    
    try {
      return _oemailimSystemOpen(dataDirPtr, configDirPtr, logDirPtr, nullptr) == 1;
    } finally {
      malloc.free(dataDirPtr);
      malloc.free(configDirPtr);
      malloc.free(logDirPtr);
    }
  }

  static void oemailimSetCallback(Pointer callback) {
    _oemailimSetCallback(callback);
  }

  static int oemailimOpenNewEmail(String emailId) {
    print('[Dart] oemailimOpenNewEmail called with emailId: $emailId');
    final emailIdPtr = emailId.toNativeUtf8();
    try {
      final result = _oemailimOpenNewEmail(emailIdPtr);
      print('[Dart] oemailimOpenNewEmail result: $result');
      return result;
    } finally {
      malloc.free(emailIdPtr);
    }
  }

  static bool oemailimGo(int configIndex) {
    return _oemailimGo(configIndex) >= 0;
  }

  static bool oemailimAuthority(int configIndex) {
    return _oemailimAuthority(configIndex) >= 0;
  }

  static int oemailimAddOutlookEmail() {
    return _oemailimAddOutlookEmail();
  }

  static String oemailimGetEmail(int configIndex) {
    final outEmail = malloc.allocate<Uint8>(256);
    try {
      final result = _oemailimGetEmail(configIndex, outEmail, 256);
      if (result == 0) {
        final email = outEmail.cast<Utf8>().toDartString();
        return email;
      }
      return '';
    } finally {
      malloc.free(outEmail);
    }
  }

  static String oemailimGetRefreshToken(int configIndex) {
    final outToken = malloc.allocate<Uint8>(2048);
    try {
      final result = _oemailimGetRefreshToken(configIndex, outToken, 2048);
      if (result == 0) {
        final token = outToken.cast<Utf8>().toDartString();
        return token;
      }
      return '';
    } finally {
      malloc.free(outToken);
    }
  }

  static void oemailimSystemClose(int configIndex) {
    _oemailimSystemClose(configIndex);
  }

  static String oemailimEmailList(int configIndex, String path) {
    final pathPtr = path.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(4096);
    try {
      final result = _oemailimEmailList(configIndex, pathPtr, outJson, 4096);
      if (result == 0) {
        return outJson.toDartString();
      } else {
        return '{"status":"failed", "error":"native_call_failed"}';
      }
    } finally {
      malloc.free(pathPtr);
      malloc.free(outJson);
    }
  }

  static String oemailimEmailSelect(int configIndex, String path) {
    final pathPtr = path.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(4096);
    try {
      final result = _oemailimEmailSelect(configIndex, pathPtr, outJson, 4096);
      if (result == 0) {
        return outJson.toDartString();
      } else {
        return '{"status":"failed", "error":"native_call_failed"}';
      }
    } finally {
      malloc.free(pathPtr);
      malloc.free(outJson);
    }
  }

  // High-level email connection API

  /// Sets email credentials on a config index.
  /// Returns 0 on success, negative on error.
  static int setEmailCredentials(int configIndex, String email, String authCode) {
    final emailPtr = email.toNativeUtf8();
    final authCodePtr = authCode.toNativeUtf8();
    try {
      return _emailSetCredentials(configIndex, emailPtr, authCodePtr);
    } finally {
      malloc.free(emailPtr);
      malloc.free(authCodePtr);
    }
  }

  /// Connects to email server and authenticates.
  /// Returns 0 on success, negative on error.
  static int connectEmail(int configIndex) {
    return _emailConnect(configIndex);
  }

  /// Lists emails in a folder. Returns JSON string with uids.
  static String listEmails(int configIndex, String folder) {
    final folderPtr = folder.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(8192);
    try {
      final result = _emailList(configIndex, folderPtr, outJson, 8192);
      if (result == 0) {
        return outJson.toDartString();
      } else {
        return '{"status":"failed", "error":"native_call_failed", "code":$result}';
      }
    } finally {
      malloc.free(folderPtr);
      malloc.free(outJson);
    }
  }

  /// Gets email content by UID. Returns JSON string with content.
  static String getEmailContent(int configIndex, String folder, String uid) {
    final folderPtr = folder.toNativeUtf8();
    final uidPtr = uid.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(65536);
    try {
      final result = _emailGetContent(configIndex, folderPtr, uidPtr, outJson, 65536);
      if (result == 0) {
        return outJson.toDartString();
      } else {
        return '{"status":"failed", "error":"native_call_failed", "code":$result}';
      }
    } finally {
      malloc.free(folderPtr);
      malloc.free(uidPtr);
      malloc.free(outJson);
    }
  }

  /// Fetches email headers from IMAP and stores them in localemail SQLite table.
  /// Returns JSON string with fetched emails and stored count.
  static String fetchAndStoreEmails(int configIndex, String folder, String account) {
    // Get max UID from localemail table to use as start point
    String startUid = getMaxUid(account, folder);
    if (startUid.isEmpty) {
      startUid = "0"; // Start from beginning if no emails stored
    }

    final folderPtr = folder.toNativeUtf8();
    final startUidPtr = startUid.toNativeUtf8();
    final accountPtr = account.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(65536);
    try {
      final result = _emailFetchAndStore(configIndex, folderPtr, startUidPtr, accountPtr, outJson, 65536);
      // Always return outJson content, even on error (it contains the error details)
      return outJson.toDartString();
    } finally {
      malloc.free(folderPtr);
      malloc.free(startUidPtr);
      malloc.free(accountPtr);
      malloc.free(outJson);
    }
  }

  /// Queries localemail table for stored emails by account.
  /// Returns JSON string with emails array.
  static String queryLocalemail(String account) {
    final accountPtr = account.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(512 * 1024);
    try {
      final result = _emailQueryLocalemail(accountPtr, outJson, 512 * 1024);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(outJson);
    }
  }

  /// Queries thread root emails (first email of each conversation thread) by account.
  /// Returns JSON string with thread root emails array.
  static String queryThreadRoots(String account) {
    final accountPtr = account.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(512 * 1024);
    try {
      final result = _emailQueryThreadRoots(accountPtr, outJson, 512 * 1024);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(outJson);
    }
  }

  static String queryThread(String sessionId) {
    final sessionIdPtr = sessionId.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(512 * 1024);
    try {
      final result = _emailQueryThread(sessionIdPtr, outJson, 512 * 1024);
      return outJson.toDartString();
    } finally {
      malloc.free(sessionIdPtr);
      malloc.free(outJson);
    }
  }

  static String generateSessions(String account) {
    final accountPtr = account.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(64 * 1024);
    try {
      final result = _emailGenerateSessions(accountPtr, outJson, 64 * 1024);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(outJson);
    }
  }

  static String createSession(String account, String subject, String members, String messageId, {int encryptMethod = 0}) {
    final accountPtr = account.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final membersPtr = members.toNativeUtf8();
    final messageIdPtr = messageId.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(4096);
    try {
      final result = _emailCreateSession(accountPtr, subjectPtr, membersPtr, messageIdPtr, encryptMethod, outJson, 4096);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(subjectPtr);
      malloc.free(membersPtr);
      malloc.free(messageIdPtr);
      malloc.free(outJson);
    }
  }

  static String codeQueryByAccount(String account) {
    final accountPtr = account.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(8192);
    try {
      final result = _emailCodeQueryByAccount(accountPtr, outJson, 8192);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(outJson);
    }
  }

  static String querySessionIndexUuid(String sessionId) {
    final sessionIdPtr = sessionId.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(4096);
    try {
      final result = _emailQuerySessionIndexUuid(sessionIdPtr, outJson, 4096);
      return outJson.toDartString();
    } finally {
      malloc.free(sessionIdPtr);
      malloc.free(outJson);
    }
  }

  static String addEmailToSession(String sessionId, String messageId, String account, {int encryptMethod = 0}) {
    final sessionIdPtr = sessionId.toNativeUtf8();
    final messageIdPtr = messageId.toNativeUtf8();
    final accountPtr = account.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(4096);
    try {
      final result = _emailAddEmailToSession(sessionIdPtr, messageIdPtr, accountPtr, encryptMethod, outJson, 4096);
      return outJson.toDartString();
    } finally {
      malloc.free(sessionIdPtr);
      malloc.free(messageIdPtr);
      malloc.free(accountPtr);
      malloc.free(outJson);
    }
  }

  static String insertSentEmail(String account, String sender, String fromAddr, String toAddr, String subject, String date, String messageId, String inReplyTo, String body, String storageDir) {
    final accountPtr = account.toNativeUtf8();
    final senderPtr = sender.toNativeUtf8();
    final fromAddrPtr = fromAddr.toNativeUtf8();
    final toAddrPtr = toAddr.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final datePtr = date.toNativeUtf8();
    final messageIdPtr = messageId.toNativeUtf8();
    final inReplyToPtr = inReplyTo.toNativeUtf8();
    final bodyPtr = body.toNativeUtf8();
    final storageDirPtr = storageDir.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(4096);
    try {
      final result = _emailInsertSentEmail(accountPtr, senderPtr, fromAddrPtr, toAddrPtr, subjectPtr, datePtr, messageIdPtr, inReplyToPtr, bodyPtr, storageDirPtr, outJson, 4096);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(senderPtr);
      malloc.free(fromAddrPtr);
      malloc.free(toAddrPtr);
      malloc.free(subjectPtr);
      malloc.free(datePtr);
      malloc.free(messageIdPtr);
      malloc.free(inReplyToPtr);
      malloc.free(bodyPtr);
      malloc.free(storageDirPtr);
      malloc.free(outJson);
    }
  }

  /// Gets max UID from localemail table for a specific account.
  /// Returns the max UID string (or empty if none).
  static String getMaxUid(String account, String folder) {
    final accountPtr = account.toNativeUtf8();
    final folderPtr = folder.toNativeUtf8();
    final outUid = malloc.allocate<Utf8>(256);
    try {
      final result = _emailGetMaxUid(accountPtr, folderPtr, outUid, 256);
      return outUid.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(folderPtr);
      malloc.free(outUid);
    }
  }

  /// Enters IMAP IDLE on the current folder, blocks until server notification.
  /// Returns 1 if notification received, 0 if timeout, negative on error.
  static int idleWait(int configIndex, String folder, int timeoutSeconds) {
    final folderPtr = folder.toNativeUtf8();
    try {
      return _emailIdleWait(configIndex, folderPtr, timeoutSeconds);
    } finally {
      malloc.free(folderPtr);
    }
  }

  /// Discovers the Sent folder name via IMAP SPECIAL-USE or name matching.
  /// Returns the folder name string (e.g. "Sent", "Sent Items").
  static String findSentFolder(int configIndex) {
    final outFolder = malloc.allocate<Utf8>(256);
    try {
      final result = _emailFindSentFolder(configIndex, outFolder, 256);
      if (result == 0) {
        return outFolder.toDartString();
      } else {
        return 'Sent';
      }
    } finally {
      malloc.free(outFolder);
    }
  }

  /// Gets pending email sent notifications.
  /// Returns a list of notification data maps.
  static List<Map<String, dynamic>> getEmailSentNotifications() {
    final notifications = List<Map<String, dynamic>>.from(_emailSentNotifications);
    _emailSentNotifications.clear();
    return notifications;
  }

  static bool sendViaConfig(int configIndex, String content) {
    return sendViaConfigRaw(configIndex, content) == 0;
  }

  static int sendViaConfigRaw(int configIndex, String content) {
    final contentPtr = content.toNativeUtf8();
    try {
      return _emailSendViaConfig(configIndex, contentPtr);
    } finally {
      malloc.free(contentPtr);
    }
  }

  static String getLastError(int configIndex) {
    final outBuf = malloc.allocate<Utf8>(512);
    try {
      final result = _emailLastError(configIndex, outBuf, 512);
      if (result == 0) {
        return outBuf.toDartString();
      }
      return 'getLastError failed ($result)';
    } finally {
      malloc.free(outBuf);
    }
  }

  static void logWrite(String message) {
    final msgPtr = message.toNativeUtf8();
    try {
      _emailLogWrite(msgPtr);
    } finally {
      malloc.free(msgPtr);
    }
  }

  /// Downloads email bodies for emails with islocal=0.
  /// Fetches full body via IMAP, saves to <storageDir>/<account>/<uuid>.eml, updates islocal=1.
  /// Returns JSON string with status and downloaded count.
  static String downloadPendingBodies(int configIndex, String account, String storageDir) {
    final accountPtr = account.toNativeUtf8();
    final storageDirPtr = storageDir.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(65536);
    try {
      _emailDownloadPending(configIndex, accountPtr, storageDirPtr, outJson, 65536);
      return outJson.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(storageDirPtr);
      malloc.free(outJson);
    }
  }

  /// Counts pending email bodies (islocal=0) without connecting to IMAP.
  /// Returns count, or -1 on error.
  static int countPendingBodies(String account) {
    final accountPtr = account.toNativeUtf8();
    try {
      return _emailCountPending(accountPtr);
    } finally {
      malloc.free(accountPtr);
    }
  }

  /// Parses an .eml file using vmime and returns JSON with text_body, html_body, attachments.
  static String parseEml(String filePath) {
    final filePathPtr = filePath.toNativeUtf8();
    final outJson = malloc.allocate<Utf8>(1048576);
    try {
      _emailParseEml(filePathPtr, outJson, 1048576);
      return outJson.toDartString();
    } finally {
      malloc.free(filePathPtr);
      malloc.free(outJson);
    }
  }

  /// Decrypts an x_start_new=data email body for the given account.
  /// Returns 0 on success, negative on error. Output plaintext in outJson.
  static int decryptDataBody(String encryptedBody, String account, Pointer<Utf8> outJson, int outSize) {
    final bodyPtr = encryptedBody.toNativeUtf8();
    final accountPtr = account.toNativeUtf8();
    try {
      return _emailDecryptDataBody(bodyPtr, accountPtr, outJson, outSize);
    } finally {
      malloc.free(bodyPtr);
      malloc.free(accountPtr);
    }
  }

  /// Updates session isread field to 1 for all emails in a session.
  /// Returns 0 on success, negative on error.
  static int updateSessionRead(String sessionId) {
    final sessionIdPtr = sessionId.toNativeUtf8();
    try {
      return _emailUpdateSessionRead(sessionIdPtr);
    } finally {
      malloc.free(sessionIdPtr);
    }
  }

  /// Queries unread count for a session (count of records with isread=0).
  /// Returns count on success, negative on error.
  static int querySessionUnread(String sessionId) {
    final sessionIdPtr = sessionId.toNativeUtf8();
    try {
      return _emailQuerySessionUnread(sessionIdPtr);
    } finally {
      malloc.free(sessionIdPtr);
    }
  }

  /// Hides a session by setting visible=0 for all rows with the given session_id.
  /// Returns 0 on success, negative on error.
  static int hideSession(String sessionId) {
    final sessionIdPtr = sessionId.toNativeUtf8();
    try {
      return _emailHideSession(sessionIdPtr);
    } finally {
      malloc.free(sessionIdPtr);
    }
  }

  /// Adds a contact to the database. Returns the contact id (>0) on success, negative on error.
  static int contactAdd({required String email, required String name, required String categories, required String notes, String key = ''}) {
    final emailPtr = email.toNativeUtf8();
    final namePtr = name.toNativeUtf8();
    final categoriesPtr = categories.toNativeUtf8();
    final notesPtr = notes.toNativeUtf8();
    final keyPtr = key.toNativeUtf8();
    try {
      return _contactAdd(emailPtr, namePtr, categoriesPtr, notesPtr, keyPtr);
    } finally {
      malloc.free(emailPtr);
      malloc.free(namePtr);
      malloc.free(categoriesPtr);
      malloc.free(notesPtr);
      malloc.free(keyPtr);
    }
  }

  /// Queries all contacts. Returns a JSON string array, or null on error.
  static String? contactQueryAll() {
    final ptr = _contactQueryAll();
    if (ptr == nullptr) return null;
    try {
      return ptr.toDartString();
    } finally {
      malloc.free(ptr);
    }
  }

  /// Deletes a contact by id. Returns 0 on success, negative on error.
  static int contactDelete(int id) {
    return _contactDelete(id);
  }
}

class EmailInbox {
  final Pointer<_NativeEmailHandler> _handle;
  bool _disposed = false;

  EmailInbox._(this._handle);

  factory EmailInbox({int capacity = 100}) {
    return EmailInbox._(_handlerCreate(capacity));
  }

  void dispose() {
    if (_disposed) return;
    _handlerDestroy(_handle);
    _disposed = true;
  }

  int get count => _emailCount(_handle);

  bool add({
    required String sender,
    required String recipient,
    required String subject,
    required String body,
  }) {
    final senderPtr = sender.toNativeUtf8();
    final recipientPtr = recipient.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final bodyPtr = body.toNativeUtf8();
    try {
      return _emailAdd(_handle, senderPtr, recipientPtr, subjectPtr, bodyPtr) == 0;
    } finally {
      malloc.free(senderPtr);
      malloc.free(recipientPtr);
      malloc.free(subjectPtr);
      malloc.free(bodyPtr);
    }
  }

  bool removeAt(int index) => _emailRemove(_handle, index) == 0;

  EmailMessage? getAt(int index) {
    final ptr = _emailGet(_handle, index);
    if (ptr == nullptr) return null;
    return _emailMessageFromNative(ptr.ref);
  }

  List<EmailMessage> getAll() {
    final result = <EmailMessage>[];
    final total = count;
    for (int i = 0; i < total; i++) {
      final email = getAt(i);
      if (email != null) result.add(email);
    }
    return result;
  }

  void receive() => _emailReceive(_handle);

  List<EmailMessage> search(String query) {
    final queryPtr = query.toNativeUtf8();
    final resultsOut = malloc<Pointer<_NativeEmail>>();
    final countOut = malloc<Int32>();
    try {
      final status = _emailSearch(_handle, queryPtr, resultsOut, countOut);
      if (status != 0) return [];

      final count = countOut.value;
      final resultsPtr = resultsOut.value;
      final results = <EmailMessage>[];
      for (int i = 0; i < count; i++) {
        final entry = resultsPtr[i];
        results.add(_emailMessageFromNative(entry));
        _emailFree(resultsPtr + i);
      }
      if (resultsPtr != nullptr) {
        malloc.free(resultsPtr);
      }
      return results;
    } finally {
      malloc.free(queryPtr);
      malloc.free(resultsOut);
      malloc.free(countOut);
    }
  }

  bool save(String path) {
    final pathPtr = path.toNativeUtf8();
    try {
      return _emailSave(_handle, pathPtr) == 0;
    } finally {
      malloc.free(pathPtr);
    }
  }

  bool load(String path) {
    final pathPtr = path.toNativeUtf8();
    try {
      return _emailLoad(_handle, pathPtr) == 0;
    } finally {
      malloc.free(pathPtr);
    }
  }
}
