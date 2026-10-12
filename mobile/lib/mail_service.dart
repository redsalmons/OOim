import 'package:flutter/foundation.dart';
import 'package:oim_core/native/email_background_service.dart';

/// Owns the background mail service and broadcasts a tick whenever mail
/// state changes so open pages can reload from the database.
class MailService {
  MailService._();
  static final MailService instance = MailService._();

  final ValueNotifier<int> tick = ValueNotifier<int>(0);
  EmailBackgroundService? _bg;

  void start(String configPath) {
    if (_bg != null) return;
    _bg = EmailBackgroundService(
      onMessage: (msg) {
        switch (msg.type) {
          case 'new_emails':
          case 'email_sent':
          case 'bodies_downloaded':
            tick.value++;
            break;
        }
      },
    )..start(configPath);
  }
}
