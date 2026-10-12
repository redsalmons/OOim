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
    if (Platform.isMacOS) ...[
      'libemail_core.dylib',
      '${Directory.current.path}/email/build/libemail_core.dylib',
      '${File(Platform.resolvedExecutable).parent.path}/libemail_core.dylib',
      '${File(Platform.resolvedExecutable).parent.parent.path}/Frameworks/libemail_core.dylib',
    ],
    if (Platform.isWindows) ...[
      'email_core.dll',
      '${Directory.current.path}\\email\\build\\email_core.dll',
      '${File(Platform.resolvedExecutable).parent.path}\\email_core.dll',
    ],
    if (Platform.isAndroid) ...[
      'libemail_core.so',
    ],
  ];
  for (final path in candidates) {
    try {
      return DynamicLibrary.open(path);
    } catch (e) {
      // try next
    }
  }
  throw Exception('Failed to load native email_core library for UnifiedSessionManager');
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

// us_send_message / us_add_members / us_list_messages-style (a, b, c, out, size)
typedef _UsThreeArgNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsThreeArgDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

// us_send_files (account, sessionId, filesJson, text, out, size)
typedef _UsSendFilesNative = Int32 Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsSendFilesDart = int Function(
    Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

// us_list_messages (account, sessionId, out, size)
typedef _UsTwoArgNative = Int32 Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Int32);
typedef _UsTwoArgDart = int Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, int);

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
final _usSendMessage = _lib.lookupFunction<_UsThreeArgNative, _UsThreeArgDart>('us_send_message');
final _usSendFiles = _lib.lookupFunction<_UsSendFilesNative, _UsSendFilesDart>('us_send_files');
final _usListMessages = _lib.lookupFunction<_UsTwoArgNative, _UsTwoArgDart>('us_list_messages');
final _usAddMembers = _lib.lookupFunction<_UsThreeArgNative, _UsThreeArgDart>('us_add_members');
final _usListSessions = _lib.lookupFunction<_UsQueryNative, _UsQueryDart>('us_list_sessions');
final _usGetSession = _lib.lookupFunction<_UsQueryNative, _UsQueryDart>('us_get_session');
final _usSetSessionFlags = _lib.lookupFunction<_UsSetSessionFlagsNative, _UsSetSessionFlagsDart>('us_set_session_flags');
final _usIsReady = _lib.lookupFunction<_UsIsReadyNative, _UsIsReadyDart>('us_is_ready');
final _usSetSubject = _lib.lookupFunction<_UsSetSubjectNative, _UsSetSubjectDart>('us_set_subject');
final _usResetPeerIdentity = _lib.lookupFunction<_UsQueryNative, _UsQueryDart>('us_reset_peer_identity');

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
  final int status;     // 0 active, 1 closed, 2 peer identity changed
  final bool pinned;    // 置顶
  final bool hidden;    // 隐藏
  final String createdAt;
  final String updatedAt;
  /// Safety-number material (Signal sessions): SHA-256 hex of own / peer IK.
  final String identityFp;
  final String peerIdentityFp;

  bool get identityChanged => status == 2;

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
    this.identityFp = '',
    this.peerIdentityFp = '',
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
      identityFp: json['identity_fp']?.toString() ?? '',
      peerIdentityFp: json['peer_identity_fp']?.toString() ?? '',
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
      identityFp: identityFp,
      peerIdentityFp: peerIdentityFp,
    );
  }
}

// ---------------------------------------------------------------------------
// Conversation messages (built by the native core; the UI only renders them)
// ---------------------------------------------------------------------------

class SessionFile {
  final String fileId;
  final String fileName;
  final int fileSize;
  final int totalChunks;
  final int receivedChunks;
  final int transferStatus; // 0=pending, 1=complete, 2=failed

  const SessionFile({
    this.fileId = '',
    this.fileName = '',
    this.fileSize = 0,
    this.totalChunks = 0,
    this.receivedChunks = 0,
    this.transferStatus = 0,
  });

  factory SessionFile.fromJson(Map<String, dynamic> j) => SessionFile(
        fileId: j['file_id']?.toString() ?? '',
        fileName: j['file_name']?.toString() ?? '',
        fileSize: (j['file_size'] as num?)?.toInt() ?? 0,
        totalChunks: (j['total_chunks'] as num?)?.toInt() ?? 0,
        receivedChunks: (j['received_chunks'] as num?)?.toInt() ?? 0,
        transferStatus: (j['transfer_status'] as num?)?.toInt() ?? 0,
      );
}

/// text: user message; handshake: key exchange / group control (render as a
/// marker); file: file card with [files] and an optional caption in [text].
enum MessageKind { text, handshake, file }

class SessionMessage {
  final String messageId;
  final String replyTo;
  final String sender;
  final String timestamp;
  final bool isSent;
  final MessageKind kind;
  final String text;
  final String batchId;
  final List<SessionFile> files;

  /// Optimistic "sending…" bubble created by the UI from a send result; it is
  /// dropped once a stored message with the same [messageId] shows up.
  final bool pending;

  const SessionMessage({
    this.messageId = '',
    this.replyTo = '',
    this.sender = '',
    this.timestamp = '',
    this.isSent = false,
    this.kind = MessageKind.text,
    this.text = '',
    this.batchId = '',
    this.files = const [],
    this.pending = false,
  });

  factory SessionMessage.fromJson(Map<String, dynamic> j) => SessionMessage(
        messageId: j['x_message_id']?.toString() ?? '',
        replyTo: j['x_reply_to']?.toString() ?? '',
        sender: j['sender']?.toString() ?? '',
        timestamp: j['timestamp']?.toString() ?? '',
        isSent: (j['is_sent'] as num?)?.toInt() == 1,
        kind: switch (j['kind']) {
          'handshake' => MessageKind.handshake,
          'file' => MessageKind.file,
          _ => MessageKind.text,
        },
        text: j['text']?.toString() ?? '',
        batchId: j['batch_id']?.toString() ?? '',
        files: [
          for (final f in (j['files'] as List? ?? const []))
            SessionFile.fromJson(f as Map<String, dynamic>)
        ],
      );

  /// Optimistic bubble for a successful send result ({message_id, files?, batch_id?}).
  factory SessionMessage.pendingFrom(Map<String, dynamic> sendResult,
      {required String sender, required String text}) {
    final files = [
      for (final f in (sendResult['files'] as List? ?? const []))
        SessionFile.fromJson(f as Map<String, dynamic>)
    ];
    return SessionMessage(
      messageId: sendResult['message_id']?.toString() ?? '',
      sender: sender,
      timestamp: DateTime.now().toString().substring(0, 19),
      isSent: true,
      kind: files.isEmpty ? MessageKind.text : MessageKind.file,
      text: text,
      batchId: sendResult['batch_id']?.toString() ?? '',
      files: files,
      pending: true,
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
  /// The handshake (PREKEY_BUNDLE / MLS invite) is queued by the core.
  ///
  /// Returns a JSON string: {status, session_id, message_id, x_mailer, task_id}
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

  static String _call3(_UsThreeArgDart fn, String a, String b, String c) {
    final pa = a.toNativeUtf8(), pb = b.toNativeUtf8(), pc = c.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      fn(pa, pb, pc, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(pa);
      malloc.free(pb);
      malloc.free(pc);
      malloc.free(outBuf);
    }
  }

  /// Send a message in an existing session. The core routes it to Signal or MLS
  /// and threads it (body x-reply-to) onto the conversation's last message.
  ///
  /// Returns a JSON string: {status, session_id, message_id, x_mailer, task_id}
  static String sendMessage(String account, String sessionId, String plaintext) =>
      _call3(_usSendMessage, account, sessionId, plaintext);

  /// Queue files ([{path, name}]) with an optional caption.
  ///
  /// Returns a JSON string: {status, message_id, files, batch_id, x_mailer}
  static String sendFiles(String account, String sessionId,
      List<({String path, String name})> files, String text) {
    final filesJson = jsonEncode([for (final f in files) {'path': f.path, 'name': f.name}]);
    final pa = account.toNativeUtf8(), ps = sessionId.toNativeUtf8();
    final pf = filesJson.toNativeUtf8(), pt = text.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(65536);
    try {
      _usSendFiles(pa, ps, pf, pt, outBuf, 65536);
      return outBuf.toDartString();
    } finally {
      malloc.free(pa);
      malloc.free(ps);
      malloc.free(pf);
      malloc.free(pt);
      malloc.free(outBuf);
    }
  }

  /// Messages of a session in reply-chain order (membership by body x-reply-to).
  static List<SessionMessage> listMessages(String account, String sessionId) {
    final pa = account.toNativeUtf8(), ps = sessionId.toNativeUtf8();
    var size = 1 << 20;
    try {
      while (true) {
        final outBuf = malloc.allocate<Utf8>(size);
        try {
          final rc = _usListMessages(pa, ps, outBuf, size);
          if (rc < 0 && -rc > size) {
            size = -rc;
            continue;
          }
          final decoded = jsonDecode(outBuf.toDartString());
          if (decoded['status'] != 'success') return const [];
          return [
            for (final m in decoded['messages'] as List)
              SessionMessage.fromJson(m as Map<String, dynamic>)
          ];
        } finally {
          malloc.free(outBuf);
        }
      }
    } finally {
      malloc.free(pa);
      malloc.free(ps);
    }
  }

  /// Add members to an existing session. A Signal session growing past two
  /// members is irreversibly upgraded to MLS.
  ///
  /// Returns a JSON string: {status, session_id, upgraded, mode, task_id}
  static String addMembers(String account, String sessionId, List<String> newMembers) =>
      _call3(_usAddMembers, account, sessionId, jsonEncode(newMembers));

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

  /// After the user verified a peer's new fingerprint: forget the pinned
  /// identity key and reopen a session flagged [UnifiedSessionInfo.identityChanged].
  static bool resetPeerIdentity(String sessionId) {
    final sessionPtr = sessionId.toNativeUtf8();
    final outBuf = malloc.allocate<Utf8>(4096);
    try {
      return _usResetPeerIdentity(sessionPtr, outBuf, 4096) == 0;
    } finally {
      malloc.free(sessionPtr);
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
