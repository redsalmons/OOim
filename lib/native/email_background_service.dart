import 'dart:async';
import 'dart:convert';
import 'dart:isolate';
import 'dart:io';
import 'package:path_provider/path_provider.dart';
import 'email_core.dart' as native;

// ---------------------------------------------------------------------------
// Message types for communication between UI ↔ supervisor ↔ child isolates
// ---------------------------------------------------------------------------

/// Messages sent from UI to supervisor isolate
class SupervisorCommand {
  final String type; // 'start', 'stop', 'config_path'
  final String? configPath;
  SupervisorCommand(this.type, {this.configPath});
}

/// Messages sent from supervisor to UI
class SupervisorMessage {
  final String type; // 'new_emails', 'child_started', 'child_exited', 'error', 'log', 'email_sent', 'bodies_downloaded'
  final String? account;
  final Map<String, dynamic>? data;
  SupervisorMessage(this.type, {this.account, this.data});
}

// ---------------------------------------------------------------------------
// Supervisor isolate entry point
// ---------------------------------------------------------------------------

/// Runs in a persistent isolate. Scans email config periodically, spawns one
/// child isolate per account/folder, and starts/stops children when config changes.
void supervisorEntryPoint(SendPort mainSendPort) {
  final receivePort = ReceivePort();
  mainSendPort.send(receivePort.sendPort);

  String? configPath;
  bool running = false;
  final Map<String, Isolate> childIsolates = {};
  final Map<String, SendPort> childSendPorts = {};
  final Map<String, bool> childAlive = {};
  // Track the authCode per account email to detect credential changes
  final Map<String, String> accountAuthCodes = {};
  // Track which emails are currently configured
  Set<String> configuredEmails = {};

  void log(String msg) {
    mainSendPort.send(SupervisorMessage('log', data: {'msg': msg}));
  }

  void stopChild(String childKey) {
    if (childAlive[childKey] == true) {
      log('Stopping child for $childKey');
      childSendPorts[childKey]?.send({'type': 'stop'});
      childIsolates[childKey]?.kill(priority: Isolate.immediate);
    }
    childIsolates.remove(childKey);
    childSendPorts.remove(childKey);
    childAlive.remove(childKey);
  }

  void startChild(String email, String authCode, String accountType, String folder, int configIndex, {String? storageDir}) {
    final childKey = '$email:$folder';
    if (childAlive[childKey] == true) {
      log('Child for $childKey already running, skipping');
      return;
    }

    final childReceivePort = ReceivePort();
    final isolate = Isolate.spawn(
      (SendPort supervisorPort) => childEntryPoint(supervisorPort),
      childReceivePort.sendPort,
      onExit: mainSendPort,
      onError: mainSendPort,
    );

    isolate.then((iso) {
      childIsolates[childKey] = iso;
      childAlive[childKey] = true;
      log('Child isolate started for $childKey with configIndex: $configIndex');

      childReceivePort.listen((message) {
        if (message is SendPort) {
          childSendPorts[childKey] = message;
          message.send({
            'type': 'start',
            'email': email,
            'authCode': authCode,
            'accountType': accountType,
            'folder': folder,
            'configIndex': configIndex,
            'configPath': configPath,
            if (storageDir != null) 'storageDir': storageDir,
          });
        } else if (message is Map) {
          final msg = SupervisorMessage(
            message['type'] as String? ?? 'unknown',
            account: email,
            data: message['data'] != null
                ? Map<String, dynamic>.from(message['data'] as Map)
                : null,
          );
          mainSendPort.send(msg);
        }
      });
    }).catchError((e) {
      log('Failed to spawn child for $childKey: $e');
      childAlive[childKey] = false;
    });
  }

  void scanConfigAndStart() {
    if (configPath == null) {
      log('No config path set');
      return;
    }

    final config = native.EmailCore.loadConfig(configPath!);
    if (config == null || config.accounts.isEmpty) {
      log('No config or no accounts found, stopping all children');
      for (final key in childAlive.keys.toList()) {
        stopChild(key);
      }
      accountAuthCodes.clear();
      configuredEmails = {};
      return;
    }

    for (final account in config.accounts) {
      log('Account: ${account.email}, id: ${account.id}');
    }

    // Build the set of currently valid emails
    final newEmails = <String>{};
    final newAuthCodes = <String, String>{};

    for (final account in config.accounts) {
      if (account.email.isNotEmpty && account.authCode.isNotEmpty) {
        newEmails.add(account.email);
        newAuthCodes[account.email] = account.authCode;
      }
    }

    // Stop children for emails no longer in config or with changed credentials
    for (final email in accountAuthCodes.keys.toList()) {
      if (!newEmails.contains(email)) {
        log('Account $email removed from config, stopping children');
        stopChild('$email:INBOX');
        stopChild('$email:Sent');
        stopChild('$email:Download');
        stopChild('$email:SendTask');
        accountAuthCodes.remove(email);
      } else if (accountAuthCodes[email] != newAuthCodes[email]) {
        log('Account $email credentials changed, restarting children');
        stopChild('$email:INBOX');
        stopChild('$email:Sent');
        stopChild('$email:Download');
        stopChild('$email:SendTask');
        accountAuthCodes[email] = newAuthCodes[email]!;
      }
    }

    // Start children for new emails
    for (final account in config.accounts) {
      if (account.email.isNotEmpty && account.authCode.isNotEmpty) {
        final alreadyRunning = accountAuthCodes.containsKey(account.email);
        if (alreadyRunning) continue;

        log('New account detected: ${account.email}, creating instances');
        int inboxConfigIndex;
        int sentConfigIndex;
        int downloadConfigIndex;

        if (account.type == 'outlook.com') {
          // Outlook accounts don't need config file, use AddOutlookEmail
          try {
            inboxConfigIndex = native.EmailCore.oemailimAddOutlookEmail();
          } catch (e) {
            log('ERROR calling oemailimAddOutlookEmail for INBOX: $e');
            continue;
          }
          try {
            sentConfigIndex = native.EmailCore.oemailimAddOutlookEmail();
          } catch (e) {
            log('ERROR calling oemailimAddOutlookEmail for SENT: $e');
            continue;
          }
          try {
            downloadConfigIndex = native.EmailCore.oemailimAddOutlookEmail();
          } catch (e) {
            log('ERROR calling oemailimAddOutlookEmail for Download: $e');
            continue;
          }
          // Set refresh token for Outlook accounts
          native.EmailCore.setRefreshToken(inboxConfigIndex, account.authCode);
          native.EmailCore.setRefreshToken(sentConfigIndex, account.authCode);
          native.EmailCore.setRefreshToken(downloadConfigIndex, account.authCode);
        } else {
          final emailId = account.id;
          if (emailId.isEmpty) {
            log('Email ID is missing for ${account.email}, skipping');
            continue;
          }
          try {
            inboxConfigIndex = native.EmailCore.oemailimOpenNewEmail(emailId);
          } catch (e) {
            log('ERROR calling oemailimOpenNewEmail for INBOX: $e');
            continue;
          }
          try {
            sentConfigIndex = native.EmailCore.oemailimOpenNewEmail(emailId);
          } catch (e) {
            log('ERROR calling oemailimOpenNewEmail for SENT: $e');
            continue;
          }
          try {
            downloadConfigIndex = native.EmailCore.oemailimOpenNewEmail(emailId);
          } catch (e) {
            log('ERROR calling oemailimOpenNewEmail for Download: $e');
            continue;
          }
        }

        if (inboxConfigIndex < 0 || sentConfigIndex < 0 || downloadConfigIndex < 0) {
          log('Failed to create email instances for ${account.email}');
          continue;
        }

        accountAuthCodes[account.email] = account.authCode;

        startChild(account.email, account.authCode, account.type, 'INBOX', inboxConfigIndex);
        // Sent folder watch disabled for all accounts
        // startChild(account.email, account.authCode, account.type, 'Sent', sentConfigIndex);
        startChild(account.email, account.authCode, account.type, 'Download', downloadConfigIndex, storageDir: configPath?.replaceAll('/config/oim.conf', '/data'));
        startChild(account.email, account.authCode, account.type, 'SendTask', sentConfigIndex);
      }
    }

    configuredEmails = newEmails;
  }

  receivePort.listen((cmd) {
    if (cmd is SupervisorCommand) {
      switch (cmd.type) {
        case 'start':
          running = true;
          configPath = cmd.configPath;
          log('Supervisor starting with config: $configPath');
          scanConfigAndStart();
          break;
        case 'stop':
          running = false;
          log('Supervisor stopping all children');
          for (final key in childAlive.keys.toList()) {
            stopChild(key);
          }
          accountAuthCodes.clear();
          configuredEmails = {};
          break;
        case 'config_path':
          configPath = cmd.configPath;
          break;
        case 'rescan':
          log('Manual rescan triggered');
          scanConfigAndStart();
          break;
      }
    }
  });

  // Periodically rescan config to detect changes
  Timer.periodic(const Duration(seconds: 5), (timer) {
    if (!running) return;
    scanConfigAndStart();
  });

  log('Supervisor isolate ready');
}

// ---------------------------------------------------------------------------
// Child isolate entry point — one per email account
// ---------------------------------------------------------------------------

/// Runs in a child isolate. Connects to IMAP, fetches new emails, stores
/// them in the DB, then enters IDLE loop. On new mail notification, fetches
/// new emails again and notifies the supervisor.
void childEntryPoint(SendPort supervisorPort) {
  final receivePort = ReceivePort();
  supervisorPort.send(receivePort.sendPort);

  String? email;
  String? authCode;
  String? accountType;
  String? folder;
  String? configPath;
  String? storageDir;
  int? configIndex;
  bool shouldStop = false;

  void notify(String type, Map<String, dynamic>? data) {
    supervisorPort.send({
      'type': type,
      'data': data,
    });
  }

  void log(String msg) {
    notify('log', {'msg': '[Child $email] $msg'});
  }

  Future<bool> runFetchCycle() async {
    if (configIndex == null || email == null || folder == null) return false;

    // For Outlook, refresh token is already set from config loading, just refresh
    if (accountType == 'outlook.com') {
      final refreshResult = native.EmailCore.refreshToken(configIndex!);
      if (refreshResult != 0) {
        log('refreshToken failed: $refreshResult');
        return false;
      }
    } else { // 163
      final credResult = native.EmailCore.setEmailCredentials(configIndex!, email!, authCode!);
      if (credResult != 0) {
        log('setEmailCredentials failed: $credResult');
        return false;
      }
    }

    // Always reconnect to avoid stale connection issues
    native.EmailCore.connectEmail(configIndex!);

    // Fetch and store new emails from the specified folder
    final fetchResult = native.EmailCore.fetchAndStoreEmails(configIndex!, folder!, email!);
    log('fetchAndStoreEmails result for $folder (configIndex: $configIndex): $fetchResult');

    try {
      final decoded = jsonDecode(fetchResult);
      if (decoded['status'] == 'success') {
        final count = decoded['stored'] ?? 0;
        if (count > 0) {
          notify('new_emails', {
            'account': email,
            'count': count,
            'emails': decoded['emails'],
          });
        }
        return true;
      } else if (decoded['error'] == '163_unsafe_login') {
        notify('error', {'account': email, 'error': '163_unsafe_login'});
      } else {
        log('fetch error: ${decoded['error']}');
      }
    } catch (e) {
      log('Failed to parse fetch result: $e');
    }
    return false;
  }

  Future<void> runIdleLoop() async {
    if (configIndex == null) return;

    // SENT folder typically doesn't support IDLE, use polling for all providers
    // INBOX uses IDLE for Outlook and QQ, polling for 163
    final usePolling = accountType == '163.com' || 
                       accountType == 'emailType163';

    if (usePolling) {
      // 163 IMAP does not support IDLE command (returns "BAD command not support").
      // Outlook INBOX and Sent both use IDLE.
      // Fall back to polling: fetch every 10 seconds.
      const pollInterval = Duration(seconds: 10);
      log('Using polling for $folder (every ${pollInterval.inSeconds}s)');

      while (!shouldStop) {
        await Future.delayed(pollInterval);
        if (shouldStop) break;

        log('Polling: fetching new emails...');
        final ok = await runFetchCycle();
        if (!ok) {
          log('Polling fetch failed, waiting 5s before retry to avoid server rejection...');
          await Future.delayed(const Duration(seconds: 5));
        }
      }
    } else {
      // Outlook and others support IDLE
      log('Entering IDLE mode for real-time push...');

      while (!shouldStop) {
        // Block and wait for new email notification (timeout 300s inside C++)
        final idleResult = native.EmailCore.idleWait(configIndex!, folder!, 300);

        if (shouldStop) break;

        if (idleResult == 0) {
          log('IDLE: New email detected, fetching...');
          final ok = await runFetchCycle();
          if (!ok) {
            log('Fetch after IDLE failed, waiting 5s before retry...');
            await Future.delayed(const Duration(seconds: 5));
          }
        } else {
          // Timeout (-5) or error
          log('IDLE wait returned $idleResult, reconnecting/refreshing...');
          final ok = await runFetchCycle();
          if (!ok) {
            await Future.delayed(const Duration(seconds: 5));
          }
        }
      }
    }
  }

  Future<void> runDownloadLoop() async {
    if (configIndex == null || email == null || storageDir == null) return;

    log('Download loop started for $email, storageDir: $storageDir, configIndex: $configIndex');

    while (!shouldStop) {
      try {
        // Check if there are any pending emails to download (no IMAP connection needed)
        final pendingCount = native.EmailCore.countPendingBodies(email!);
        log('Download loop check: pendingCount=$pendingCount for $email');
        
        if (pendingCount <= 0) {
          // No pending emails, skip IMAP connection
          await Future.delayed(const Duration(seconds: 3));
          continue;
        }

        log('Found $pendingCount pending emails, connecting to download...');

        // Refresh token for Outlook before connecting
        if (accountType == 'outlook.com') {
          native.EmailCore.refreshToken(configIndex!);
        } else {
          // For 163/QQ, set credentials before connecting
          final credResult = native.EmailCore.setEmailCredentials(configIndex!, email!, authCode!);
          log('Download loop: setEmailCredentials result=$credResult');
        }

        // Connect to ensure IMAP session is alive
        final connectResult = native.EmailCore.connectEmail(configIndex!);
        log('Download loop: connectEmail result=$connectResult');

        // Download pending bodies (islocal=0)
        final result = native.EmailCore.downloadPendingBodies(configIndex!, email!, storageDir!);
        log('Download loop: downloadPendingBodies result=$result');
        
        try {
          final decoded = jsonDecode(result);
          if (decoded['status'] == 'success') {
            final count = decoded['downloaded'] ?? 0;
            if (count > 0) {
              log('Downloaded $count email bodies');
              notify('bodies_downloaded', {
                'account': email,
                'count': count,
              });
            } else {
              log('Downloaded 0 email bodies (no new downloads)');
            }
          } else {
            log('Download error: ${decoded['error']}');
          }
        } catch (e) {
          log('Failed to parse download result: $e, result=$result');
        }
      } catch (e) {
        log('Download loop exception: $e');
      }

      // Wait 3 seconds before next check
      await Future.delayed(const Duration(seconds: 3));
    }
  }

  Future<void> runSendTaskLoop() async {
    if (configIndex == null || email == null) return;

    log('SendTask loop started for $email, configIndex: $configIndex');

    while (!shouldStop) {
      try {
        // Set credentials for SMTP
        if (accountType == 'outlook.com' || accountType == 'gmail.com') {
          native.EmailCore.refreshToken(configIndex!);
        } else {
          native.EmailCore.setEmailCredentials(configIndex!, email!, authCode!);
        }

        // Process pending send tasks
        final result = native.EmailCore.taskProcessPending(configIndex!, email!);
        log('SendTask loop: taskProcessPending for account=$email, configIndex=$configIndex, result=$result');

        try {
          final decoded = jsonDecode(result);
          if (decoded['status'] == 'success') {
            final sent = decoded['sent'] ?? 0;
            if (sent > 0) {
              log('SendTask: sent $sent emails');
              final tasks = decoded['tasks'] as List? ?? [];
              for (final task in tasks) {
                notify('email_sent', {
                  'account': email,
                  'task_id': task['id'],
                  'message_id': task['message_id'],
                });
              }
            }
          } else {
            log('SendTask error: ${decoded['error']}');
          }
        } catch (e) {
          log('SendTask: failed to parse result: $e');
        }
      } catch (e) {
        log('SendTask loop exception: $e');
      }

      // Wait 2 seconds before next check
      await Future.delayed(const Duration(seconds: 2));
    }
  }

  receivePort.listen((msg) async {
    if (msg is Map<String, dynamic>) {
      switch (msg['type']) {
        case 'start':
          email = msg['email'] as String?;
          authCode = msg['authCode'] as String?;
          accountType = msg['accountType'] as String?;
          folder = msg['folder'] as String? ?? 'INBOX';
          configIndex = msg['configIndex'] as int?;
          configPath = msg['configPath'] as String?;
          storageDir = msg['storageDir'] as String?;

          log('Child starting for $email folder:$folder with configIndex: $configIndex, accountType: $accountType');

          // Download mode: poll for islocal=0 emails and download bodies
          if (folder == 'Download') {
            // Set credentials for the IMAP connection
            final credResult = native.EmailCore.setEmailCredentials(configIndex!, email!, authCode!);
            log('setEmailCredentials result (Download): $credResult');

            await runDownloadLoop();
            notify('child_exited', {'account': email});
            break;
          }

          // SendTask mode: poll task table every 2 seconds and send emails
          if (folder == 'SendTask') {
            log('SendTask mode starting for $email');
            await runSendTaskLoop();
            notify('child_exited', {'account': email});
            break;
          }

          // For Outlook, refresh token is already set from config loading
          // However, we still need to call setEmailCredentials to ensure the email address is set
          final credResult = native.EmailCore.setEmailCredentials(configIndex!, email!, authCode!);
          log('setEmailCredentials result: $credResult');

          // For Sent folder, discover the actual IMAP folder name dynamically
          if (folder == 'Sent') {
            try {
              native.EmailCore.connectEmail(configIndex!);
              final discoveredFolder = native.EmailCore.findSentFolder(configIndex!);
              log('findSentFolder discovered: "$discoveredFolder" (was "Sent")');
              folder = discoveredFolder;
            } catch (e) {
              log('findSentFolder failed: $e, using "Sent" as default');
            }
          }

          // Initial fetch — retry until success before entering IDLE
          bool fetchOk = false;
          while (!fetchOk && !shouldStop) {
            fetchOk = await runFetchCycle();
            if (!fetchOk) {
              log('Initial fetch failed, retrying in 5s...');
              await Future.delayed(const Duration(seconds: 5));
            }
          }

          if (shouldStop) break;

          // Enter IDLE loop
          await runIdleLoop();

          // If we exit the IDLE loop, notify supervisor
          notify('child_exited', {'account': email});
          break;

        case 'stop':
          log('Received stop command');
          shouldStop = true;
          break;
      }
    }
  });
}

// ---------------------------------------------------------------------------
// UI-side controller class
// ---------------------------------------------------------------------------

/// Manages the supervisor isolate from the UI thread.
/// Call [start] to begin background email monitoring, [stop] to shut it down.
class EmailBackgroundService {
  Isolate? _supervisorIsolate;
  SendPort? _supervisorPort;
  ReceivePort? _mainReceivePort;
  StreamSubscription? _subscription;

  final void Function(SupervisorMessage) onMessage;

  EmailBackgroundService({required this.onMessage});

  Future<void> start(String configPath) async {
    if (_supervisorIsolate != null) {
      onMessage(SupervisorMessage('log', data: {'msg': 'Service already running'}));
      return;
    }

    _mainReceivePort = ReceivePort();

    _supervisorIsolate = await Isolate.spawn(
      supervisorEntryPoint,
      _mainReceivePort!.sendPort,
    );

    _subscription = _mainReceivePort!.listen((message) {
      if (message is SendPort) {
        _supervisorPort = message;
        _supervisorPort!.send(SupervisorCommand('start', configPath: configPath));
      } else if (message is SupervisorMessage) {
        onMessage(message);
      } else if (message is List) {
        // Isolate.onExit / onError — find which child died
        // For now, just log
        onMessage(SupervisorMessage('log', data: {'msg': 'Isolate event: $message'}));
      }
    });
  }

  Future<void> stop() async {
    _supervisorPort?.send(SupervisorCommand('stop'));
    await Future.delayed(const Duration(milliseconds: 500));
    _supervisorIsolate?.kill(priority: Isolate.immediate);
    _supervisorIsolate = null;
    _supervisorPort = null;
    _subscription?.cancel();
    _subscription = null;
    _mainReceivePort?.close();
    _mainReceivePort = null;
  }

  bool get isRunning => _supervisorIsolate != null;

  void rescan() {
    _supervisorPort?.send(SupervisorCommand('rescan'));
  }
}
