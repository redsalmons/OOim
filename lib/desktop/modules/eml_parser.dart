import 'dart:convert';
import 'dart:ffi';
import 'package:ffi/ffi.dart';
import '../../native/email_core.dart' as native;

class EmlParsedContent {
  final String textBody;
  final String htmlBody;
  final List<EmlAttachment> attachments;
  final bool hasAttachments;

  EmlParsedContent({
    this.textBody = '',
    this.htmlBody = '',
    this.attachments = const [],
    this.hasAttachments = false,
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

EmlParsedContent parseEmlFile(String filePath, {String? account}) {
  // Clear cache if it grows too large
  if (_emlCache.length > 1000) {
    _emlCache.clear();
  }
  final cacheKey = account != null ? '$filePath|$account' : filePath;
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
                  textBody = outBuf.toDartString();
                  native.EmailCore.logWrite('[EML] decrypt success');
                } else {
                  native.EmailCore.logWrite('[EML] decrypt failed, rc=$rc');
                }
              } finally {
                malloc.free(outBuf);
              }
            } else {
              native.EmailCore.logWrite('[EML] decrypt skipped: account is null or empty');
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

    final attachments = attList.map((a) => EmlAttachment(
      filename: a['filename'] as String? ?? 'unknown',
      contentType: a['content_type'] as String? ?? '',
      size: a['size'] as int? ?? 0,
    )).toList();

    final displayBody = textBody.isNotEmpty ? textBody : _stripHtml(htmlBody);

    final result = EmlParsedContent(
      textBody: displayBody,
      htmlBody: htmlBody,
      attachments: attachments,
      hasAttachments: hasAtt,
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
