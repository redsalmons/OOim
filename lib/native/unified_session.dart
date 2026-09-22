import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

// ============================================================================
// Unified Session Manager — Dart FFI bindings
//
// This is the single entry point for the Flutter GUI layer.
// All protocol details (Signal vs MLS) are hidden behind this facade.
//
// Routing rules (handled in C++):
//   - 2 members  → Signal 1:1 (Double Ratchet / X3DH)
//   - 3+ members → MLS 1:n (Ratchet Tree / RFC 9420)
//   - Adding a member to a Signal session with >2 total → irreversible upgrade
// ============================================================================

// ---------------------------------------------------------------------------
// Dynamic library loading (same pattern as email_core.dart)
// ---------------------------------------------------------------------------

DynamicLibrary _loadLibrary() {
  final candidates = <String>[
    'libemail_core.dylib',
    '${Directory.current.path}/email/build/libemail_core.dylib',
    '${File(Platform.resolvedExecutable).parent.path}/libemail_core.dylib',
    '${File(Platform.resolvedExecutable).parent.parent.path}/Frameworks/libemail_core.dylib',
  ];
  for (final path in candidates) {
    try {
      return DynamicLibrary.open(path);
    } catch (e) {
      // try next
    }
  }
  throw Exception('Failed to load libemail_core.dylib for UnifiedSessionManager');
}

final DynamicLibrary _lib = _loadLibrary();

// ---------------------------------------------------------------------------
// FFI typedefs
// ---------------------------------------------------------------------------

// us_create_session
// (account, subject, membersJson, mailmenJson, pinned, hidden, outJson, outSize)
typedef _UsCreateSessionNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32, Int32, Pointer<Utf8>, Int32);
typedef _UsCreateSessionDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int, int, Pointer<Utf8>, int);

// us_send_message
typedef _UsSendMessageNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsSendMessageDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

// us_handle_incoming
typedef _UsHandleIncomingNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>,
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsHandleIncomingDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>,
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

// us_add_members
typedef _UsAddMembersNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsAddMembersDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

// us_list_sessions / us_get_session
typedef _UsQueryNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsQueryDart = int Function(Pointer<Utf8>, Pointer<Utf8>, int);

// us_set_session_flags
// (sessionId, pinned, hidden, outJson, outSize)
typedef _UsSetSessionFlagsNative = Int32 Function(
    Pointer<Utf8>, Int32, Int32, Pointer<Utf8>, Int32);
typedef _UsSetSessionFlagsDart = int Function(
    Pointer<Utf8>, int, int, Pointer<Utf8>, int);

// us_is_ready
typedef _UsIsReadyNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>);
typedef _UsIsReadyDart = int Function(Pointer<Utf8>, Pointer<Utf8>);

// us_set_subject
// (sessionId, subject, outJson, outSize)
typedef _UsSetSubjectNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsSetSubjectDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

// ---------------------------------------------------------------------------
// Function lookups
// ---------------------------------------------------------------------------

final _usCreateSession = _lib.lookupFunction<_UsCreateSessionNative, _UsCreateSessionDart>('us_create_session');
final _usSendMessage = _lib.lookupFunction<_UsSendMessageNative, _UsSendMessageDart>('us_send_message');
final _usHandleIncoming = _lib.lookupFunction<_UsHandleIncomingNative, _UsHandleIncomingDart>('us_handle_incoming');
final _usAddMembers = _lib.lookupFunction<_UsAddMembersNative, _UsAddMembersDart>('us_add_members');
final _usListSessions = _lib.lookupFunction<_UsQueryNative, _UsQueryDart>('us_list_sessions');
final _usGetSession = _lib.lookupFunction<_UsQueryNative, _UsQueryDart>('us_get_session');
final _usSetSessionFlags = _lib.lookupFunction<_UsSetSessionFlagsNative, _UsSetSessionFlagsDart>('us_set_session_flags');
final _usIsReady = _lib.lookupFunction<_UsIsReadyNative, _UsIsReadyDart>('us_is_ready');
final _usSetSubject = _lib.lookupFunction<_UsSetSubjectNative, _UsSetSubjectDart>('us_set_subject');

// ---------------------------------------------------------------------------
// Data class for unified session info (used by GUI layer)
// ---------------------------------------------------------------------------

class UnifiedSessionInfo {
  final String sessionId;
  final String account;
  final String subject;
  final String mode;         // 'signal' | 'mls'
  final List<String> members;
  final String signalSessionId;
  final String mlsGroupId;
  final String rootMessageId;
  final int status;
  final bool pinned;    // 置顶
  final bool hidden;    // 隐藏
  final String createdAt;
  final String updatedAt;

  UnifiedSessionInfo({
    this.sessionId = '',
    this.account = '',
    this.subject = '',
    this.mode = 'signal',
    this.members = const [],
    this.signalSessionId = '',
    this.mlsGroupId = '',
    this.rootMessageId = '',
    this.status = 0,
    this.pinned = false,
    this.hidden = false,
    this.createdAt = '',
    this.updatedAt = '',
  });

  factory UnifiedSessionInfo.fromJson(Map<String, dynamic> json) {
    return UnifiedSessionInfo(
      sessionId: json['session_id']?.toString() ?? '',
      account: json['account']?.toString() ?? '',
      subject: json['subject']?.toString() ?? '',
      mode: json['mode']?.toString() ?? 'signal',
      members: (json['members'] as List?)?.map((e) => e.toString()).toList() ?? [],
      signalSessionId: json['signal_session_id']?.toString() ?? '',
      mlsGroupId: json['mls_group_id']?.toString() ?? '',
      rootMessageId: json['root_message_id']?.toString() ?? '',
      status: json['status'] is int ? json['status'] : (int.tryParse(json['status']?.toString() ?? '0') ?? 0),
      pinned: (json['pinned'] == 1 || json['pinned'] == true),
      hidden: (json['hidden'] == 1 || json['hidden'] == true),
      createdAt: json['created_at']?.toString() ?? '',
      updatedAt: json['updated_at']?.toString() ?? '',
    );
  }

  /// Create a copy with some fields replaced.
  UnifiedSessionInfo copyWith({
    String? mode,
    List<String>? members,
    String? mlsGroupId,
    String? signalSessionId,
  }) {
    return UnifiedSessionInfo(
      sessionId: sessionId,
      account: account,
      subject: subject,
      mode: mode ?? this.mode,
      members: members ?? this.members,
      signalSessionId: signalSessionId ?? this.signalSessionId,
      mlsGroupId: mlsGroupId ?? this.mlsGroupId,
      rootMessageId: rootMessageId,
      status: status,
      pinned: pinned,
      hidden: hidden,
      createdAt: createdAt,
      updatedAt: updatedAt,
    );
  }
}

// ---------------------------------------------------------------------------
// High-level Dart API
// ---------------------------------------------------------------------------

class UnifiedSessionManager {
  /// Create a new encrypted session.
  ///
  /// [members] must include the local account + all remote participants.
  /// [mailmen] is the sending pool (round-robin); empty means only the main account.
  /// - 2 members → Signal 1:1
  /// - 3+ members → MLS 1:n
  ///
  /// Returns a JSON string: {status, session_id, message_id, x_mailer, task_id, encrypted_body}
  static String createSession(String account, String subject, List<String> members,
      {List<String> mailmen = const [], bool pinned = false, bool hidden = false}) {
    final accountPtr = account.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final membersJson = jsonEncode(members);
    final membersPtr = membersJson.toNativeUtf8();
    final mailmenJson = jsonEncode(mailmen);
    final mailmenPtr = mailmenJson.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usCreateSession(accountPtr, subjectPtr, membersPtr, mailmenPtr,
          pinned ? 1 : 0, hidden ? 1 : 0, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(subjectPtr);
      malloc.free(membersPtr);
      malloc.free(mailmenPtr);
      malloc.free(outBuf);
    }
  }

  /// Send a message in an existing session.
  ///
  /// Auto-routes to Signal or MLS encryption based on the session's mode.
  /// [inReplyTo] is the x_message_id of the previous message (for threading).
  ///
  /// Returns a JSON string: {status, session_id, message_id, x_mailer, task_id, encrypted_body}
  static String sendMessage(String account, String sessionId,
      String plaintext, String inReplyTo) {
    final accountPtr = account.toNativeUtf8();
    final sessionPtr = sessionId.toNativeUtf8();
    final textPtr = plaintext.toNativeUtf8();
    final irtPtr = inReplyTo.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usSendMessage(accountPtr, sessionPtr, textPtr, irtPtr, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(sessionPtr);
      malloc.free(textPtr);
      malloc.free(irtPtr);
      malloc.free(outBuf);
    }
  }

  /// Handle an incoming encrypted message.
  ///
  /// Auto-detects protocol from [xMailer] and routes to the correct decryptor.
  ///
  /// Returns a JSON string: {status, session_id, plaintext, sender, x_mailer}
  static String handleIncoming(String account, String from, String xMailer,
      String body, String messageId, String inReplyTo) {
    final accountPtr = account.toNativeUtf8();
    final fromPtr = from.toNativeUtf8();
    final xMailerPtr = xMailer.toNativeUtf8();
    final bodyPtr = body.toNativeUtf8();
    final midPtr = messageId.toNativeUtf8();
    final irtPtr = inReplyTo.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usHandleIncoming(accountPtr, fromPtr, xMailerPtr, bodyPtr,
          midPtr, irtPtr, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(fromPtr);
      malloc.free(xMailerPtr);
      malloc.free(bodyPtr);
      malloc.free(midPtr);
      malloc.free(irtPtr);
      malloc.free(outBuf);
    }
  }

  /// Add members to an existing session.
  ///
  /// If the session is Signal and total members > 2, triggers an irreversible
  /// upgrade to MLS.
  ///
  /// Returns a JSON string: {status, session_id, upgraded, mode, task_id}
  static String addMembers(String account, String sessionId, List<String> newMembers,
      {String inReplyTo = ''}) {
    final accountPtr = account.toNativeUtf8();
    final sessionPtr = sessionId.toNativeUtf8();
    final membersJson = jsonEncode(newMembers);
    final membersPtr = membersJson.toNativeUtf8();
    final inReplyToPtr = inReplyTo.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usAddMembers(accountPtr, sessionPtr, membersPtr, inReplyToPtr, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(sessionPtr);
      malloc.free(membersPtr);
      malloc.free(inReplyToPtr);
      malloc.free(outBuf);
    }
  }

  /// List all active sessions for an account.
  ///
  /// Returns a JSON string: {status, sessions: [{session_id, mode, members, ...}]}
  static String listSessions(String account) {
    final accountPtr = account.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usListSessions(accountPtr, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(accountPtr);
      malloc.free(outBuf);
    }
  }

  /// Convenience: list sessions and return parsed objects.
  static List<UnifiedSessionInfo> listSessionsAsObjects(String account) {
    final result = <UnifiedSessionInfo>[];
    try {
      final jsonStr = listSessions(account);
      final decoded = jsonDecode(jsonStr);
      if (decoded['status'] == 'success' && decoded['sessions'] is List) {
        for (final s in decoded['sessions']) {
          result.add(UnifiedSessionInfo.fromJson(s as Map<String, dynamic>));
        }
      }
    } catch (_) {}
    return result;
  }

  /// Get a specific session by its unified session_id.
  ///
  /// Returns a JSON string: {status, session: {session_id, mode, members, ...}}
  static String getSession(String sessionId) {
    final sessionPtr = sessionId.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usGetSession(sessionPtr, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(sessionPtr);
      malloc.free(outBuf);
    }
  }

  /// Update the pinned/hidden display flags of a session.
  ///
  /// Returns a JSON string: {status}
  static String setSessionFlags(String sessionId, bool pinned, bool hidden) {
    final sessionPtr = sessionId.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usSetSessionFlags(sessionPtr, pinned ? 1 : 0, hidden ? 1 : 0, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(sessionPtr);
      malloc.free(outBuf);
    }
  }

  /// Update the subject (title) of a session.
  ///
  /// Returns a JSON string: {status}
  static String setSessionSubject(String sessionId, String subject) {
    final sessionPtr = sessionId.toNativeUtf8();
    final subjectPtr = subject.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usSetSubject(sessionPtr, subjectPtr, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(sessionPtr);
      malloc.free(subjectPtr);
      malloc.free(outBuf);
    }
  }

  /// Check if a session is ready for sending.
  ///
  /// Returns true if the session has completed key exchange / MLS join.
  static bool isReady(String account, String sessionId) {
    final accountPtr = account.toNativeUtf8();
    final sessionPtr = sessionId.toNativeUtf8();
    try {
      final rc = _usIsReady(accountPtr, sessionPtr);
      return rc == 1;
    } finally {
      malloc.free(accountPtr);
      malloc.free(sessionPtr);
    }
  }
}
