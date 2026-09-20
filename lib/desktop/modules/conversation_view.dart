import 'dart:io';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:emoji_picker_flutter/emoji_picker_flutter.dart';
import 'package:desktop_drop/desktop_drop.dart';
import 'package:file_picker/file_picker.dart';
import '../../native/email_core.dart' as native;
import '../../native/unified_session.dart';
import 'email_utils.dart';
import 'email_module_base.dart';
import 'eml_parser.dart';
import '../../i18n/app_strings.dart';

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
  bool get showEmojiPicker;
  set showEmojiPicker(bool v);
  TextEditingController get replyController;
  String get configPath;
  String get emailDataPath;

  String _extractEmailAddress(String sender) {
    final lt = sender.indexOf('<');
    final gt = sender.indexOf('>');
    if (lt >= 0 && gt > lt) {
      return sender.substring(lt + 1, gt).trim().toLowerCase();
    }
    return sender.trim().toLowerCase();
  }

  List<DroppedFile> get droppedFiles;
  bool _isDragging = false;

  // Cached readiness of the currently selected unified session.
  bool _sessionReady = false;

  /// Re-evaluate whether the selected session has completed key exchange.
  void refreshSessionReady() {
    final sid = selectedUnifiedSessionId;
    if (sid == null) {
      _sessionReady = false;
      return;
    }
    final session = unifiedSessions.where((s) => s.sessionId == sid).firstOrNull;
    if (session == null) {
      _sessionReady = false;
      return;
    }
    _sessionReady = UnifiedSessionManager.isReady(session.account, session.sessionId);
  }

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


  // ---------------------------------------------------------------------------
  // Unified Conversation View (replaces both buildConversationDetail and
  // the old buildGroupConversationView)
  // ---------------------------------------------------------------------------

  // These are provided by EmailModuleState
  List<UnifiedSessionInfo> get unifiedSessions;
  String? get selectedUnifiedSessionId;
  set selectedUnifiedSessionId(String? v);
  bool get showUnifiedMembers;
  set showUnifiedMembers(bool v);
  List<native.EmailMessage> get unifiedMessages;
  List<native.EmailMessage> get pendingUnifiedMessages;
  void addPendingUnifiedMessage(native.EmailMessage msg);
  void loadUnifiedSessions();
  void loadUnifiedSessionMessages(String unifiedSessionId);

  Widget buildUnifiedConversationView() {
    // Find the current unified session
    UnifiedSessionInfo? usSession;
    if (selectedUnifiedSessionId != null) {
      for (final s in unifiedSessions) {
        if (s.sessionId == selectedUnifiedSessionId) {
          usSession = s;
          break;
        }
      }
    }
    if (usSession == null) {
      return Expanded(child: Center(child: Text('Session not found', style: TextStyle(color: Colors.grey[400]))));
    }
    final session = usSession; // promote to non-nullable

    final messages = [
      ...unifiedMessages,
      ...pendingUnifiedMessages.where((p) => p.sessionId == session.sessionId),
    ];
    final members = session.members;
    final isGroup = members.length > 2;
    final title = session.subject.isNotEmpty ? session.subject : members.where((m) => m != session.account).take(3).join(', ');

    // Build the conversation area
    final conversation = Container(
      color: Colors.white,
      child: Column(
        children: [
          // Header
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            decoration: BoxDecoration(
              color: Colors.grey[50],
              border: Border(bottom: BorderSide(color: Colors.grey[200]!)),
            ),
            child: Row(
              children: [
                Icon(isGroup ? Icons.group : Icons.person,
                    color: isGroup ? Colors.green[700] : Colors.blue[700], size: 24),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(title.isEmpty ? AppStrings.noSubject : title,
                          style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500),
                          maxLines: 1, overflow: TextOverflow.ellipsis),
                      Text('${members.length}${AppStrings.isZh ? "人" : " members"}',
                          style: TextStyle(fontSize: 12, color: Colors.grey[500])),
                    ],
                  ),
                ),
                IconButton(
                  icon: Icon(Icons.more_horiz, size: 22, color: Colors.grey[700]),
                  onPressed: () => setState(() => showUnifiedMembers = true),
                ),
              ],
            ),
          ),
          // Messages
          Expanded(
            child: messages.isEmpty
                ? Center(child: Text(AppStrings.sessionCreatedWaiting, style: TextStyle(color: Colors.grey[400])))
                : ListView.builder(
                    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                    itemCount: messages.length,
                    itemBuilder: (context, index) => _buildUnifiedMessageBubble(messages[index], members, usSession!.account),
                  ),
          ),
          // Input bar
          _buildUnifiedInputBar(usSession),
        ],
      ),
    );

    // Member panel (slides in from right)
    final memberPanel = Container(
      width: 240,
      decoration: BoxDecoration(
        color: const Color(0xFFF7F7F7),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.15), blurRadius: 12, offset: const Offset(-2, 0))],
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: Row(
              children: [
                Text(AppStrings.conversationMembers, style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500, color: Colors.grey[600])),
                const SizedBox(width: 8),
                Text('(${members.length})', style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                const Spacer(),
                IconButton(
                  icon: Icon(Icons.close, size: 20, color: Colors.grey[600]),
                  onPressed: () => setState(() => showUnifiedMembers = false),
                ),
              ],
            ),
          ),
          Expanded(
            child: ListView(
              padding: const EdgeInsets.symmetric(vertical: 4, horizontal: 8),
              children: members.map((m) {
                final isMe = m.toLowerCase() == session.account.toLowerCase();
                final displayName = isMe ? AppStrings.me : m.split('@').first;
                return Padding(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  child: Row(
                    children: [
                      CircleAvatar(
                        radius: 16,
                        backgroundColor: isMe ? Colors.green[100] : Colors.blue[100],
                        child: Text(m.isNotEmpty ? m[0].toUpperCase() : '?',
                            style: TextStyle(fontSize: 12, color: isMe ? Colors.green[700] : Colors.blue[700])),
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(displayName, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500), maxLines: 1, overflow: TextOverflow.ellipsis),
                            Text(m, style: TextStyle(fontSize: 11, color: Colors.grey[500]), maxLines: 1, overflow: TextOverflow.ellipsis),
                          ],
                        ),
                      ),
                    ],
                  ),
                );
              }).toList(),
            ),
          ),
          // Add member button
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: SizedBox(
              width: double.infinity,
              child: TextButton.icon(
                onPressed: () => _unifiedAddMember(session),
                icon: Icon(Icons.person_add, size: 18, color: Colors.green[700]),
                label: Text(AppStrings.isZh ? '添加成员' : 'Add member', style: TextStyle(fontSize: 13, color: Colors.green[700])),
                style: TextButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 8), shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6))),
              ),
            ),
          ),
          // 置顶 / 隐藏会话 开关（持久化到 DB，切换后刷新列表分组）
          Padding(
            padding: const EdgeInsets.fromLTRB(4, 0, 4, 8),
            child: Column(
              children: [
                SwitchListTile(
                  dense: true,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 8),
                  title: Text(AppStrings.pinSession, style: const TextStyle(fontSize: 13)),
                  value: session.pinned,
                  onChanged: (v) {
                    UnifiedSessionManager.setSessionFlags(session.sessionId, v, session.hidden);
                    loadUnifiedSessions();
                  },
                ),
                SwitchListTile(
                  dense: true,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 8),
                  title: Text(AppStrings.hideSession, style: const TextStyle(fontSize: 13)),
                  value: session.hidden,
                  onChanged: (v) {
                    UnifiedSessionManager.setSessionFlags(session.sessionId, session.pinned, v);
                    loadUnifiedSessions();
                  },
                ),
              ],
            ),
          ),
        ],
      ),
    );

    return Stack(
      children: [
        conversation,
        Positioned(
          top: 0,
          right: 0,
          bottom: 0,
          child: AnimatedSlide(
            duration: const Duration(milliseconds: 200),
            curve: Curves.easeOut,
            offset: showUnifiedMembers ? Offset.zero : const Offset(1, 0),
            child: memberPanel,
          ),
        ),
      ],
    );
  }

  Widget _buildUnifiedMessageBubble(native.EmailMessage msg, List<String> members, String myAccount) {
    final isMe = msg.isSent == 1;
    bool isHandshake = native.XMailer.isKeyExchange(msg.xMailer);
    String displayBody = '';
    EmlParsedContent? fileMeta;
    if (msg.file.isNotEmpty) {
      final emlPath = '$emailDataPath/${msg.account}/${msg.file}.eml';
      final parsed = parseEmlFile(emlPath, account: msg.account, sessionId: msg.sessionId, fromAddr: msg.sender, xMailer: msg.xMailer, isSent: msg.isSent);
      isHandshake = isHandshake || parsed.isHandshakeMessage;
      if (parsed.isFileMessage) fileMeta = parsed;
      displayBody = parsed.textBody;
    } else if (msg.body.isNotEmpty) {
      // Message body is directly available from localemail table (no EML file needed)
      displayBody = msg.body;
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
      child: Row(
        mainAxisAlignment: isMe ? MainAxisAlignment.end : MainAxisAlignment.start,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (!isMe) ...[
            CircleAvatar(radius: 16, backgroundColor: Colors.blue[100],
                child: Text(msg.sender.isNotEmpty ? msg.sender[0].toUpperCase() : '?', style: TextStyle(color: Colors.blue[700], fontSize: 11))),
            const SizedBox(width: 8),
          ],
          Flexible(
            child: Column(
              crossAxisAlignment: isMe ? CrossAxisAlignment.end : CrossAxisAlignment.start,
              children: [
                if (!isMe && members.length > 2)
                  Text(msg.sender.split('@').first, style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: BoxDecoration(
                    color: isMe ? const Color(0xFF95EC69) : Colors.white,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.grey[300]!),
                  ),
                  child: isHandshake
                      ? const Text('🤝', style: TextStyle(fontSize: 20))
                      : (fileMeta != null
                          ? _buildFileCard(fileMeta, isMe, displayBody)
                          : SelectableText(displayBody, style: const TextStyle(fontSize: 14))),
                ),
                const SizedBox(height: 2),
                Text(
                  msg.uuid.startsWith('pending_')
                      ? (AppStrings.isZh ? '发送中…' : 'Sending…')
                      : msg.timestamp,
                  style: TextStyle(fontSize: 11,
                      color: msg.uuid.startsWith('pending_') ? Colors.orange[400] : Colors.grey[400]),
                ),
              ],
            ),
          ),
          if (isMe) ...[
            const SizedBox(width: 8),
            CircleAvatar(radius: 16, backgroundColor: Colors.green[100],
                child: Text(AppStrings.me[0], style: TextStyle(color: Colors.green[700], fontSize: 11))),
          ],
        ],
      ),
    );
  }

  static String _formatBytes(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    if (bytes < 1024 * 1024 * 1024) return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(2)} GB';
  }

  Widget _buildFileCard(EmlParsedContent meta, bool isMe, String caption) {
    final sizeStr = _formatBytes(meta.fileSize);
    final progress = meta.totalChunks > 0 ? meta.receivedChunks / meta.totalChunks : 0.0;
    final done = meta.transferStatus == 1;
    final failed = meta.transferStatus == 2;
    return GestureDetector(
      onTap: done || isMe ? () => _saveUnifiedFile(meta, isMe) : null,
      child: ConstrainedBox(
        constraints: const BoxConstraints(minWidth: 180, maxWidth: 260),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              Icon(failed ? Icons.error_outline : Icons.insert_drive_file,
                  size: 28, color: failed ? Colors.red : Colors.blueGrey),
              const SizedBox(width: 8),
              Expanded(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text(meta.fileName, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500),
                      overflow: TextOverflow.ellipsis),
                  Text(sizeStr, style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                ]),
              ),
              if (done || isMe) Icon(Icons.download, size: 16, color: Colors.grey[600]),
            ]),
            const SizedBox(height: 6),
            LinearProgressIndicator(
              value: progress,
              minHeight: 3,
              backgroundColor: Colors.grey[300],
              valueColor: AlwaysStoppedAnimation<Color>(failed ? Colors.red : Colors.blue),
            ),
            const SizedBox(height: 2),
            Text(
              failed ? '传输失败' : done ? '已完成 ${meta.receivedChunks}/${meta.totalChunks}' : '${meta.receivedChunks}/${meta.totalChunks}',
              style: TextStyle(fontSize: 10, color: Colors.grey[500]),
            ),
            if (caption.isNotEmpty)
              Padding(
                padding: const EdgeInsets.only(top: 6),
                child: SelectableText(caption, style: const TextStyle(fontSize: 14)),
              ),
          ],
        ),
      ),
    );
  }

  Future<void> _saveUnifiedFile(EmlParsedContent meta, bool isMe) async {
    try {
      final dir = await FilePicker.getDirectoryPath(dialogTitle: '保存文件');
      if (dir == null || dir.isEmpty) return;
      final resultJson = isMe
          ? native.EmailCore.fileTransferCopyOriginal(meta.fileId, dir)
          : native.EmailCore.fileTransferReassemble(meta.fileId, dir);
      final result = jsonDecode(resultJson);
      final ok = result['status'] == 'success';
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(ok ? '已保存到 ${result['output_path'] ?? dir}' : '保存失败: ${result['error'] ?? resultJson}'),
        duration: const Duration(seconds: 3),
      ));
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('保存失败: $e')));
    }
  }

  Widget _buildUnifiedInputBar(UnifiedSessionInfo session) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: const Color(0xFFF7F7F7),
        border: Border(top: BorderSide(color: Colors.grey[300]!, width: 0.5)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
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
                            '￼' +
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
                                  onTap: () {
                                    if (showEmojiPicker) {
                                      setState(() => showEmojiPicker = false);
                                    }
                                  },
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
                                  child: Row(
                                    mainAxisSize: MainAxisSize.min,
                                    children: [
                                      IconButton(
                                        icon: Icon(Icons.folder_open, size: 20, color: Colors.grey[600]),
                                        onPressed: () {},
                                        constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                                        padding: EdgeInsets.zero,
                                      ),
                                      IconButton(
                                        icon: Icon(
                                          showEmojiPicker ? Icons.emoji_emotions : Icons.emoji_emotions_outlined,
                                          size: 20,
                                          color: showEmojiPicker ? const Color(0xFF07C160) : Colors.grey[600],
                                        ),
                                        onPressed: () {
                                          setState(() {
                                            showEmojiPicker = !showEmojiPicker;
                                          });
                                        },
                                        constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                                        padding: EdgeInsets.zero,
                                      ),
                                    ],
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
                  backgroundColor: _sessionReady ? const Color(0xFF07C160) : Colors.grey[300],
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(4)),
                ),
                onPressed: _sessionReady ? () {
                  if (showEmojiPicker) {
                    setState(() => showEmojiPicker = false);
                  }
                  _unifiedSendMessage(session);
                } : null,
                child: Text(AppStrings.send, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
              ),
            ],
          ),
          if (showEmojiPicker)
            SizedBox(
              height: 250,
              child: EmojiPicker(
                onEmojiSelected: (category, emoji) {
                  final text = replyController.text;
                  final selection = replyController.selection;
                  final cursorPos = selection.baseOffset < 0 ? text.length : selection.baseOffset;
                  final newText = text.substring(0, cursorPos) + emoji.emoji + text.substring(cursorPos);
                  replyController.value = TextEditingValue(
                    text: newText,
                    selection: TextSelection.collapsed(offset: cursorPos + emoji.emoji.length),
                  );
                },
                config: Config(
                  height: 250,
                  checkPlatformCompatibility: true,
                  emojiViewConfig: EmojiViewConfig(
                    emojiSizeMax: 28,
                    backgroundColor: Colors.white,
                  ),
                  categoryViewConfig: CategoryViewConfig(
                    indicatorColor: const Color(0xFF07C160),
                    iconColorSelected: const Color(0xFF07C160),
                    backgroundColor: Colors.white,
                  ),
                  searchViewConfig: SearchViewConfig(
                    backgroundColor: Colors.white,
                    buttonIconColor: Colors.grey[600] ?? Colors.grey,
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }

  void _unifiedSendMessage(UnifiedSessionInfo session) {
    final bodyText = replyController.text.replaceAll('￼', '').trim();
    final hasFiles = droppedFiles.isNotEmpty;
    if (bodyText.isEmpty && !hasFiles) return;

    // Gate: key exchange must be completed before sending.
    if (!UnifiedSessionManager.isReady(session.account, session.sessionId)) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(AppStrings.isZh
              ? '密钥交换尚未完成，无法发送消息'
              : 'Key exchange in progress, cannot send yet'),
          duration: const Duration(seconds: 2),
        ),
      );
      return;
    }

    final isSignal = session.mode == 'signal';

    // File transfer: 1:1 sessions use Signal attach messages (1.0.4/1.0.5);
    // group sessions use MLS attach messages (2.0.4/2.0.5). Strict separation.
    if (hasFiles && !isSignal) {
      final chain = unifiedMessages.where((m) => m.messageId.isNotEmpty).toList();
      final inReplyTo = chain.isNotEmpty ? chain.last.messageId : '';
      final batchId = 'batch_${DateTime.now().millisecondsSinceEpoch}';
      bool allOk = true;
      for (int i = 0; i < droppedFiles.length; i++) {
        final file = droppedFiles[i];
        final textForThisFile = (i == 0) ? bodyText : '';
        final resultJson = native.EmailCore.groupSendFile(
          session.account,
          session.sessionId,
          file.path,
          file.name,
          inReplyTo,
          session.subject,
          textForThisFile,
          batchId,
        );
        native.EmailCore.logWrite('[Unified] groupSendFile result: $resultJson');
        try {
          final r = jsonDecode(resultJson);
          if (r['status'] != 'success') allOk = false;
        } catch (_) {
          allOk = false;
        }
      }
      if (allOk) {
        replyController.clear();
        droppedFiles.clear();
        setState(() {});
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('消息已发送'), duration: Duration(seconds: 1)));
        loadUnifiedSessionMessages(session.sessionId);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('文件发送失败'), duration: Duration(seconds: 3)));
      }
      return;
    }

    // Find the in_reply_to (last message's message_id)
    final chain = unifiedMessages.where((m) => m.messageId.isNotEmpty).toList();
    final inReplyTo = chain.isNotEmpty ? chain.last.messageId : '';

    if (hasFiles) {
      // Signal 1:1 file send: queue attach tasks via fileSplitAndSend. Chunks are
      // compressed+split now and Double-Ratchet-encrypted by the task processor
      // right before each send; sessionId is the unified session id.
      final recipients = session.members
          .where((m) => m.toLowerCase() != session.account.toLowerCase())
          .join(', ');
      final batchId = 'batch_${DateTime.now().millisecondsSinceEpoch}';
      bool allOk = true;
      for (int i = 0; i < droppedFiles.length; i++) {
        final file = droppedFiles[i];
        final textForThisFile = (i == 0) ? bodyText : '';
        final resultJson = native.EmailCore.fileSplitAndSend(
          filePath: file.path,
          fileName: file.name,
          account: session.account,
          recipient: recipients,
          sessionId: session.sessionId,
          inReplyTo: inReplyTo,
          subject: session.subject,
          text: textForThisFile,
          batchId: batchId,
        );
        native.EmailCore.logWrite('[Unified] fileSplitAndSend result: $resultJson');
        try {
          final r = jsonDecode(resultJson);
          if (r['status'] != 'success') allOk = false;
        } catch (_) {
          allOk = false;
        }
      }
      if (allOk) {
        replyController.clear();
        droppedFiles.clear();
        setState(() {});
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('消息已发送'), duration: Duration(seconds: 1)));
        loadUnifiedSessionMessages(session.sessionId);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('文件发送失败'), duration: Duration(seconds: 3)));
      }
      return;
    }

    final resultJson = UnifiedSessionManager.sendMessage(session.account, session.sessionId, bodyText, inReplyTo);
    native.EmailCore.logWrite('[Unified] sendMessage result: $resultJson');

    try {
      final result = jsonDecode(resultJson);
      if (result['status'] == 'success') {
        // Both Signal and MLS app messages are now delivered through the outbox:
        // the per-account background loop sends them with pacing and retry.
        // SESSION_INIT-style creation flows return task_id=0 and must fail here.
        final taskId = result['task_id'];
        final messageId = result['message_id']?.toString() ?? '';
        final xMailer = result['x_mailer']?.toString() ?? '';
        final enqueued = taskId is int && taskId > 0;
        if (!enqueued) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('发送失败: 入队失败'), duration: Duration(seconds: 3)));
          return;
        }

        replyController.clear();

        // Optimistic "sending..." bubble: dropped once the real message lands in
        // localemail (background loop refreshes after send) or the task failed.
        final peer = session.members
            .where((m) => m.toLowerCase() != session.account.toLowerCase())
            .join(', ');
        addPendingUnifiedMessage(native.EmailMessage(
          sender: session.account,
          recipient: peer,
          subject: session.subject,
          body: bodyText,
          timestamp: DateTime.now().toString().substring(0, 19),
          uuid: 'pending_$messageId',
          inReplyTo: inReplyTo,
          messageId: messageId,
          sessionId: session.sessionId,
          account: session.account,
          xMailer: xMailer,
          isSent: 1,
        ));

        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('已进入发送队列'), duration: Duration(seconds: 1)));
        loadUnifiedSessionMessages(session.sessionId);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('发送失败: ${result['error'] ?? 'unknown'}'), duration: const Duration(seconds: 3)));
      }
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('发送失败: $e'), duration: const Duration(seconds: 3)));
    }
  }

  void _unifiedAddMember(UnifiedSessionInfo session) {
    final accounts = _configAccounts();
    final existingMembers = session.members.toSet();
    TextEditingController controller = TextEditingController();
    String newMember = '';

    showDialog(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (ctx, setDialogState) {
          return AlertDialog(
            title: Text(AppStrings.isZh ? '添加成员' : 'Add member'),
            content: SizedBox(
              width: 360,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('${AppStrings.isZh ? "当前成员" : "Current members"}: ${session.members.length}', style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                  const SizedBox(height: 12),
                  Autocomplete<String>(
                    fieldViewBuilder: (context, ctrl, focusNode, onFieldSubmitted) {
                      controller = ctrl;
                      return TextField(
                        controller: ctrl,
                        focusNode: focusNode,
                        decoration: const InputDecoration(hintText: '输入新成员邮箱', border: OutlineInputBorder(), isDense: true),
                        onSubmitted: (_) => onFieldSubmitted(),
                      );
                    },
                    optionsBuilder: (TextEditingValue v) {
                      if (v.text.isEmpty) return accounts.where((a) => !existingMembers.contains(a));
                      final input = v.text.toLowerCase();
                      return accounts.where((a) => a.toLowerCase().contains(input) && !existingMembers.contains(a));
                    },
                    onSelected: (String s) => setDialogState(() => newMember = s),
                    optionsViewBuilder: (context, onSelected, options) {
                      return Align(
                        alignment: Alignment.topLeft,
                        child: Material(
                          elevation: 4, borderRadius: BorderRadius.circular(6),
                          child: ConstrainedBox(
                            constraints: const BoxConstraints(maxHeight: 200),
                            child: ListView.builder(
                              shrinkWrap: true, itemCount: options.length,
                              itemBuilder: (context, index) {
                                final opt = options.elementAt(index);
                                return ListTile(dense: true, title: Text(opt, style: const TextStyle(fontSize: 13)), onTap: () => onSelected(opt));
                              },
                            ),
                          ),
                        ),
                      );
                    },
                  ),
                ],
              ),
            ),
            actions: [
              TextButton(onPressed: () => Navigator.of(dialogContext).pop(), child: Text(AppStrings.cancel)),
              ElevatedButton(
                style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF07C160)),
                onPressed: () {
                  final member = controller.text.trim().isNotEmpty ? controller.text.trim() : newMember;
                  if (member.isEmpty) return;
                  Navigator.of(dialogContext).pop();
                  _doUnifiedAddMember(session, member);
                },
                child: Text(AppStrings.isZh ? '添加' : 'Add', style: const TextStyle(color: Colors.white)),
              ),
            ],
          );
        },
      ),
    );
  }

  void _doUnifiedAddMember(UnifiedSessionInfo session, String newMemberEmail) {
    // x_reply_to for the upgrade invite = last message's x-message-id in this conversation
    final chain = sortByReplyChain(unifiedMessages);
    final inReplyTo = chain.isNotEmpty ? chain.last.messageId : '';
    final resultJson = UnifiedSessionManager.addMembers(
        session.account, session.sessionId, [newMemberEmail], inReplyTo: inReplyTo);
    native.EmailCore.logWrite('[Unified] addMembers result: $resultJson');

    try {
      final result = jsonDecode(resultJson);
      if (result['status'] == 'success') {
        final upgraded = result['upgraded'] == true;

        // Update the local session data
        final idx = unifiedSessions.indexWhere((s) => s.sessionId == session.sessionId);
        if (idx >= 0) {
          final old = unifiedSessions[idx];
          final newMembers = List<String>.from(old.members);
          if (!newMembers.contains(newMemberEmail)) newMembers.add(newMemberEmail);
          final updated = old.copyWith(
            mode: upgraded ? 'mls' : null,
            members: newMembers,
            mlsGroupId: upgraded ? (result['mls_group_id']?.toString() ?? '') : null,
          );
          unifiedSessions[idx] = updated;
        }

        // Show upgrade notification if applicable
        if (upgraded) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text(AppStrings.isZh ? '会话已升级为群组' : 'Session upgraded to group'),
              duration: const Duration(seconds: 2),
            ),
          );
        }

        // Reload messages and refresh UI — stays in the same window
        setState(() {});
        loadUnifiedSessionMessages(session.sessionId);
        loadUnifiedSessions(); // refresh the list
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('添加失败: ${result['error'] ?? 'unknown'}'), duration: const Duration(seconds: 3)));
      }
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('添加失败: $e'), duration: const Duration(seconds: 3)));
    }
  }

  List<String> _configAccounts() {
    final config = native.EmailCore.loadConfig(configPath);
    return config?.accounts
            .where((a) => a.email.isNotEmpty)
            .map((a) => a.email)
            .toList() ??
        <String>[];
  }
}

