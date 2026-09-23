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
import '../app_theme.dart';

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
            child: _buildInlineFileChip(context, droppedFiles[fileIndex], fileIndex),
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

  Widget _buildInlineFileChip(BuildContext context, DroppedFile file, int index) {
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
      iconColor = context.oim.textSecondary;
    }

    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 1),
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
      decoration: BoxDecoration(
        color: context.oim.groupHeader,
        borderRadius: BorderRadius.circular(3),
        border: Border.all(color: context.oim.border, width: 0.5),
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
            child: Icon(Icons.close, size: 12, color: context.oim.textMuted),
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
      return Expanded(child: Center(child: Text('Session not found', style: TextStyle(color: context.oim.textMuted))));
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
    final conversation = GestureDetector(
      behavior: HitTestBehavior.translucent,
      onTap: () {
        if (showUnifiedMembers) {
          setState(() {
            showUnifiedMembers = false;
            _editingTitle = false;
          });
        }
      },
      child: Container(
        color: context.scheme.surface,
        child: Column(
        children: [
          // Header
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
            decoration: BoxDecoration(
              color: context.oim.panel,
              border: Border(bottom: BorderSide(color: context.oim.border)),
            ),
            child: Row(
              children: [
                Icon(isGroup ? Icons.group : Icons.person,
                    color: isGroup ? context.oim.avatarGroupFg : context.oim.avatarTheirsFg, size: 24),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(title.isEmpty ? AppStrings.noSubject : title,
                          style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500),
                          maxLines: 1, overflow: TextOverflow.ellipsis),
                      Text('${members.length}${AppStrings.isZh ? "人" : " members"}',
                          style: TextStyle(fontSize: 12, color: context.oim.textMuted)),
                    ],
                  ),
                ),
                IconButton(
                  icon: Icon(Icons.more_horiz, size: 22, color: context.oim.textSecondary),
                  onPressed: () => setState(() => showUnifiedMembers = true),
                ),
              ],
            ),
          ),
          // Messages
          Expanded(
            child: messages.isEmpty
                ? Center(child: Text(AppStrings.sessionCreatedWaiting, style: TextStyle(color: context.oim.textMuted)))
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
      ),
    );

    // Member panel (slides in from right)
    final memberPanel = Container(
      width: 240,
      decoration: BoxDecoration(
        color: context.oim.panel,
        boxShadow: [BoxShadow(color: Colors.black.withValues(alpha: context.isDark ? 0.5 : 0.15), blurRadius: 12, offset: const Offset(-2, 0))],
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            child: Row(
              children: [
                Expanded(
                  child: _editingTitle
                      ? TextField(
                          controller: _titleEditController,
                          focusNode: _titleFocusNode,
                          autofocus: true,
                          style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
                          decoration: const InputDecoration(
                            isDense: true,
                            contentPadding: EdgeInsets.symmetric(vertical: 4),
                            border: InputBorder.none,
                          ),
                          onSubmitted: (_) => _commitTitleEdit(session),
                          onEditingComplete: () => _commitTitleEdit(session),
                        )
                      : GestureDetector(
                          onTap: () {
                            setState(() {
                              _titleEditController.text = session.subject;
                              _titleEditSession = session;
                              _editingTitle = true;
                            });
                          },
                          child: Tooltip(
                            message: AppStrings.isZh ? '点击修改会话标题' : 'Tap to rename session',
                            child: Text(
                              title.isEmpty ? AppStrings.noSubject : title,
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: context.oim.textPrimary),
                            ),
                          ),
                        ),
                ),
                Text('(${members.length})', style: TextStyle(fontSize: 11, color: context.oim.textMuted)),
                IconButton(
                  icon: Icon(Icons.close, size: 20, color: context.oim.textSecondary),
                  onPressed: () => setState(() {
                    showUnifiedMembers = false;
                    _editingTitle = false;
                  }),
                  constraints: const BoxConstraints(minWidth: 28, minHeight: 28),
                  padding: EdgeInsets.zero,
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
                        backgroundColor: isMe ? context.oim.avatarMineBg : context.oim.avatarTheirsBg,
                        child: Text(m.isNotEmpty ? m[0].toUpperCase() : '?',
                            style: TextStyle(fontSize: 12, color: isMe ? context.oim.avatarMineFg : context.oim.avatarTheirsFg)),
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(displayName, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500), maxLines: 1, overflow: TextOverflow.ellipsis),
                            Text(m, style: TextStyle(fontSize: 11, color: context.oim.textMuted), maxLines: 1, overflow: TextOverflow.ellipsis),
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
                icon: Icon(Icons.person_add, size: 18, color: context.oim.accentGreen),
                label: Text(AppStrings.isZh ? '添加成员' : 'Add member', style: TextStyle(fontSize: 13, color: context.oim.accentGreen)),
                style: TextButton.styleFrom(padding: const EdgeInsets.symmetric(vertical: 8), shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6))),
              ),
            ),
          ),
          // 置顶 / 隐藏会话 开关（持久化到 DB，切换后刷新列表分组）
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 0, 12, 8),
            child: Column(
              children: [
                _compactSwitch(AppStrings.pinSession, session.pinned, (v) {
                  UnifiedSessionManager.setSessionFlags(session.sessionId, v, session.hidden);
                  loadUnifiedSessions();
                }),
                _compactSwitch(AppStrings.hideSession, session.hidden, (v) {
                  UnifiedSessionManager.setSessionFlags(session.sessionId, session.pinned, v);
                  loadUnifiedSessions();
                }),
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

  bool _editingTitle = false;
  final TextEditingController _titleEditController = TextEditingController();
  UnifiedSessionInfo? _titleEditSession;
  late final FocusNode _titleFocusNode = FocusNode()
    ..addListener(() {
      // Blur: non-empty text commits, empty text abandons the edit.
      if (!_titleFocusNode.hasFocus && _editingTitle && _titleEditSession != null) {
        _commitTitleEdit(_titleEditSession!);
      }
    });

  void _commitTitleEdit(UnifiedSessionInfo session) {
    if (!_editingTitle) return;
    final newTitle = _titleEditController.text.trim();
    setState(() {
      _editingTitle = false;
      _titleEditSession = null;
    });
    if (newTitle.isEmpty || newTitle == session.subject) return;
    UnifiedSessionManager.setSessionSubject(session.sessionId, newTitle);
    loadUnifiedSessions();
  }

  Widget _compactSwitch(String label, bool value, ValueChanged<bool> onChanged) {
    return Row(
      children: [
        Expanded(child: Text(label, style: const TextStyle(fontSize: 13))),
        Transform.scale(
          scale: 0.67,
          child: Switch(value: value, onChanged: onChanged),
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
            CircleAvatar(radius: 16, backgroundColor: context.oim.avatarTheirsBg,
                child: Text(msg.sender.isNotEmpty ? msg.sender[0].toUpperCase() : '?', style: TextStyle(color: context.oim.avatarTheirsFg, fontSize: 11))),
            const SizedBox(width: 8),
          ],
          Flexible(
            child: Column(
              crossAxisAlignment: isMe ? CrossAxisAlignment.end : CrossAxisAlignment.start,
              children: [
                if (!isMe && members.length > 2)
                  Text(msg.sender.split('@').first, style: TextStyle(fontSize: 11, color: context.oim.textMuted)),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: BoxDecoration(
                    color: isMe ? context.oim.bubbleMine : context.oim.bubbleTheirs,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: context.oim.border),
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
                      color: msg.uuid.startsWith('pending_') ? Colors.orange[400] : context.oim.textMuted),
                ),
              ],
            ),
          ),
          if (isMe) ...[
            const SizedBox(width: 8),
            CircleAvatar(radius: 16, backgroundColor: context.oim.avatarMineBg,
                child: Text(AppStrings.me[0], style: TextStyle(color: context.oim.avatarMineFg, fontSize: 11))),
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
                  Text(sizeStr, style: TextStyle(fontSize: 11, color: context.oim.textMuted)),
                ]),
              ),
              if (done || isMe) Icon(Icons.download, size: 16, color: context.oim.textSecondary),
            ]),
            const SizedBox(height: 6),
            LinearProgressIndicator(
              value: progress,
              minHeight: 3,
              backgroundColor: context.oim.border,
              valueColor: AlwaysStoppedAnimation<Color>(failed ? Colors.red : context.scheme.primary),
            ),
            const SizedBox(height: 2),
            Text(
              failed ? AppStrings.transferFailed : done ? (AppStrings.isZh ? '已完成 ${meta.receivedChunks}/${meta.totalChunks}' : 'Done ${meta.receivedChunks}/${meta.totalChunks}') : '${meta.receivedChunks}/${meta.totalChunks}',
              style: TextStyle(fontSize: 10, color: context.oim.textMuted),
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
      final dir = await FilePicker.getDirectoryPath(dialogTitle: AppStrings.saveFile);
      if (dir == null || dir.isEmpty) return;
      final resultJson = isMe
          ? native.EmailCore.fileTransferCopyOriginal(meta.fileId, dir)
          : native.EmailCore.fileTransferReassemble(meta.fileId, dir);
      final result = jsonDecode(resultJson);
      final ok = result['status'] == 'success';
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
        content: Text(ok ? AppStrings.savedTo('${result['output_path'] ?? dir}') : AppStrings.sendFailedWith('${result['error'] ?? resultJson}')),
        duration: const Duration(seconds: 3),
      ));
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(AppStrings.sendFailedWith('$e'))));
    }
  }

  Widget _buildUnifiedInputBar(UnifiedSessionInfo session) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: context.oim.panel,
        border: Border(top: BorderSide(color: context.oim.border, width: 0.5)),
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
                      color: context.oim.inputField,
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Container(
                      decoration: BoxDecoration(
                        borderRadius: BorderRadius.circular(4),
                        border: Border.all(
                          color: _isDragging ? context.oim.accentGreen : context.oim.border,
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
                                    hintStyle: TextStyle(fontSize: 14, color: context.oim.textMuted),
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
                                padding: const EdgeInsets.only(left: 4, right: 8, bottom: 4),
                                child: Row(
                                  children: [
                                    IconButton(
                                      icon: Icon(Icons.folder_open, size: 20, color: context.oim.textSecondary),
                                      onPressed: () {},
                                      constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                                      padding: EdgeInsets.zero,
                                    ),
                                    IconButton(
                                      icon: Icon(
                                        showEmojiPicker ? Icons.emoji_emotions : Icons.emoji_emotions_outlined,
                                        size: 20,
                                        color: showEmojiPicker ? context.oim.accentGreen : context.oim.textSecondary,
                                      ),
                                      onPressed: () {
                                        setState(() {
                                          showEmojiPicker = !showEmojiPicker;
                                        });
                                      },
                                      constraints: const BoxConstraints(minWidth: 32, minHeight: 28),
                                      padding: EdgeInsets.zero,
                                    ),
                                    const Spacer(),
                                    TextButton(
                                      style: TextButton.styleFrom(
                                        backgroundColor: _sessionReady ? context.oim.accentGreen : context.oim.border,
                                        foregroundColor: Colors.white,
                                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
                                        minimumSize: const Size(0, 28),
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
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
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
                    backgroundColor: context.oim.inputField,
                  ),
                  categoryViewConfig: CategoryViewConfig(
                    indicatorColor: context.oim.accentGreen,
                    iconColorSelected: context.oim.accentGreen,
                    backgroundColor: context.oim.panel,
                  ),
                  searchViewConfig: SearchViewConfig(
                    backgroundColor: context.oim.inputField,
                    buttonIconColor: context.oim.textSecondary,
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
              ? AppStrings.keyExchangePending
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
          SnackBar(content: Text(AppStrings.messageSent), duration: const Duration(seconds: 1)));
        loadUnifiedSessionMessages(session.sessionId);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.fileSendFailed), duration: const Duration(seconds: 3)));
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
          SnackBar(content: Text(AppStrings.messageSent), duration: const Duration(seconds: 1)));
        loadUnifiedSessionMessages(session.sessionId);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.fileSendFailed), duration: const Duration(seconds: 3)));
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
            SnackBar(content: Text(AppStrings.sendFailedEnqueue), duration: const Duration(seconds: 3)));
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
          SnackBar(content: Text(AppStrings.queuedForSend), duration: const Duration(seconds: 1)));
        loadUnifiedSessionMessages(session.sessionId);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text(AppStrings.sendFailedWith('${result['error'] ?? 'unknown'}')), duration: const Duration(seconds: 3)));
      }
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.sendFailedWith('$e')), duration: const Duration(seconds: 3)));
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
                  Text('${AppStrings.isZh ? "当前成员" : "Current members"}: ${session.members.length}', style: TextStyle(fontSize: 12, color: context.oim.textSecondary)),
                  const SizedBox(height: 12),
                  Autocomplete<String>(
                    fieldViewBuilder: (context, ctrl, focusNode, onFieldSubmitted) {
                      controller = ctrl;
                      return TextField(
                        controller: ctrl,
                        focusNode: focusNode,
                        decoration: InputDecoration(hintText: AppStrings.addMemberHint, border: const OutlineInputBorder(), isDense: true),
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
                style: ElevatedButton.styleFrom(backgroundColor: context.oim.accentGreen),
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
          SnackBar(content: Text(AppStrings.addFailed('${result['error'] ?? 'unknown'}')), duration: const Duration(seconds: 3)));
      }
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.addFailed('$e')), duration: const Duration(seconds: 3)));
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

