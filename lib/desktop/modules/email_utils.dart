import 'dart:convert';
import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import '../../i18n/app_strings.dart';

Future<String> fetchEmailsInIsolate(Map<String, dynamic> params) async {
  final email = params['email'] as String;
  final authCode = params['authCode'] as String;
  final configIndex = params['configIndex'] as int;

  final credResult = native.EmailCore.setEmailCredentials(configIndex, email, authCode);
  if (credResult != 0) {
    return jsonEncode({'status': 'failed', 'error': 'cred_failed'});
  }

  final connectResult = native.EmailCore.connectEmail(configIndex);
  if (connectResult != 0) {
    return jsonEncode({'status': 'failed', 'error': 'connect_failed'});
  }

  // Fetch INBOX
  native.EmailCore.fetchAndStoreEmails(configIndex, 'INBOX', email);
  
  // Try to determine Sent folder name and fetch it
  String sentFolder = 'Sent';
  try {
    final folderResult = native.EmailCore.findSentFolder(configIndex);
    if (folderResult.isNotEmpty) {
      sentFolder = folderResult;
    }
  } catch (_) {}
  
  final fetchResult = native.EmailCore.fetchAndStoreEmails(configIndex, sentFolder, email);
  return fetchResult;
}

DateTime? parseDate(String timestamp) {
  try {
    final cleaned = timestamp.replaceAll(RegExp(r' \(.*?\)'), '').trim();
    const months = {
      'Jan': '01', 'Feb': '02', 'Mar': '03', 'Apr': '04',
      'May': '05', 'Jun': '06', 'Jul': '07', 'Aug': '08',
      'Sep': '09', 'Oct': '10', 'Nov': '11', 'Dec': '12',
    };
    final match = RegExp(
      r'(\w{3},\s*)?(\d{1,2})\s+(\w{3})\s+(\d{4})\s+(\d{2}):(\d{2}):(\d{2})\s*([+-]\d{4})?',
    ).firstMatch(cleaned);
    if (match != null) {
      final day = match.group(2)!.padLeft(2, '0');
      final month = months[match.group(3)!]!;
      final year = match.group(4)!;
      final hour = match.group(5)!;
      final minute = match.group(6)!;
      final second = match.group(7)!;
      final tz = match.group(8) ?? '+0000';
      final iso = '$year-$month-${day}T$hour:$minute:$second$tz';
      return DateTime.parse(iso).toLocal();
    }
    return DateTime.parse(cleaned).toLocal();
  } catch (_) {
    return null;
  }
}

String formatTime(String timestamp) {
  if (timestamp.isEmpty) return '';
  final dt = parseDate(timestamp);
  if (dt == null) return timestamp;
  final now = DateTime.now();
  final today = DateTime(now.year, now.month, now.day);
  final emailDate = DateTime(dt.year, dt.month, dt.day);
  final diff = today.difference(emailDate).inDays;
  if (diff == 0) {
    return '${AppStrings.today} ${_systemTime(dt)}';
  } else if (diff == 1) {
    return '${AppStrings.yesterday} ${_systemTime(dt)}';
  } else if (diff < 7) {
    return '${AppStrings.weekdays[dt.weekday - 1]} ${_systemTime(dt)}';
  } else {
    return '${_systemDate(dt)} ${_systemTime(dt)}';
  }
}

String formatTimeShort(String timestamp) {
  if (timestamp.isEmpty) return '';
  final dt = parseDate(timestamp);
  if (dt == null) return timestamp;
  final now = DateTime.now();
  final today = DateTime(now.year, now.month, now.day);
  final emailDate = DateTime(dt.year, dt.month, dt.day);
  final diff = today.difference(emailDate).inDays;
  if (diff == 0) {
    return _systemTime(dt);
  } else if (diff < 7) {
    final weekdays = AppStrings.weekdays;
    return weekdays[dt.weekday - 1];
  } else {
    return _systemDate(dt);
  }
}

String _systemDate(DateTime dt) {
  return '${dt.year}/${dt.month.toString().padLeft(2, '0')}/${dt.day.toString().padLeft(2, '0')}';
}

String _systemTime(DateTime dt) {
  return '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}:${dt.second.toString().padLeft(2, '0')}';
}

bool hasAttachment(String bodystructure) {
  if (bodystructure.isEmpty) return false;
  try {
    final parsed = jsonDecode(bodystructure);
    if (parsed is Map) {
      final type = parsed['type']?.toString() ?? '';
      final subtype = parsed['subtype']?.toString() ?? '';
      if (type == 'multipart' && subtype == 'mixed') {
        final parts = parsed['parts'] as List?;
        if (parts != null && parts.length > 1) {
          for (final part in parts) {
            final partType = part['type']?.toString() ?? '';
            final partSubtype = part['subtype']?.toString() ?? '';
            if (partType != 'text') return true;
            if (partType == 'text' && partSubtype != 'plain' && partSubtype != 'html') return true;
          }
        }
      }
      if (type == 'application' || subtype == 'attachment') return true;
    }
    return false;
  } catch (_) {
    return false;
  }
}

String extractName(String sender) {
  final lt = sender.indexOf('<');
  if (lt > 0) {
    return sender.substring(0, lt).trim();
  }
  return sender.isEmpty ? AppStrings.unknown : sender;
}

Color avatarColor(String name) {
  final colors = [
    Colors.blue[700]!, Colors.purple[700]!, Colors.teal[700]!,
    Colors.orange[700]!, Colors.pink[700]!, Colors.indigo[700]!,
    Colors.green[700]!, Colors.brown[700]!,
  ];
  int hash = 0;
  for (int i = 0; i < name.length; i++) {
    hash = hash * 31 + name.codeUnitAt(i);
  }
  return colors[hash.abs() % colors.length];
}

String previewFor(native.EmailMessage email) {
  final body = email.body;
  // body currently stores bodystructure JSON, not actual email text
  if (body.startsWith('{') || body.startsWith('[')) return '';
  final firstLine = body.split('\n').firstWhere((l) => l.trim().isNotEmpty, orElse: () => '');
  return firstLine.trim();
}
