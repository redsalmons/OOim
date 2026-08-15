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

EmlParsedContent parseEmlFile(String filePath, {String? account}) {
  // Clear cache if file path format changed (from uuid to file field)
  if (_emlCache.length > 1000) {
    _emlCache.clear();
  }
  final cacheKey = account != null ? '$filePath|$account' : filePath;
  if (_emlCache.containsKey(cacheKey)) {
    return _emlCache[cacheKey]!;
  }

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

    // Check if body is encrypted data (JSON with "text" and "session_info.code")
    if (textBody.isNotEmpty && textBody.contains('"text"') && textBody.contains('"session_info"')) {
      try {
        final bodyJson = jsonDecode(textBody);
        if (bodyJson is Map && bodyJson.containsKey('text') && bodyJson.containsKey('session_info')) {
          // This is an encrypted data body - try to decrypt
          if (account != null && account.isNotEmpty) {
            final outBuf = malloc.allocate<Utf8>(65536);
            try {
              final rc = native.EmailCore.decryptDataBody(textBody, account, outBuf, 65536);
              if (rc == 0) {
                textBody = outBuf.toDartString();
              }
            } finally {
              malloc.free(outBuf);
            }
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
