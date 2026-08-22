import 'dart:io';
import 'dart:convert';
import 'dart:isolate';
import 'package:flutter/material.dart';
import 'package:desktop_drop/desktop_drop.dart';
import 'package:file_picker/file_picker.dart';
import 'package:path_provider/path_provider.dart';
import '../../native/email_core.dart' as native;
import 'email_utils.dart';
import 'email_module_base.dart';
import 'eml_parser.dart';
import '../../i18n/app_strings.dart';

class FileCardInfo {
  final String fileName;
  final int fileSize;
  final String fileId;
  final int totalChunks;
  final int receivedChunks;
  final int transferStatus;

  FileCardInfo({
    this.fileName = '',
    this.fileSize = 0,
    this.fileId = '',
    this.totalChunks = 0,
    this.receivedChunks = 0,
    this.transferStatus = 0,
  });
}

class ThreadItem {
  final List<native.EmailMessage> emails;
  final bool isFileBatch;

  ThreadItem.single(native.EmailMessage email)
      : emails = [email],
        isFileBatch = false;
  ThreadItem.batch(this.emails) : isFileBatch = true;
}

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
    account: json['account'] ?? '',
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

  // Cache: email -> name from addressbook
  static Map<String, String>? _addressbookCache;

  String _addressbookNameFor(String email) {
    if (email.isEmpty) return '';
    final addr = _extractEmailAddress(email);
    if (addr.isEmpty) return '';
    _addressbookCache ??= _loadAddressbookCache();
    return _addressbookCache![addr] ?? '';
  }

  static String addressbookNameForEmail(String email) {
    if (email.isEmpty) return '';
    final lt = email.indexOf('<');
    final gt = email.indexOf('>');
    String addr;
    if (lt >= 0 && gt > lt) {
      addr = email.substring(lt + 1, gt).trim().toLowerCase();
    } else {
      addr = email.trim().toLowerCase();
    }
    if (addr.isEmpty) return '';
    _addressbookCache ??= _loadAddressbookCache();
    return _addressbookCache![addr] ?? '';
  }

  static Map<String, String> _loadAddressbookCache() {
    final map = <String, String>{};
    try {
      final jsonStr = native.EmailCore.addressbookQueryAll();
      if (jsonStr == null) return map;
      final list = jsonDecode(jsonStr) as List;
      for (final item in list) {
        final m = item as Map<String, dynamic>;
        final email = (m['email'] as String? ?? '').toLowerCase();
        final name = m['name'] as String? ?? '';
        if (email.isNotEmpty && name.isNotEmpty) {
          map[email] = name;
        }
      }
    } catch (_) {}
    return map;
  }

  static void refreshAddressbookCache() {
    _addressbookCache = null;
  }

  Widget buildAvatar(String name, {String? email}) {
    String displayName = name.isEmpty ? '?' : name[0];
    if (email != null && email.isNotEmpty) {
      final abName = _addressbookNameFor(email);
      if (abName.isNotEmpty) {
        displayName = abName[0];
      }
    }
    final bgColor = avatarColor(name);
    return Container(
      width: 36,
      height: 36,
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [bgColor, bgColor.withValues(alpha: 0.75)],
        ),
        borderRadius: BorderRadius.circular(10),
        boxShadow: [
          BoxShadow(
            color: bgColor.withValues(alpha: 0.3),
            blurRadius: 6,
            offset: const Offset(0, 2),
          ),
        ],
      ),
      child: Center(
        child: Text(displayName, style: const TextStyle(color: Colors.white, fontSize: 15, fontWeight: FontWeight.w600)),
      ),
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
    // Only show emails from INBOX folder, exclude truck (file chunk) messages
    final thread = emails
        .where((e) => e.sessionId == sessionId && e.folder == 'INBOX')
        .where((e) => !e.messageId.startsWith('<truck_'))
        .toList();

    // Sort by rowid (insertion order = chronological)
    thread.sort((a, b) {
      return a.rowid.compareTo(b.rowid);
    });

    return thread;
  }

  List<ThreadItem> buildMergedThread(String sessionId) {
    // Clear EML cache to ensure fresh file transfer status on each rebuild
    clearEmlCache();
    final thread = buildConversationThread(sessionId);
    final items = <ThreadItem>[];

    int i = 0;
    while (i < thread.length) {
      final email = thread[i];

      // Check if this is a file message by parsing its EML
      String? batchId;
      if (email.file.isNotEmpty) {
        final emlPath = '$emailDataPath/${email.account}/${email.file}.eml';
        final parsed = parseEmlFile(emlPath, account: email.account);
        if (parsed.isFileMessage && parsed.batchId.isNotEmpty) {
          batchId = parsed.batchId;
        }
      }

      if (batchId != null) {
        // Collect all consecutive file messages with the same batch_id
        final batchEmails = <native.EmailMessage>[email];
        int j = i + 1;
        while (j < thread.length) {
          final nextEmail = thread[j];
          if (nextEmail.file.isEmpty) break;
          final nextPath = '$emailDataPath/${nextEmail.account}/${nextEmail.file}.eml';
          final nextParsed = parseEmlFile(nextPath, account: nextEmail.account);
          if (nextParsed.isFileMessage && nextParsed.batchId == batchId) {
            batchEmails.add(nextEmail);
            j++;
          } else {
            break;
          }
        }
        items.add(ThreadItem.batch(batchEmails));
        i = j;
      } else {
        items.add(ThreadItem.single(email));
        i++;
      }
    }

    return items;
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
    final mergedThread = buildMergedThread(selectedConversationMessageId!);

    // Get title from thread root or from session_members
    String conversationTitle = AppStrings.noSubject;
    List<native.EmailMessage> effectiveThread = thread;
    if (thread.isNotEmpty) {
      final rootEmail = thread.firstWhere(
        (e) => e.sessionId == selectedConversationMessageId,
        orElse: () => thread.first,
      );
      conversationTitle = rootEmail.subject.isEmpty ? AppStrings.noSubject : rootEmail.subject;
    } else {
      conversationTitle = AppStrings.newConversation;
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
                                Text(AppStrings.sessionCreatedWaiting, style: TextStyle(fontSize: 14, color: Colors.grey[500])),
                              ],
                            ),
                          )
                        : ListView.builder(
                            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                            itemCount: mergedThread.length,
                            itemBuilder: (context, index) {
                              final item = mergedThread[index];
                              final firstEmail = item.emails.first;
                              final senderEmail = _extractEmailAddress(firstEmail.sender);
                              final isMe = senderEmail == firstEmail.recipient.toLowerCase();
                              return buildChatBubble(item, isMe);
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
                  Text(AppStrings.conversationMembers, style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500, color: Colors.grey[600])),
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
    final displayName = isMe ? AppStrings.me : name;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          buildAvatar(displayName, email: address),
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
                constraints: const BoxConstraints(maxHeight: 200),
                decoration: BoxDecoration(
                  color: Colors.white,
                  borderRadius: BorderRadius.circular(4),
                ),
                child: Container(
                  decoration: BoxDecoration(
                    borderRadius: BorderRadius.circular(4),
                    border: Border.all(
                      color: _isDragging ? const Color(0xFF07C160) : Colors.grey[300]!,
                      width: 1,
                    ),
                  ),
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(4),
                    child: SingleChildScrollView(
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Theme(
                        data: Theme.of(context).copyWith(
                          hoverColor: Colors.transparent,
                          highlightColor: Colors.transparent,
                        ),
                        child: TextField(
                          controller: replyController,
                          maxLines: 5,
                          minLines: 2,
                          decoration: InputDecoration(
                            hintText: AppStrings.sendMessageHint,
                            hintStyle: TextStyle(fontSize: 14, color: Colors.grey[400]),
                            border: InputBorder.none,
                            enabledBorder: InputBorder.none,
                            focusedBorder: InputBorder.none,
                            disabledBorder: InputBorder.none,
                            contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                            isDense: true,
                          ),
                        ),
                      ),
                      Padding(
                        padding: const EdgeInsets.only(left: 4, bottom: 4),
                        child: Align(
                          alignment: Alignment.centerLeft,
                          child: IconButton(
                            icon: Icon(Icons.folder_open, size: 20, color: Colors.grey[600]),
                            onPressed: () {},
                            constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                            padding: EdgeInsets.zero,
                          ),
                        ),
                      ),
                    ],
                  ),
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
            child: Text(AppStrings.send, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
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
      subject = selectedConversationMessageId ?? AppStrings.newConversation;
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
        SnackBar(content: Text(AppStrings.noRecipients), duration: const Duration(seconds: 2)),
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
        SnackBar(content: Text(AppStrings.cannotLoadConfig), duration: const Duration(seconds: 2)),
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
      SnackBar(content: Text(AppStrings.sending), duration: const Duration(seconds: 10)),
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

    // Insert send task into task table for async processing
    // If files are attached, use fileSplitAndSend which carries text in the file message
    // If no files, use regular taskInsert for plain text
    int taskId = 0;
    bool fileSendMode = droppedFiles.isNotEmpty;

    if (fileSendMode) {
      // Generate a batch_id shared by all files in this send
      final batchId = 'batch_${DateTime.now().millisecondsSinceEpoch}';
      // Send each file via fileSplitAndSend (text is embedded in the first file message)
      for (int i = 0; i < droppedFiles.length; i++) {
        final file = droppedFiles[i];
        // Only include text on the first file message
        final textForThisFile = (i == 0) ? bodyText : '';
        final resultJson = native.EmailCore.fileSplitAndSend(
          filePath: file.path,
          fileName: file.name,
          account: accountEmail,
          recipient: recipientStr,
          sessionId: sessionId ?? '',
          inReplyTo: inReplyTo,
          subject: subject,
          text: textForThisFile,
          batchId: batchId,
        );
        native.EmailCore.logWrite('[SEND] fileSplitAndSend result: $resultJson');
      }
      taskId = 1; // Indicate success
    } else {
      taskId = native.EmailCore.taskInsert(
        account: accountEmail,
        recipient: recipientStr,
        subject: subject,
        body: bodyText,
        inReplyTo: inReplyTo,
        messageId: replyMessageId,
        sessionId: sessionId ?? '',
        xSessionChart: native.XMailer.text,
      );
    }

    () async {
      bool ok = false;
      String errorMsg = '';
      try {
        // Yield to let UI update (show snackbar)
        await Future.delayed(const Duration(milliseconds: 50));
        native.EmailCore.logWrite('[SEND] Task inserted with id: $taskId');

        if (taskId > 0) {
          ok = true;
          native.EmailCore.logWrite('[SEND] Task queued successfully');
        } else {
          errorMsg = '${AppStrings.sendFailed} (task_id=$taskId)';
          native.EmailCore.logWrite('[SEND] Task insert failed: $taskId');
        }
      } catch (e) {
        errorMsg = '${AppStrings.exceptionLabel}: $e';
        native.EmailCore.logWrite('[SEND] Error: $errorMsg');
      }

      if (ok) {
        // Task queued successfully. Wait for email_sent notification from SendTask isolate
        native.EmailCore.logWrite('[SEND] Task queued, waiting for email_sent notification');
        // The actual email will be sent by SendTask isolate and notification will trigger reload
        if (!mounted) return;
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        replyController.clear();
        droppedFiles.clear();
        setState(() {});
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.messageSent), duration: const Duration(seconds: 1)),
        );
      } else {
        if (!mounted) return;
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(errorMsg), duration: const Duration(seconds: 3)),
        );
      }
    }();
  }

  List<FileCardInfo> _refreshFileCardStatuses(List<FileCardInfo> cards) {
    return cards.map((card) {
      if (card.fileId.isEmpty) return card;
      try {
        final statusJson = native.EmailCore.fileTransferQuery(card.fileId);
        native.EmailCore.logWrite('[FILE_STATUS] fileId=${card.fileId}, query=$statusJson');
        final decoded = jsonDecode(statusJson);
        if (decoded['status'] == 'success') {
          return FileCardInfo(
            fileName: card.fileName,
            fileSize: card.fileSize,
            fileId: card.fileId,
            totalChunks: card.totalChunks,
            receivedChunks: decoded['received_chunks'] as int? ?? card.receivedChunks,
            transferStatus: decoded['transfer_status'] as int? ?? card.transferStatus,
          );
        }
      } catch (e) {
        native.EmailCore.logWrite('[FILE_STATUS] error querying ${card.fileId}: $e');
      }
      return card;
    }).toList();
  }

  Widget buildChatBubble(ThreadItem item, bool isMe) {
    final firstEmail = item.emails.first;
    final isFileBatch = item.isFileBatch;

    // Hide individual file transfer messages (not batches) from conversation
    if (!isFileBatch && (
        firstEmail.messageId.startsWith('<file_') ||
        firstEmail.messageId.startsWith('<truck_'))) {
      return const SizedBox.shrink();
    }

    final displayName = extractName(firstEmail.sender);

    String bodyText = '';
    bool hasAtt = false;
    bool isDownloading = firstEmail.file.isEmpty;
    bool showDownloadingIndicator = false;
    String savedEmlPath = '';
    List<EmlAttachment> savedAttachments = [];

    List<FileCardInfo> fileCards = [];

    if (isFileBatch) {
      // Parse all emails in the batch to collect file cards
      String batchText = '';
      for (int i = 0; i < item.emails.length; i++) {
        final email = item.emails[i];
        if (email.file.isEmpty) continue;
        final emlPath = '$emailDataPath/${email.account}/${email.file}.eml';
        final parsed = parseEmlFile(emlPath, account: email.account);
        if (parsed.isFileMessage) {
          // Text comes from the first file message that has non-empty text
          if (batchText.isEmpty && parsed.textBody.isNotEmpty) {
            batchText = parsed.textBody;
          }
          fileCards.add(FileCardInfo(
            fileName: parsed.fileName,
            fileSize: parsed.fileSize,
            fileId: parsed.fileId,
            totalChunks: parsed.totalChunks,
            receivedChunks: parsed.receivedChunks,
            transferStatus: parsed.transferStatus,
          ));
        }
      }
      // Re-query fresh transfer status (bypass EML cache)
      fileCards = _refreshFileCardStatuses(fileCards);
      bodyText = batchText;
      // If any file is still downloading, show indicator
      showDownloadingIndicator = item.emails.any((e) => e.file.isEmpty || e.isLocal == 0);
    } else {
      final email = firstEmail;
      isDownloading = email.file.isEmpty;

      native.EmailCore.logWrite('[Conversation] email.uuid=${email.uuid}, email.file="${email.file}", email.isLocal=${email.isLocal}, email.recipient=${email.recipient}');

      bool isFileMessage = false;
      if (!isDownloading) {
        final emlPath = '$emailDataPath/${email.account}/${email.file}.eml';
        savedEmlPath = emlPath;
        final parsed = parseEmlFile(emlPath, account: email.account);
        bodyText = parsed.textBody;
        hasAtt = parsed.hasAttachments;
        isFileMessage = parsed.isFileMessage;
        savedAttachments = parsed.attachments;
        if (isFileMessage) {
          fileCards.add(FileCardInfo(
            fileName: parsed.fileName,
            fileSize: parsed.fileSize,
            fileId: parsed.fileId,
            totalChunks: parsed.totalChunks,
            receivedChunks: parsed.receivedChunks,
            transferStatus: parsed.transferStatus,
          ));
        }
      }
      // Re-query fresh transfer status (bypass EML cache)
      fileCards = _refreshFileCardStatuses(fileCards);

      if (!hasAtt && email.body.isNotEmpty) {
        hasAtt = hasAttachment(email.body);
      }

      // Fallback: if hasAtt is true (from bodystructure) but savedAttachments is empty
      // (because isDownloading was true or parser cleared attachments for encrypted messages),
      // try parsing the EML file directly if it exists on disk.
      if (hasAtt && savedAttachments.isEmpty && email.file.isNotEmpty) {
        final emlPath = '$emailDataPath/${email.account}/${email.file}.eml';
        final file = File(emlPath);
        if (file.existsSync()) {
          savedEmlPath = emlPath;
          final parsed = parseEmlFile(emlPath, account: email.account);
          savedAttachments = parsed.attachments;
          if (savedAttachments.isEmpty) {
            // Parser may have cleared attachments for encrypted messages.
            // Re-parse without account to bypass encrypted-body handling.
            final rawParsed = parseEmlFile(emlPath);
            savedAttachments = rawParsed.attachments;
          }
        }
      }

      showDownloadingIndicator = (email.isLocal == 0 && !isDownloading) || isDownloading;
    }

    const bubbleColorMe = Color(0xFF95EC69);
    const bubbleColorOther = Colors.white;
    final bubbleColor = isMe ? bubbleColorMe : bubbleColorOther;

    return Padding(
      padding: const EdgeInsets.only(bottom: 20),
      child: Row(
        mainAxisAlignment: isMe ? MainAxisAlignment.end : MainAxisAlignment.start,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (!isMe) ...[buildAvatar(displayName, email: firstEmail.sender), const SizedBox(width: 10)],
          Flexible(
            child: Column(
              crossAxisAlignment: isMe ? CrossAxisAlignment.end : CrossAxisAlignment.start,
              children: [
                if (!isMe)
                  Padding(
                    padding: const EdgeInsets.only(bottom: 5, left: 2),
                    child: Text(displayName, style: TextStyle(fontSize: 12, color: Colors.grey[500], fontWeight: FontWeight.w400)),
                  ),
                if (hasAtt) ...[
                  Padding(
                    padding: const EdgeInsets.only(bottom: 5, left: 2, right: 2),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(Icons.attach_file, size: 14, color: Colors.grey[400]),
                        const SizedBox(width: 3),
                        Text(AppStrings.attachment, style: TextStyle(fontSize: 11, color: Colors.grey[400])),
                      ],
                    ),
                  ),
                  if (savedEmlPath.isNotEmpty && savedAttachments.isNotEmpty)
                    ...List.generate(savedAttachments.length, (i) {
                      final att = savedAttachments[i];
                      return Padding(
                        padding: const EdgeInsets.only(bottom: 3, left: 2, right: 2),
                        child: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(Icons.insert_drive_file, size: 14, color: Colors.grey[500]),
                            const SizedBox(width: 4),
                            ConstrainedBox(
                              constraints: const BoxConstraints(maxWidth: 150),
                              child: Text(att.filename, style: TextStyle(fontSize: 11, color: Colors.grey[600]), overflow: TextOverflow.ellipsis),
                            ),
                            const SizedBox(width: 6),
                            TextButton.icon(
                              onPressed: () => _saveAttachment(savedEmlPath, i, att.filename),
                              icon: Icon(Icons.save_alt, size: 12),
                              label: Text(AppStrings.isZh ? '另存为' : 'Save As', style: TextStyle(fontSize: 11)),
                              style: TextButton.styleFrom(
                                padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                                minimumSize: const Size(0, 24),
                              ),
                            ),
                          ],
                        ),
                      );
                    }),
                ],
                _ChatBubble(
                  isMe: isMe,
                  color: bubbleColor,
                  maxWidth: MediaQuery.of(context).size.width * 0.5,
                  bodyText: bodyText,
                  showDownloadingIndicator: showDownloadingIndicator,
                  fileCards: fileCards,
                  onSaveFile: (context, fileId, fileName) => _saveFileTransfer(context, fileId, fileName, isMe),
                ),
                Padding(
                  padding: const EdgeInsets.only(top: 5, left: 2, right: 2),
                  child: Text(formatTimeShort(firstEmail.timestamp), style: TextStyle(fontSize: 10, color: Colors.grey[400], fontWeight: FontWeight.w300)),
                ),
              ],
            ),
          ),
          if (isMe) ...[const SizedBox(width: 10), buildAvatar(displayName, email: firstEmail.sender)],
        ],
      ),
    );
  }

  Future<void> _saveFileTransfer(BuildContext context, String fileId, String fileName, bool isMe) async {
    if (fileId.isEmpty) return;

    String? selectedDir = await FilePicker.getDirectoryPath(
      dialogTitle: AppStrings.isZh ? '选择保存位置' : 'Choose save location',
    );
    if (selectedDir == null) return;

    final result = isMe
        ? native.EmailCore.fileTransferCopyOriginal(fileId, selectedDir)
        : native.EmailCore.fileTransferReassemble(fileId, selectedDir);
    try {
      final decoded = jsonDecode(result);
      if (decoded['status'] == 'success') {
        final outputPath = decoded['output_path']?.toString() ?? '';
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.isZh ? '已保存到: $outputPath' : 'Saved to: $outputPath'), duration: const Duration(seconds: 2)),
        );
      } else {
        final error = decoded['error']?.toString() ?? 'unknown';
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.isZh ? '保存失败: $error' : 'Save failed: $error'), duration: const Duration(seconds: 2)),
        );
      }
    } catch (_) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.isZh ? '保存失败' : 'Save failed'), duration: const Duration(seconds: 2)),
      );
    }
  }

  Future<void> _saveAttachment(String emlPath, int index, String filename) async {
    Directory dir;
    try {
      dir = await getApplicationDocumentsDirectory();
    } catch (_) {
      dir = Directory('${Platform.environment['HOME']}/Documents');
    }
    final saveDir = Directory('${dir.path}/Attachments');
    if (!saveDir.existsSync()) saveDir.createSync(recursive: true);

    final outputPath = '${saveDir.path}/$filename';
    final result = native.EmailCore.saveAttachment(emlPath, index, outputPath);
    if (result == 0) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.isZh ? '已保存到: $outputPath' : 'Saved to: $outputPath'), duration: const Duration(seconds: 2)),
      );
    } else {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.isZh ? '保存失败 (code: $result)' : 'Save failed (code: $result)'), duration: const Duration(seconds: 2)),
      );
    }
  }
}

class _ChatBubble extends StatelessWidget {
  final bool isMe;
  final Color color;
  final double maxWidth;
  final String bodyText;
  final bool showDownloadingIndicator;
  final List<FileCardInfo> fileCards;
  final void Function(BuildContext, String, String)? onSaveFile;

  const _ChatBubble({
    required this.isMe,
    required this.color,
    required this.maxWidth,
    required this.bodyText,
    required this.showDownloadingIndicator,
    this.fileCards = const [],
    this.onSaveFile,
  });

  @override
  Widget build(BuildContext context) {
    const tailWidth = 8.0;
    const tailHeight = 16.0;
    const radius = 8.0;
    const padH = 14.0;
    const padV = 11.0;

    return LimitedBox(
      maxWidth: maxWidth + tailWidth + padH * 2,
      child: CustomPaint(
        painter: _BubbleShapePainter(
          color: color,
          isMe: isMe,
          tailWidth: tailWidth,
          tailHeight: tailHeight,
          radius: radius,
        ),
        child: Padding(
          padding: EdgeInsets.only(
            left: isMe ? padH : padH + tailWidth,
            right: isMe ? padH + tailWidth : padH,
            top: padV,
            bottom: padV,
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              if (bodyText.isNotEmpty)
                SelectableText(
                  bodyText,
                  style: TextStyle(
                    fontSize: 14,
                    height: 1.5,
                    color: isMe ? const Color(0xFF1A1A1A) : const Color(0xFF333333),
                  ),
                ),
              for (final card in fileCards)
                _buildFileCard(context, card, isMe),
              if (!isMe && showDownloadingIndicator)
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
                      Text(AppStrings.downloading, style: TextStyle(fontSize: 13, color: Colors.grey[400])),
                    ],
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildFileCard(BuildContext context, FileCardInfo card, bool isMe) {
    final fileName = card.fileName;
    final fileSize = card.fileSize;
    final totalChunks = card.totalChunks;
    final receivedChunks = card.receivedChunks;
    final transferStatus = card.transferStatus;

    final ext = fileName.split('.').last.toLowerCase();
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

    String sizeStr;
    if (fileSize >= 1024 * 1024) {
      sizeStr = '${(fileSize / (1024 * 1024)).toStringAsFixed(1)} MB';
    } else if (fileSize >= 1024) {
      sizeStr = '${(fileSize / 1024).toStringAsFixed(1)} KB';
    } else {
      sizeStr = '$fileSize B';
    }

    final bool isComplete = isMe
        ? (transferStatus == 1)
        : (totalChunks > 0 && receivedChunks >= totalChunks);
    final bool isFailed = transferStatus == 2;
    final double progress = totalChunks > 0 ? receivedChunks / totalChunks : 0.0;

    return Container(
      margin: const EdgeInsets.only(top: 8),
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: const Color(0xFFF5F5F5),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: Colors.grey[300]!, width: 0.5),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(fileIcon, size: 28, color: iconColor),
              const SizedBox(width: 8),
              Flexible(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(
                      fileName,
                      style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                    ),
                    Text(
                      sizeStr,
                      style: TextStyle(fontSize: 11, color: Colors.grey[500]),
                    ),
                  ],
                ),
              ),
            ],
          ),
          if (!isMe && !isComplete && !isFailed && totalChunks > 0) ...[
            const SizedBox(height: 6),
            ClipRRect(
              borderRadius: BorderRadius.circular(3),
              child: LinearProgressIndicator(
                value: progress,
                backgroundColor: Colors.grey[300],
                valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF07C160)),
                minHeight: 4,
              ),
            ),
            const SizedBox(height: 2),
            Text(
              '$receivedChunks / $totalChunks',
              style: TextStyle(fontSize: 10, color: Colors.grey[500]),
            ),
          ],
          if (!isMe && isComplete)
            Padding(
              padding: const EdgeInsets.only(top: 4),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.check_circle, size: 14, color: Colors.green[600]),
                  const SizedBox(width: 4),
                  Text('Received', style: TextStyle(fontSize: 11, color: Colors.green[600])),
                  const SizedBox(width: 8),
                  TextButton.icon(
                    onPressed: () => onSaveFile?.call(context, card.fileId, card.fileName),
                    icon: Icon(Icons.save_alt, size: 14),
                    label: Text(AppStrings.isZh ? '另存为' : 'Save As', style: TextStyle(fontSize: 11)),
                    style: TextButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                      minimumSize: const Size(0, 24),
                    ),
                  ),
                ],
              ),
            ),
          if (!isMe && isFailed)
            Padding(
              padding: const EdgeInsets.only(top: 4),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.error_outline, size: 14, color: Colors.red[600]),
                  const SizedBox(width: 4),
                  Text('Failed', style: TextStyle(fontSize: 11, color: Colors.red[600])),
                ],
              ),
            ),
          if (isMe && !isComplete && !isFailed)
            Padding(
              padding: const EdgeInsets.only(top: 4),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  SizedBox(
                    width: 12,
                    height: 12,
                    child: CircularProgressIndicator(
                      strokeWidth: 2,
                      color: Colors.grey[500],
                    ),
                  ),
                  const SizedBox(width: 6),
                  Text('Sending...', style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                ],
              ),
            ),
          if (isMe && isComplete)
            Padding(
              padding: const EdgeInsets.only(top: 4),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(Icons.check_circle, size: 14, color: Colors.green[600]),
                  const SizedBox(width: 4),
                  Text('Sent', style: TextStyle(fontSize: 11, color: Colors.green[600])),
                  const SizedBox(width: 8),
                  TextButton.icon(
                    onPressed: () => onSaveFile?.call(context, card.fileId, card.fileName),
                    icon: Icon(Icons.save_alt, size: 14),
                    label: Text(AppStrings.isZh ? '另存为' : 'Save As', style: TextStyle(fontSize: 11)),
                    style: TextButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                      minimumSize: const Size(0, 24),
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}

class _BubbleShapePainter extends CustomPainter {
  final Color color;
  final bool isMe;
  final double tailWidth;
  final double tailHeight;
  final double radius;

  _BubbleShapePainter({
    required this.color,
    required this.isMe,
    required this.tailWidth,
    required this.tailHeight,
    required this.radius,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.fill
      ..isAntiAlias = true;

    final shadowPaint = Paint()
      ..color = Colors.black.withValues(alpha: 0.08)
      ..style = PaintingStyle.fill
      ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 5);

    final w = size.width;
    final h = size.height;
    final r = radius;
    final tw = tailWidth;
    final th = tailHeight;

    // The bubble rect (accounting for tail space on one side)
    final bubbleLeft = isMe ? tw : 0.0;
    final bubbleRight = isMe ? w : w - tw;
    final bubbleTop = 0.0;
    final bubbleBottom = h;

    // Build the path: rounded rect + triangular tail
    final path = Path();

    if (isMe) {
      // Tail at top-right corner, smooth
      path.moveTo(bubbleLeft + r, bubbleTop);
      path.lineTo(bubbleRight - r - th * 0.6, bubbleTop);
      // Smooth tail curving out to the right and back
      path.quadraticBezierTo(bubbleRight + tw * 0.3, bubbleTop, bubbleRight + tw, bubbleTop + 3);
      path.quadraticBezierTo(bubbleRight + tw * 0.4, bubbleTop + r + 4, bubbleRight, bubbleTop + r + 6);
      path.lineTo(bubbleRight, bubbleBottom - r);
      path.quadraticBezierTo(bubbleRight, bubbleBottom, bubbleRight - r, bubbleBottom);
      path.lineTo(bubbleLeft + r, bubbleBottom);
      path.quadraticBezierTo(bubbleLeft, bubbleBottom, bubbleLeft, bubbleBottom - r);
      path.lineTo(bubbleLeft, bubbleTop + r);
      path.quadraticBezierTo(bubbleLeft, bubbleTop, bubbleLeft + r, bubbleTop);
    } else {
      // Tail at top-left corner, smooth
      path.moveTo(bubbleLeft + r, bubbleTop);
      path.lineTo(bubbleRight - r, bubbleTop);
      path.quadraticBezierTo(bubbleRight, bubbleTop, bubbleRight, bubbleTop + r);
      path.lineTo(bubbleRight, bubbleBottom - r);
      path.quadraticBezierTo(bubbleRight, bubbleBottom, bubbleRight - r, bubbleBottom);
      path.lineTo(bubbleLeft + r, bubbleBottom);
      path.quadraticBezierTo(bubbleLeft, bubbleBottom, bubbleLeft, bubbleBottom - r);
      path.lineTo(bubbleLeft, bubbleTop + r + 6);
      // Smooth tail curving out to the left and back
      path.quadraticBezierTo(bubbleLeft - tw * 0.4, bubbleTop + r + 4, bubbleLeft - tw, bubbleTop + 3);
      path.quadraticBezierTo(bubbleLeft - tw * 0.3, bubbleTop, bubbleLeft + r, bubbleTop);
    }
    path.close();

    // Draw shadow first (slightly offset)
    canvas.save();
    canvas.translate(0, 2);
    canvas.drawPath(path, shadowPaint);
    canvas.restore();

    // Draw the bubble
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant _BubbleShapePainter oldDelegate) =>
      color != oldDelegate.color || isMe != oldDelegate.isMe;
}
