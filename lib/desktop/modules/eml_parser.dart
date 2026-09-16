import 'dart:convert';
import 'dart:ffi';
import 'package:ffi/ffi.dart';
import '../../native/email_core.dart' as native;

class EmlParsedContent {
  final String textBody;
  final String htmlBody;
  final List<EmlAttachment> attachments;
  final bool hasAttachments;
  final bool isFileMessage;
  final String fileName;
  final int fileSize;
  final String fileId;
  final String batchId;
  final int totalChunks;
  final int receivedChunks;
  final int transferStatus; // 0=pending, 1=complete, 2=failed
  final bool isHandshakeMessage; // true for PREKEY_BUNDLE (1.0.0) and SESSION_INIT (1.0.1)

  EmlParsedContent({
    this.textBody = '',
    this.htmlBody = '',
    this.attachments = const [],
    this.hasAttachments = false,
    this.isFileMessage = false,
    this.fileName = '',
    this.fileSize = 0,
    this.fileId = '',
    this.batchId = '',
    this.totalChunks = 0,
    this.receivedChunks = 0,
    this.transferStatus = 0,
    this.isHandshakeMessage = false,
  });
}

class EmlAttachment {
  final String filename;
  final String contentType;
  final int size;

  EmlAttachment({required this.filename, this.contentType = '', this.size = 0});
}

final Map<String, EmlParsedContent> _emlCache = {};

void clearEmlCache() {
  _emlCache.clear();
}

EmlParsedContent parseEmlFile(String filePath, {String? account, String? sessionId, String? fromAddr, String? xMailer, int isSent = 0}) {
  // Clear cache if it grows too large
  if (_emlCache.length > 1000) {
    _emlCache.clear();
  }
  final cacheKey = [filePath, account ?? '', sessionId ?? '', fromAddr ?? '', xMailer ?? '', isSent].join('|');
  if (_emlCache.containsKey(cacheKey)) {
    return _emlCache[cacheKey]!;
  }
  native.EmailCore.logWrite('[EML] cache miss for $cacheKey, parsing...');

  try {
    final jsonStr = native.EmailCore.parseEml(filePath);
    final decoded = jsonDecode(jsonStr);

    if (decoded['status'] != 'success') {
      final result = EmlParsedContent();
      _emlCache[cacheKey] = result;
      return result;
    }

    var textBody = decoded['text_body'] as String? ?? '';
    final htmlBody = decoded['html_body'] as String? ?? '';
    final hasAtt = decoded['has_attachments'] as bool? ?? false;
    final attList = decoded['attachments'] as List? ?? [];
    final fileXMailer = (decoded['x_mailer'] as String? ?? '').trim();
    final effectiveXMailer = (xMailer ?? fileXMailer).trim();

    // Check if body is encrypted data (JSON with "text" and "session_info")
    if (textBody.isNotEmpty && textBody.contains('"text"') && textBody.contains('"session_info"')) {
      try {
        final bodyJson = jsonDecode(textBody);
        if (bodyJson is Map && bodyJson.containsKey('text') && bodyJson.containsKey('session_info')) {
          final sessionInfo = bodyJson['session_info'] as Map? ?? {};
          // Only "data" type has "code" field in session_info → needs decryption
          // "new" and "exchange" types have other fields → just show the text field
          if (sessionInfo.containsKey('code')) {
            // This is an encrypted data body - try to decrypt
            if (account != null && account.isNotEmpty) {
              final outBuf = malloc.allocate<Utf8>(65536);
              try {
                final rc = native.EmailCore.decryptDataBody(textBody, account, outBuf, 65536);
                native.EmailCore.logWrite('[EML] decryptDataBody rc=$rc, account=$account');
                if (rc == 0) {
                  final decryptedText = outBuf.toDartString();
                  native.EmailCore.logWrite('[EML] decrypt success');
                  // Check if decrypted content is a file/truck JSON message
                  try {
                    final decryptedJson = jsonDecode(decryptedText);
                    if (decryptedJson is Map && decryptedJson.containsKey('msg_type')) {
                      final msgType = decryptedJson['msg_type'] as String? ?? '';
                      if (msgType == 'file') {
                        textBody = decryptedJson['text'] as String? ?? '';
                        final fId = decryptedJson['file_id'] as String? ?? '';
                        final fName = decryptedJson['file_name'] as String? ?? '';
                        final fSize = decryptedJson['file_size'] as int? ?? 0;
                        final fChunks = decryptedJson['total_chunks'] as int? ?? 0;
                        final fBatchId = decryptedJson['batch_id'] as String? ?? '';
                        // Query transfer status from native
                        int receivedChunks = 0;
                        int transferStatus = 0;
                        if (fId.isNotEmpty) {
                          try {
                            final statusJson = native.EmailCore.fileTransferQuery(fId);
                            final statusDecoded = jsonDecode(statusJson);
                            if (statusDecoded['status'] == 'success') {
                              receivedChunks = statusDecoded['received_chunks'] as int? ?? 0;
                              transferStatus = statusDecoded['transfer_status'] as int? ?? 0;
                            }
                          } catch (_) {}
                        }
                        final result = EmlParsedContent(
                          textBody: textBody.isNotEmpty ? textBody : '',
                          htmlBody: '',
                          attachments: [],
                          hasAttachments: false,
                          isFileMessage: true,
                          fileName: fName,
                          fileSize: fSize,
                          fileId: fId,
                          batchId: fBatchId,
                          totalChunks: fChunks,
                          receivedChunks: receivedChunks,
                          transferStatus: transferStatus,
                        );
                        _emlCache[cacheKey] = result;
                        return result;
                      } else if (msgType == 'truck') {
                        textBody = '[File chunk data]';
                      } else {
                        textBody = decryptedJson['text'] as String? ?? decryptedText;
                      }
                    } else if (decryptedJson is Map && decryptedJson.containsKey('text')) {
                      textBody = decryptedJson['text'] as String? ?? decryptedText;
                    } else {
                      textBody = decryptedText;
                    }
                  } catch (_) {
                    // Not JSON, use as-is
                    textBody = decryptedText;
                  }
                } else {
                  native.EmailCore.logWrite('[EML] decrypt failed, rc=$rc');
                  textBody = '[Decryption failed]';
                }
              } finally {
                malloc.free(outBuf);
              }
            } else {
              native.EmailCore.logWrite('[EML] decrypt skipped: account is null or empty');
              textBody = '[Decryption failed]';
            }
          } else {
            // exchange or new type - just extract the text field
            textBody = bodyJson['text'] as String? ?? '';
            native.EmailCore.logWrite('[EML] using text field directly (exchange/new type)');
          }
        }
      } catch (_) {
        // Not valid JSON or decryption failed, keep original
      }
    }

    // Group application message (2.0.3): C++ side rewrites received .eml bodies
    // with the decrypted plaintext; sent copies keep the {plaintext, ...} JSON.
    if (effectiveXMailer == native.XMailer.mlsAppMsg &&
        sessionId != null && sessionId.startsWith('group_') &&
        textBody.isNotEmpty) {
      try {
        final bodyJson = jsonDecode(textBody);
        // Received copies are already rewritten to plaintext by the C++ side
        // (download_pending). Sent copies keep the {plaintext, ...} task JSON.
        // A "ciphertext" field means the C++ side hasn't processed it yet.
        if (bodyJson is Map) {
          if (bodyJson.containsKey('plaintext')) {
            textBody = bodyJson['plaintext'] as String? ?? '';
          } else if (bodyJson.containsKey('ciphertext')) {
            textBody = '[MLS message pending decryption]';
          }
        }
      } catch (e) {
        // If body is not JSON, keep as-is (already decrypted plaintext).
        native.EmailCore.logWrite('[EML] group msg parse error: $e');
      }
    }

    // Check if this is a handshake/control message.
    // MLS messages (2.0.x) and Signal prekey/session-init (1.0.0/1.0.1) are
    // handshake messages — show the handshake emoji, not raw JSON.
    bool isHandshakeMessage = false;
    if (effectiveXMailer == '1.0.0' ||
        effectiveXMailer == '1.0.1' ||
        effectiveXMailer == native.XMailer.mlsKeyPackage ||
        effectiveXMailer == native.XMailer.mlsWelcome ||
        effectiveXMailer == native.XMailer.mlsCommit) {
      isHandshakeMessage = true;
      textBody = '';
    } else if (textBody.toLowerCase().contains('mls_invite') ||
               textBody.toLowerCase().contains('mls_key_package') ||
               textBody.toLowerCase().contains('mls_welcome') ||
               textBody.toLowerCase().contains('mls_commit') ||
               textBody.contains('[MLS handshake:')) {
      isHandshakeMessage = true;
      textBody = '';
    } else if (textBody.isNotEmpty &&
        textBody.contains('"version"') &&
        textBody.contains('"signal_header"')) {
      try {
        final bodyJson = jsonDecode(textBody);
        if (bodyJson is Map &&
            bodyJson.containsKey('version') &&
            bodyJson.containsKey('signal_header')) {
          final header = bodyJson['signal_header'] as Map? ?? {};
          final msgType = header['msg_type'] as String? ?? '';
          if (msgType == 'prekey' || msgType == 'init') {
            isHandshakeMessage = true;
            textBody = '';
          } else if (msgType == 'msg') {
            textBody = '[Signal encrypted message]';
          } else if (msgType == 'repair') {
            textBody = '[Signal session repair requested]';
          } else {
            textBody = '[Signal protocol message]';
          }
          native.EmailCore.logWrite('[EML] detected Signal protocol message, msg_type=$msgType');
        }
      } catch (_) {
        // Not valid JSON, keep original
      }
    }

    final attachments = attList.map((a) => EmlAttachment(
      filename: a['filename'] as String? ?? 'unknown',
      contentType: a['content_type'] as String? ?? '',
      size: a['size'] as int? ?? 0,
    )).toList();

    native.EmailCore.logWrite('[EML] effectiveXMailer=$effectiveXMailer, isHandshake=$isHandshakeMessage, textLen=${textBody.length}');

    final displayBody = textBody.isNotEmpty ? textBody : _stripHtml(htmlBody);

    final result = EmlParsedContent(
      textBody: displayBody,
      htmlBody: htmlBody,
      attachments: attachments,
      hasAttachments: hasAtt,
      isHandshakeMessage: isHandshakeMessage,
    );
    _emlCache[cacheKey] = result;
    return result;
  } catch (_) {
    final result = EmlParsedContent();
    _emlCache[cacheKey] = result;
    return result;
  }
}

String _stripHtml(String html) {
  if (html.isEmpty) return '';
  var text = html.replaceAll(RegExp(r'<[^>]+>'), '');
  text = text.replaceAll('&nbsp;', ' ');
  text = text.replaceAll('&amp;', '&');
  text = text.replaceAll('&lt;', '<');
  text = text.replaceAll('&gt;', '>');
  text = text.replaceAll('&quot;', '"');
  text = text.replaceAll('&#39;', "'");
  text = text.replaceAll(RegExp(r'\s+'), ' ').trim();
  return text;
}
