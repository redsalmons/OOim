import 'dart:io';
import 'dart:convert';
import 'dart:isolate';
import 'package:flutter/material.dart';
import 'package:desktop_drop/desktop_drop.dart';
import '../../native/email_core.dart' as native;
import 'email_utils.dart';
import 'email_module_base.dart';
import 'eml_parser.dart';

// Helper function to parse EmailMessage from JSON
native.EmailMessage _emailMessageFromJson(Map<String, dynamic> json) {
  return native.EmailMessage(
    sender: json['sender'] ?? '',
    recipient: json['recipient'] ?? '',
    subject: json['subject'] ?? '',
    body: json['body'] ?? '',
    timestamp: json['date'] ?? '',
    uuid: json['uuid'] ?? '',
    flags: (json['flags'] as String? ?? '').split(',').where((s) => s.isNotEmpty).toList(),
    isAnswered: json['flags']?.toString().contains('\\Answered') ?? false,
    inReplyTo: json['in_reply_to'] ?? '',
    messageId: json['message_id'] ?? '',
    folder: json['folder'] ?? 'INBOX',
    isLocal: json['islocal'] ?? 0,
    sessionId: json['session_id'] ?? '',
    rowid: json['rowid'] ?? 0,
    toAddr: json['to_addr'] ?? '',
    file: json['file'] ?? '',
  );
}

class DroppedFile {
  final String name;
  final String path;
  final int size;
  DroppedFile({required this.name, required this.path, required this.size});
}

class RichTextReplyController extends TextEditingController {
  List<DroppedFile> droppedFiles;
  VoidCallback? onChangedCallback;

  RichTextReplyController({String text = '', required this.droppedFiles})
      : super(text: text);

  @override
  TextSpan buildTextSpan(
      {required BuildContext context,
      TextStyle? style,
      required bool withComposing}) {
    final text = this.text;
    final children = <InlineSpan>[];
    int start = 0;
    int fileIndex = 0;
    for (int i = 0; i < text.length; i++) {
      if (text.codeUnitAt(i) == 0xFFFC) {
        if (i > start) {
          children.add(TextSpan(text: text.substring(start, i), style: style));
        }
        if (fileIndex < droppedFiles.length) {
          children.add(WidgetSpan(
            alignment: PlaceholderAlignment.middle,
            child: _buildInlineFileChip(droppedFiles[fileIndex], fileIndex),
          ));
          fileIndex++;
        }
        start = i + 1;
      }
    }
    if (start < text.length) {
      children.add(TextSpan(text: text.substring(start), style: style));
    }
    if (children.isEmpty) {
      return TextSpan(text: text, style: style);
    }
    return TextSpan(children: children, style: style);
  }

  Widget _buildInlineFileChip(DroppedFile file, int index) {
    final ext = file.name.split('.').last.toLowerCase();
    IconData fileIcon;
    Color iconColor;
    if (['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp'].contains(ext)) {
      fileIcon = Icons.image;
      iconColor = Colors.blue[600]!;
    } else if (['pdf'].contains(ext)) {
      fileIcon = Icons.picture_as_pdf;
      iconColor = Colors.red[600]!;
    } else if (['doc', 'docx'].contains(ext)) {
      fileIcon = Icons.description;
      iconColor = Colors.blue[800]!;
    } else if (['xls', 'xlsx'].contains(ext)) {
      fileIcon = Icons.table_chart;
      iconColor = Colors.green[800]!;
    } else if (['zip', 'rar', '7z', 'tar', 'gz'].contains(ext)) {
      fileIcon = Icons.folder_zip;
      iconColor = Colors.orange[700]!;
    } else {
      fileIcon = Icons.insert_drive_file;
      iconColor = Colors.grey[600]!;
    }

    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 1),
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
      decoration: BoxDecoration(
        color: const Color(0xFFF0F0F0),
        borderRadius: BorderRadius.circular(3),
        border: Border.all(color: Colors.grey[300]!, width: 0.5),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(fileIcon, size: 12, color: iconColor),
          const SizedBox(width: 3),
          ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 120),
            child: Text(file.name, style: const TextStyle(fontSize: 12, height: 1.4), maxLines: 1, overflow: TextOverflow.ellipsis),
          ),
          const SizedBox(width: 2),
          GestureDetector(
            onTap: () {
              final text = this.text;
              int objIdx = 0;
              int charIdx = 0;
              for (int i = 0; i < text.length; i++) {
                if (text.codeUnitAt(i) == 0xFFFC) {
                  if (objIdx == index) {
                    charIdx = i;
                    break;
                  }
                  objIdx++;
                }
              }
              final newText = text.substring(0, charIdx) + text.substring(charIdx + 1);
              this.value = TextEditingValue(
                text: newText,
                selection: TextSelection.collapsed(offset: charIdx.clamp(0, newText.length)),
              );
              droppedFiles.removeAt(index);
              onChangedCallback?.call();
            },
            child: Icon(Icons.close, size: 12, color: Colors.grey[500]),
          ),
        ],
      ),
    );
  }
}

mixin ConversationViewMixin on State<EmailModule> {
  List<native.EmailMessage> get emails;
  List<native.EmailMessage> get conversationEmails;
  String? get selectedConversationMessageId;
  bool get isConversationView;
  bool get showConversationPanel;
  set showConversationPanel(bool v);
  TextEditingController get replyController;
  String get configPath;
  String get emailDataPath;
  Map<String, int> get configIndexMap;
  VoidCallback? get onRefresh;

  String _extractEmailAddress(String sender) {
    final lt = sender.indexOf('<');
    final gt = sender.indexOf('>');
    if (lt >= 0 && gt > lt) {
      return sender.substring(lt + 1, gt).trim().toLowerCase();
    }
    return sender.trim().toLowerCase();
  }

  Set<String> _getAccountEmails() {
    try {
      final config = native.EmailCore.loadConfig(configPath);
      if (config != null) {
        return config.accounts
            .where((a) => a.email.isNotEmpty)
            .map((a) => a.email.toLowerCase())
            .toSet();
      }
    } catch (_) {}
    return {};
  }

  List<DroppedFile> get droppedFiles;
  bool _isDragging = false;

  Widget buildAvatar(String name) {
    final displayName = name.isEmpty ? '?' : name[0];
    return Container(
      width: 32,
      height: 32,
      decoration: BoxDecoration(color: avatarColor(name), borderRadius: BorderRadius.circular(16)),
      child: Center(child: Text(displayName, style: const TextStyle(color: Colors.white, fontSize: 13, fontWeight: FontWeight.w500))),
    );
  }

  List<native.EmailMessage> buildConversationRoots(List<native.EmailMessage> emailList) {
    // Group emails by session_id
    final sessionMap = <String, List<native.EmailMessage>>{};
    for (final e in emailList) {
      if (e.sessionId.isNotEmpty) {
        if (!sessionMap.containsKey(e.sessionId)) {
          sessionMap[e.sessionId] = [];
        }
        sessionMap[e.sessionId]!.add(e);
      }
    }

    // For each session, find the root email (first one chronologically)
    final roots = <native.EmailMessage>[];
    for (final sessionEmails in sessionMap.values) {
      if (sessionEmails.isEmpty) continue;
      
      // Sort by rowid to find the earliest email
      sessionEmails.sort((a, b) {
        return a.rowid.compareTo(b.rowid);
      });
      
      // The earliest email in the session is the root
      roots.add(sessionEmails.first);
    }

    // Sort roots by most recent activity (latest rowid in the session)
    roots.sort((a, b) {
      final aSessionEmails = sessionMap[a.sessionId] ?? [];
      final bSessionEmails = sessionMap[b.sessionId] ?? [];
      
      final aLatestUid = aSessionEmails.isEmpty ? 0 : aSessionEmails.last.rowid;
      final bLatestUid = bSessionEmails.isEmpty ? 0 : bSessionEmails.last.rowid;
      
      return bLatestUid.compareTo(aLatestUid);
    });

    return roots;
  }

  List<native.EmailMessage> buildConversationThread(String sessionId) {
    // Only show emails from INBOX folder
    final thread = emails
        .where((e) => e.sessionId == sessionId && e.folder == 'INBOX')
        .toList();

    // Sort by rowid (insertion order = chronological)
    thread.sort((a, b) {
      return a.rowid.compareTo(b.rowid);
    });

    return thread;
  }

  List<native.EmailMessage> buildConversationThreadLegacy(String rootMessageId) {
    final root = emails.firstWhere(
      (e) => e.messageId == rootMessageId,
      orElse: () => emails.first,
    );

    // Build children map: parentMessageId -> list of replies
    final childrenByInReplyTo = <String, List<native.EmailMessage>>{};
    for (final e in emails) {
      if (e.inReplyTo.isNotEmpty) {
        childrenByInReplyTo.putIfAbsent(e.inReplyTo, () => []).add(e);
      }
    }

    // Sort siblings by uuid (same mailbox internal ordering)
    for (final key in childrenByInReplyTo.keys) {
      childrenByInReplyTo[key]!.sort((a, b) {
        final aUid = int.tryParse(a.uuid) ?? 0;
        final bUid = int.tryParse(b.uuid) ?? 0;
        return aUid.compareTo(bUid);
      });
    }

    // DFS traversal following reply chain
    final thread = <native.EmailMessage>[];
    final visited = <String>{};

    void dfs(String messageId) {
      if (visited.contains(messageId)) return;
      visited.add(messageId);

      final email = emails.firstWhere(
        (e) => e.messageId == messageId,
        orElse: () => emails.first,
      );
      thread.add(email);

      final children = childrenByInReplyTo[messageId] ?? [];
      for (final child in children) {
        dfs(child.messageId);
      }
    }

    dfs(root.messageId);
    return thread;
  }

  Widget buildConversationDetail() {
    final thread = buildConversationThread(selectedConversationMessageId!);

    // Get title from thread root or from session_members
    String conversationTitle = '无主题';
    List<native.EmailMessage> effectiveThread = thread;
    if (thread.isNotEmpty) {
      final rootEmail = thread.firstWhere(
        (e) => e.sessionId == selectedConversationMessageId,
        orElse: () => thread.first,
      );
      conversationTitle = rootEmail.subject.isEmpty ? '无主题' : rootEmail.subject;
    } else {
      conversationTitle = '新会话';
    }

    final accountEmails = _getAccountEmails();

    return Expanded(
      child: Stack(
        children: [
          Column(
            children: [
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                decoration: BoxDecoration(
                  color: Colors.grey[50],
                  border: Border(bottom: BorderSide(color: Colors.grey[200]!)),
                ),
                child: Row(
                  children: [
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(conversationTitle, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500), maxLines: 1, overflow: TextOverflow.ellipsis),
                    ),
                    IconButton(
                      icon: Icon(Icons.more_horiz, size: 22, color: Colors.grey[700]),
                      onPressed: () {
                        setState(() {
                          showConversationPanel = true;
                        });
                      },
                    ),
                  ],
                ),
              ),
              Expanded(
                child: GestureDetector(
                  onTap: () {
                    if (showConversationPanel) {
                      setState(() {
                        showConversationPanel = false;
                      });
                    }
                  },
                  child: Container(
                    color: const Color(0xFFEDEDED),
                    child: thread.isEmpty
                        ? Center(
                            child: Column(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Icon(Icons.chat_bubble_outline, size: 48, color: Colors.grey[400]),
                                const SizedBox(height: 12),
                                Text('会话已创建，等待邮件到达', style: TextStyle(fontSize: 14, color: Colors.grey[500])),
                              ],
                            ),
                          )
                        : ListView.builder(
                            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                            itemCount: thread.length,
                            itemBuilder: (context, index) {
                              final email = thread[index];
                              final senderEmail = _extractEmailAddress(email.sender);
                              final isMe = senderEmail == email.recipient.toLowerCase();
                              return buildChatBubble(email, isMe);
                            },
                          ),
                  ),
                ),
              ),
              buildChatInputBar(effectiveThread),
            ],
          ),
          Positioned(
            top: 0,
            right: 0,
            bottom: 0,
            child: buildConversationPanel(thread),
          ),
        ],
      ),
    );
  }

  Widget buildConversationPanel(List<native.EmailMessage> thread) {
    final members = <String, String>{};
    String ownerAddr = '';
    String sessionId = '';
    String conversationAccount = '';
    if (thread.isNotEmpty) {
      conversationAccount = thread.first.recipient.toLowerCase();
    }
    // Extract all addresses from sender, to_addr, and recipient of every email in thread
    for (final email in thread) {
      if (sessionId.isEmpty && email.sessionId.isNotEmpty) {
        sessionId = email.sessionId;
      }
      final allAddrs = <String>[email.sender];
      if (email.toAddr.isNotEmpty) {
        allAddrs.addAll(email.toAddr.split(',').map((a) => a.trim()).where((a) => a.isNotEmpty));
      }
      allAddrs.add(email.recipient);
      for (final addr in allAddrs) {
        final emailAddr = _extractEmailAddress(addr);
        if (emailAddr.isNotEmpty && !members.containsValue(emailAddr)) {
          final name = extractName(addr);
          members[name.isNotEmpty ? name : emailAddr.split('@').first] = emailAddr;
        }
      }
    }

    // The root email's sender is the group owner
    if (thread.isNotEmpty) {
      final rootEmail = thread.reduce((a, b) {
        return a.rowid < b.rowid ? a : b;
      });
      ownerAddr = _extractEmailAddress(rootEmail.sender);
    }

    return AnimatedSlide(
      duration: const Duration(milliseconds: 200),
      curve: Curves.easeOut,
      offset: showConversationPanel ? Offset.zero : const Offset(1, 0),
      child: Container(
        width: 240,
        decoration: BoxDecoration(
          color: const Color(0xFFF7F7F7),
          boxShadow: [
            BoxShadow(color: Colors.black.withOpacity(0.15), blurRadius: 12, offset: const Offset(-2, 0)),
          ],
        ),
        child: Column(
          children: [
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              child: Row(
                children: [
                  Text('会话成员', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500, color: Colors.grey[600])),
                  if (sessionId.isNotEmpty) ...[
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        sessionId.replaceFirst('session_', ''),
                        style: TextStyle(fontSize: 11, color: Colors.grey[500]),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                  const Spacer(),
                  IconButton(
                    icon: Icon(Icons.close, size: 20, color: Colors.grey[600]),
                    onPressed: () {
                      setState(() {
                        showConversationPanel = false;
                      });
                    },
                  ),
                ],
              ),
            ),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.symmetric(vertical: 4, horizontal: 8),
                children: [
                  ...members.entries.map((entry) => buildMemberItem(entry.key, entry.value, conversationAccount)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget buildMemberItem(String name, String address, String conversationAccount) {
    final isMe = conversationAccount.isNotEmpty &&
        address.toLowerCase() == conversationAccount;
    final displayName = isMe ? '我' : name;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          buildAvatar(displayName),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(displayName, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500), maxLines: 1, overflow: TextOverflow.ellipsis),
                Text(address, style: TextStyle(fontSize: 11, color: Colors.grey[500]), maxLines: 1, overflow: TextOverflow.ellipsis),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget buildChatInputBar(List<native.EmailMessage> thread) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: const Color(0xFFF7F7F7),
        border: Border(
          top: BorderSide(color: Colors.grey[300]!, width: 0.5),
        ),
      ),
      child: Row(
        children: [
          IconButton(icon: Icon(Icons.emoji_emotions_outlined, size: 24, color: Colors.grey[600]), onPressed: () {}),
          Expanded(
            child: DropTarget(
              onDragDone: (detail) {
                setState(() {
                  for (final file in detail.files) {
                    final path = file.path;
                    final name = path.split('/').last;
                    int size = 0;
                    try {
                      size = File(path).lengthSync();
                    } catch (_) {}
                    droppedFiles.add(DroppedFile(name: name, path: path, size: size));
                    final cursor = replyController.selection.baseOffset;
                    final text = replyController.text;
                    final newText = text.substring(0, cursor.clamp(0, text.length)) +
                        '\uFFFC' +
                        text.substring(cursor.clamp(0, text.length));
                    replyController.value = TextEditingValue(
                      text: newText,
                      selection: TextSelection.collapsed(offset: cursor + 1),
                    );
                  }
                  _isDragging = false;
                });
              },
              onDragEntered: (detail) {
                setState(() => _isDragging = true);
              },
              onDragExited: (detail) {
                setState(() => _isDragging = false);
              },
              child: Container(
                constraints: const BoxConstraints(maxHeight: 160),
                decoration: BoxDecoration(
                  color: Colors.white,
                  borderRadius: BorderRadius.circular(4),
                  border: Border.all(
                    color: _isDragging ? const Color(0xFF07C160) : Colors.transparent,
                    width: 2,
                  ),
                ),
                child: SingleChildScrollView(
                  child: TextField(
                    controller: replyController,
                    maxLines: 3,
                    minLines: 1,
                    decoration: InputDecoration(
                      hintText: '发送消息...',
                      hintStyle: TextStyle(fontSize: 14, color: Colors.grey[400]),
                      border: InputBorder.none,
                      contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                      isDense: true,
                    ),
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(width: 8),
          TextButton(
            style: TextButton.styleFrom(
              backgroundColor: const Color(0xFF07C160),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
            ),
            onPressed: () {
              native.EmailCore.logWrite('[SEND] Send button tapped');
              sendConversationReply(thread);
            },
            child: const Text('发送', style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
          ),
        ],
      ),
    );
  }

  void sendConversationReply(List<native.EmailMessage> thread) {
    native.EmailCore.logWrite('[SEND] sendConversationReply called, text: "${replyController.text}"');
    if (replyController.text.trim().isEmpty) {
      native.EmailCore.logWrite('[SEND] text is empty, returning');
      return;
    }

    final accountEmails = _getAccountEmails();

    // Determine subject from thread root or session
    String subject;
    String? myEmail;

    if (thread.isNotEmpty) {
      final rootEmail = thread.first;
      subject = rootEmail.subject.startsWith('Re:') ? rootEmail.subject : 'Re: ${rootEmail.subject}';
      // The conversation account is the sender
      myEmail = thread.first.recipient;
    } else {
      subject = selectedConversationMessageId ?? '新会话';
    }

    // Collect recipients from thread emails (senders + to_addr, including self)
    final recipients = <String>{};

    for (final email in thread) {
      final senderEmail = _extractEmailAddress(email.sender);
      if (senderEmail.isNotEmpty) {
        recipients.add(senderEmail);
      }
      if (email.toAddr.isNotEmpty) {
        for (final addr in email.toAddr.split(',')) {
          final extracted = _extractEmailAddress(addr.trim());
          if (extracted.isNotEmpty) {
            recipients.add(extracted);
          }
        }
      }
    }

    if (recipients.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('没有可发送的收件人'), duration: Duration(seconds: 2)),
      );
      return;
    }

    // in_reply_to is the messageId of the last message in the thread
    String inReplyTo = '';
    if (thread.isNotEmpty) {
      final lastEmail = thread.last;
      final rawMessageId = lastEmail.messageId;
      if (rawMessageId != null && 
          rawMessageId.isNotEmpty && 
          rawMessageId != '<>' && 
          rawMessageId != '<<> <>') {
        inReplyTo = rawMessageId;
      }
    }
    // Fallback: if inReplyTo is still empty, use session's index_uuid (initial message_id)
    if (inReplyTo.isEmpty && selectedConversationMessageId != null) {
      try {
        final uuidResult = native.EmailCore.querySessionIndexUuid(selectedConversationMessageId!);
        final decoded = jsonDecode(uuidResult);
        if (decoded['status'] == 'success') {
          final indexUuid = decoded['index_uuid']?.toString() ?? '';
          if (indexUuid.isNotEmpty && indexUuid != '<>') {
            inReplyTo = indexUuid;
          }
        }
      } catch (e) {
        native.EmailCore.logWrite('[SEND] Failed to query session index_uuid: $e');
      }
    }
    
    native.EmailCore.logWrite('[SEND] inReplyTo: "$inReplyTo", recipients: $recipients, myEmail: $myEmail');

    final bodyText = replyController.text.replaceAll('\uFFFC', '');
    final recipientStr = recipients.join(', ');

    // Load config to find the sender account
    final config = native.EmailCore.loadConfig(configPath);
    if (config == null || config.accounts.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('无法加载账户配置'), duration: Duration(seconds: 2)),
      );
      return;
    }

    // Find the account matching my email, or fall back to first account
    native.EmailAccountData? myAccount;
    if (myEmail != null) {
      for (final account in config.accounts) {
        if (account.email == myEmail) {
          myAccount = account;
          break;
        }
      }
    }
    myAccount ??= config.accounts.first;

    native.EmailCore.logWrite('[SEND] Using account: ${myAccount.email}, id: ${myAccount.id}');

    // Show sending indicator
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('正在发送...'), duration: Duration(seconds: 10)),
    );

    // Run SMTP sending in a background isolate to avoid blocking UI
    final accountId = myAccount.id;
    final accountEmail = myAccount.email;
    final authCode = myAccount.authCode;
    final accountType = myAccount.type;
    final imapServer = myAccount.imapServer;
    final imapPort = myAccount.imapPort;
    final smtpServer = myAccount.smtpServer;
    final smtpPort = myAccount.smtpPort;
    final sessionId = selectedConversationMessageId;
    // Generate a message_id for this reply
    final replyMessageId = '<${DateTime.now().millisecondsSinceEpoch}.${DateTime.now().microsecond}@${accountEmail.split('@').last}>';
    final content = jsonEncode({
      'recipient': recipientStr,
      'subject': subject,
      'body': bodyText,
      'in_reply_to': inReplyTo,
      'message_id': replyMessageId,
      'session_id': sessionId,
    });

    () async {
      bool ok = false;
      String errorMsg = '';
      try {
        // Yield to let UI update (show snackbar) before blocking FFI calls
        await Future.delayed(const Duration(milliseconds: 50));
        native.EmailCore.logWrite('[SEND] Starting FFI calls...');
        int ci;
        if (accountType == 'outlook.com') {
          ci = native.EmailCore.oemailimAddOutlookEmail();
          native.EmailCore.logWrite('[SEND] oemailimAddOutlookEmail result: $ci');
        } else {
          ci = native.EmailCore.oemailimOpenNewEmail(accountId);
          native.EmailCore.logWrite('[SEND] configIndex: $ci');
        }
        if (ci < 0) {
          errorMsg = '创建邮箱实例失败 ($ci)';
        } else {
          final credResult = native.EmailCore.setEmailCredentials(ci, accountEmail, authCode);
          native.EmailCore.logWrite('[SEND] setEmailCredentials: $credResult');
          native.EmailCore.oemailimSetImapServer(ci, imapServer, imapPort);
          native.EmailCore.oemailimSetSmtpServer(ci, smtpServer, smtpPort);
          final rtResult = native.EmailCore.setRefreshToken(ci, authCode);
          native.EmailCore.logWrite('[SEND] setRefreshToken: $rtResult');

          native.EmailCore.logWrite('[SEND] Sending via SMTP...');
          final sendResult = native.EmailCore.sendViaConfigRaw(ci, content);
          native.EmailCore.logWrite('[SEND] sendViaConfig result: $sendResult');
          ok = sendResult == 0;
          if (!ok) {
            final lastError = native.EmailCore.getLastError(ci);
            errorMsg = 'sendViaConfig (code=$sendResult): $lastError';
            native.EmailCore.logWrite('[SEND] lastError: $lastError');
          }
        }
      } catch (e) {
        errorMsg = '异常: $e';
        native.EmailCore.logWrite('[SEND] Error: $errorMsg');
      }

      if (ok) {
        // C++ side already inserted the email into DB and session.
        // Just reload the conversation from database to show the new email.
        native.EmailCore.logWrite('[SEND] Send succeeded, sessionId=$sessionId, replyMessageId=$replyMessageId');
        if (sessionId != null) {
          try {
            // Immediately reload the current conversation from database
            if (!mounted) return;
            native.EmailCore.logWrite('[SEND] Reloading conversation from database, sessionId=$sessionId');
            final threadResult = native.EmailCore.queryThread(sessionId!);
            native.EmailCore.logWrite('[SEND] queryThread raw result: $threadResult');
            final threadDecoded = jsonDecode(threadResult);
            native.EmailCore.logWrite('[SEND] queryThread decoded: $threadDecoded');
            if (threadDecoded['status'] == 'success') {
              final threadEmails = (threadDecoded['emails'] as List)
                  .map((e) => _emailMessageFromJson(e as Map<String, dynamic>))
                  .toList();
              native.EmailCore.logWrite('[SEND] Thread emails count: ${threadEmails.length}');
              for (int i = 0; i < threadEmails.length; i++) {
                final email = threadEmails[i];
                native.EmailCore.logWrite('[SEND] Email[$i]: uuid=${email.uuid}, messageId=${email.messageId}, sender=${email.sender}, recipient=${email.recipient}, rowid=${email.rowid}');
              }
              final sentEmailInThread = threadEmails.any((e) => e.messageId == replyMessageId);
              native.EmailCore.logWrite('[SEND] Sent email found in thread: $sentEmailInThread (looking for messageId=$replyMessageId)');
              
              conversationEmails.clear();
              conversationEmails.addAll(threadEmails);
              native.EmailCore.logWrite('[SEND] Reloaded conversation with ${threadEmails.length} emails');
              setState(() {});
            } else {
              native.EmailCore.logWrite('[SEND] queryThread failed with status: ${threadDecoded['status']}');
            }
          } catch (e) {
            native.EmailCore.logWrite('[SEND] Failed to reload conversation: $e');
          }
        } else {
          native.EmailCore.logWrite('[SEND] sessionId is null, skipping reload');
        }
        if (!mounted) return;
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        replyController.clear();
        droppedFiles.clear();
        setState(() {});
        // Notify parent to refresh emails from database
        onRefresh?.call();
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('消息已发送'), duration: Duration(seconds: 1)),
        );
      } else {
        if (!mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('发送失败: $errorMsg'), duration: const Duration(seconds: 10)),
        );
      }
    }();
  }

  Widget buildChatBubble(native.EmailMessage email, bool isMe) {
    final displayName = extractName(email.sender);

    // Determine body content and attachment status
    String bodyText = '';
    bool hasAtt = false;
    bool isDownloading = email.file.isEmpty;

    native.EmailCore.logWrite('[Conversation] email.uuid=${email.uuid}, email.file="${email.file}", email.isLocal=${email.isLocal}, email.recipient=${email.recipient}');

    if (!isDownloading) {
      // Read and parse the .eml file using file field
      final emlPath = '$emailDataPath/${email.recipient}/${email.file}.eml';
      native.EmailCore.logWrite('[Conversation] emlPath=$emlPath');
      final parsed = parseEmlFile(emlPath);
      bodyText = parsed.textBody;
      hasAtt = parsed.hasAttachments;

      // Log if islocal=1 and file is empty (should not happen)
      if (email.isLocal == 1 && email.file.isEmpty) {
        native.EmailCore.logWrite('[Conversation] islocal=1, file empty - unexpected state');
      }
    }

    // Also check bodystructure for attachment info if not from eml
    if (!hasAtt && email.body.isNotEmpty) {
      hasAtt = hasAttachment(email.body);
    }

    if (bodyText.isEmpty && !isDownloading) {
      bodyText = '';
    }

    // Determine if we should show downloading indicator
    // islocal=0, file not empty: show content + downloading indicator
    // islocal=1, file not empty: show content only
    // islocal=0, file empty: show downloading indicator only
    // islocal=1, file empty: should not happen (downloaded emails should have file)
    bool showDownloadingIndicator = (email.isLocal == 0 && !isDownloading) || isDownloading;

    return Padding(
      padding: const EdgeInsets.only(bottom: 16),
      child: Row(
        mainAxisAlignment: isMe ? MainAxisAlignment.end : MainAxisAlignment.start,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (!isMe) ...[buildAvatar(displayName), const SizedBox(width: 8)],
          Flexible(
            child: Column(
              crossAxisAlignment: isMe ? CrossAxisAlignment.end : CrossAxisAlignment.start,
              children: [
                if (!isMe)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 4, left: 4),
                    child: Text(displayName, style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                  ),
                if (hasAtt)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 4, left: 4, right: 4),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(Icons.attach_file, size: 14, color: Colors.grey[500]),
                        const SizedBox(width: 2),
                        Text('附件', style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                      ],
                    ),
                  ),
                Row(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  textDirection: isMe ? TextDirection.rtl : TextDirection.ltr,
                  children: [
                    CustomPaint(
                      size: const Size(8, 14),
                      painter: _BubbleTailPainter(
                        color: isMe ? const Color(0xFF95EC69) : Colors.white,
                        isMe: isMe,
                      ),
                    ),
                    Flexible(
                      child: Container(
                        constraints: BoxConstraints(maxWidth: MediaQuery.of(context).size.width * 0.5),
                        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                        decoration: BoxDecoration(
                          color: isMe ? const Color(0xFF95EC69) : Colors.white,
                          borderRadius: BorderRadius.circular(12),
                          boxShadow: [
                            BoxShadow(
                              color: Colors.black.withOpacity(0.06),
                              blurRadius: 4,
                              offset: const Offset(0, 1),
                            ),
                          ],
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            if (bodyText.isNotEmpty)
                              Text(bodyText, style: const TextStyle(fontSize: 14, height: 1.4, color: Colors.black87)),
                            if (showDownloadingIndicator)
                              Padding(
                                padding: const EdgeInsets.only(top: 8),
                                child: Row(
                                  mainAxisSize: MainAxisSize.min,
                                  children: [
                                    SizedBox(
                                      width: 14,
                                      height: 14,
                                      child: CircularProgressIndicator(
                                        strokeWidth: 2,
                                        color: Colors.grey[400],
                                      ),
                                    ),
                                    const SizedBox(width: 8),
                                    Text('下载中...', style: TextStyle(fontSize: 13, color: Colors.grey[400])),
                                  ],
                                ),
                              ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
                Padding(
                  padding: const EdgeInsets.only(top: 4, left: 4, right: 4),
                  child: Text(formatTimeShort(email.timestamp), style: TextStyle(fontSize: 10, color: Colors.grey[400])),
                ),
              ],
            ),
          ),
          if (isMe) ...[const SizedBox(width: 8), buildAvatar(displayName)],
        ],
      ),
    );
  }
}

class _BubbleTailPainter extends CustomPainter {
  final Color color;
  final bool isMe;

  _BubbleTailPainter({required this.color, required this.isMe});

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.fill;

    final path = Path();
    if (isMe) {
      path.moveTo(0, 0);
      path.lineTo(size.width, size.height / 2 - 2);
      path.lineTo(0, size.height);
      path.lineTo(2, size.height / 2);
    } else {
      path.moveTo(size.width, 0);
      path.lineTo(0, size.height / 2 - 2);
      path.lineTo(size.width, size.height);
      path.lineTo(size.width - 2, size.height / 2);
    }
    path.close();
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
