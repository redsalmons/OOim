import 'dart:convert';
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

EmlParsedContent parseEmlFile(String filePath) {
  // Clear cache if file path format changed (from uuid to file field)
  if (_emlCache.length > 1000) {
    _emlCache.clear();
  }
  if (_emlCache.containsKey(filePath)) {
    return _emlCache[filePath]!;
  }

  try {
    final jsonStr = native.EmailCore.parseEml(filePath);
    final decoded = jsonDecode(jsonStr);

    if (decoded['status'] != 'success') {
      final result = EmlParsedContent();
      _emlCache[filePath] = result;
      return result;
    }

    final textBody = decoded['text_body'] as String? ?? '';
    final htmlBody = decoded['html_body'] as String? ?? '';
    final hasAtt = decoded['has_attachments'] as bool? ?? false;
    final attList = decoded['attachments'] as List? ?? [];

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
    _emlCache[filePath] = result;
    return result;
  } catch (_) {
    final result = EmlParsedContent();
    _emlCache[filePath] = result;
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
