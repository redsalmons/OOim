import 'dart:convert';

import 'package:emoji_picker_flutter/emoji_picker_flutter.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:flutter/services.dart';
import 'package:oim_core/i18n/app_strings.dart';
import 'package:oim_core/native/email_core.dart' as native;
import 'package:oim_core/native/unified_session.dart';

import 'mail_service.dart';

class ConversationPage extends StatefulWidget {
  const ConversationPage({super.key, required this.session});

  final UnifiedSessionInfo session;

  @override
  State<ConversationPage> createState() => _ConversationPageState();
}

class _ConversationPageState extends State<ConversationPage> {
  final _input = TextEditingController();
  final _inputFocus = FocusNode();
  final _scroll = ScrollController();
  bool _atBottom = true;
  final _pickedFiles = <PlatformFile>[];
  bool _showEmoji = false;
  List<SessionMessage> _messages = [];
  final List<SessionMessage> _pending = [];

  // Refreshed from the core on every load so status / fingerprints stay current.
  UnifiedSessionInfo? _live;
  UnifiedSessionInfo get session => _live ?? widget.session;

  @override
  void initState() {
    super.initState();
    MailService.instance.tick.addListener(_load);
    _scroll.addListener(() {
      if (_scroll.hasClients) {
        _atBottom = _scroll.position.pixels >=
            _scroll.position.maxScrollExtent - 40;
      }
    });
    _inputFocus.addListener(() {
      if (_inputFocus.hasFocus) {
        if (_showEmoji) setState(() => _showEmoji = false);
        SystemChannels.textInput
            .invokeMethod('TextInput.show')
            .catchError((_) => null);
      } else {
        SystemChannels.textInput
            .invokeMethod('TextInput.hide')
            .catchError((_) => null);
      }
    });
    _load();
  }

  @override
  void dispose() {
    MailService.instance.tick.removeListener(_load);
    _input.dispose();
    _inputFocus.dispose();
    _scroll.dispose();
    super.dispose();
  }

  void _toggleEmoji() {
    if (_showEmoji) {
      setState(() => _showEmoji = false);
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _inputFocus.requestFocus();
      });
    } else {
      _inputFocus.unfocus();
      setState(() => _showEmoji = true);
    }
  }

  void _insertEmoji(Emoji emoji) {
    final text = _input.text;
    final selection = _input.selection;
    final cursorPos =
        selection.baseOffset < 0 ? text.length : selection.baseOffset;
    _input.value = TextEditingValue(
      text: text.substring(0, cursorPos) + emoji.emoji + text.substring(cursorPos),
      selection:
          TextSelection.collapsed(offset: cursorPos + emoji.emoji.length),
    );
  }

  void _scrollToBottom() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted && _scroll.hasClients) {
        _atBottom = true;
        _scroll.jumpTo(_scroll.position.maxScrollExtent);
      }
    });
  }

  void _load() {
    if (!mounted) return;
    try {
      final resp = jsonDecode(UnifiedSessionManager.getSession(session.sessionId));
      if (resp['status'] == 'success') {
        _live = UnifiedSessionInfo.fromJson(resp['session'] as Map<String, dynamic>);
      }
    } catch (_) {}
    final prevCount = _messages.length + _pending.length;
    final messages = UnifiedSessionManager.listMessages(session.account, session.sessionId);
    // Drop optimistic bubbles once the real message landed or the task failed.
    _pending.removeWhere((p) {
      if (messages.any((m) => m.messageId == p.messageId)) return true;
      try {
        final st = jsonDecode(native.EmailCore.taskStatus(session.account, p.messageId));
        return st['task_status'] == 'failed';
      } catch (_) {
        return false;
      }
    });
    setState(() => _messages = messages);
    if (_atBottom && _messages.length + _pending.length > prevCount) {
      _scrollToBottom();
    }
  }

  Future<void> _pickFiles() async {
    final result = await FilePicker.platform.pickFiles(allowMultiple: true);
    if (result == null) return;
    setState(() => _pickedFiles.addAll(result.files.where((f) => f.path != null)));
  }

  void _send() {
    final bodyText = _input.text.trim();
    if (bodyText.isEmpty && _pickedFiles.isEmpty) return;

    if (session.identityChanged) {
      _toast(AppStrings.identityChangedBlocked);
      return;
    }
    if (!UnifiedSessionManager.isReady(session.account, session.sessionId)) {
      _toast(AppStrings.keyExchangePending);
      return;
    }

    final withFiles = _pickedFiles.isNotEmpty;
    final resultJson = withFiles
        ? UnifiedSessionManager.sendFiles(session.account, session.sessionId,
            [for (final f in _pickedFiles) (path: f.path!, name: f.name)], bodyText)
        : UnifiedSessionManager.sendMessage(session.account, session.sessionId, bodyText);
    native.EmailCore.logWrite('[Unified] send result: $resultJson');
    try {
      final result = jsonDecode(resultJson) as Map<String, dynamic>;
      if (result['status'] != 'success') throw StateError('send failed');
      _pending.add(SessionMessage.pendingFrom(result, sender: session.account, text: bodyText));
      _input.clear();
      _pickedFiles.clear();
      _inputFocus.unfocus();
      setState(() {});
      _scrollToBottom();
    } catch (_) {
      _toast(withFiles ? AppStrings.fileSendFailed : AppStrings.sendFailed);
    }
  }

  void _toast(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(msg), duration: const Duration(seconds: 2)));
  }

  // --- message rendering ---

  Widget _bubble(SessionMessage msg) {
    final isMe = msg.isSent;
    final Widget content = switch (msg.kind) {
      MessageKind.handshake => const Text('🤝', style: TextStyle(fontSize: 20)),
      MessageKind.file => _fileCard(msg.files, msg.text, msg.isSent),
      MessageKind.text => SelectableText(msg.text),
    };

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      child: Row(
        mainAxisAlignment: isMe ? MainAxisAlignment.end : MainAxisAlignment.start,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (!isMe)
            CircleAvatar(
              radius: 14,
              child: Text(msg.sender.isNotEmpty ? msg.sender[0].toUpperCase() : '?',
                  style: const TextStyle(fontSize: 11)),
            ),
          const SizedBox(width: 8),
          Flexible(
            child: Column(
              crossAxisAlignment:
                  isMe ? CrossAxisAlignment.end : CrossAxisAlignment.start,
              children: [
                if (!isMe && session.members.length > 2)
                  Text(msg.sender.split('@').first,
                      style: const TextStyle(fontSize: 11, color: Colors.grey)),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: BoxDecoration(
                    color: isMe
                        ? Theme.of(context).colorScheme.primaryContainer
                        : Theme.of(context).colorScheme.surfaceContainerHighest,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: content,
                ),
                Text(
                  msg.pending ? (AppStrings.isZh ? '发送中…' : 'Sending…') : msg.timestamp,
                  style: TextStyle(
                      fontSize: 10, color: msg.pending ? Colors.orange : Colors.grey),
                ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          if (isMe)
            CircleAvatar(
              radius: 14,
              child: Text(AppStrings.me[0], style: const TextStyle(fontSize: 11)),
            ),
        ],
      ),
    );
  }

  Widget _fileCard(List<SessionFile> files, String caption, bool isMe) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (caption.isNotEmpty)
          Padding(
            padding: const EdgeInsets.only(bottom: 6),
            child: SelectableText(caption),
          ),
        for (final f in files)
          _fileRow(f, isMe),
      ],
    );
  }

  Widget _fileRow(SessionFile f, bool isMe) {
    final done = f.transferStatus == 1;
    final failed = f.transferStatus == 2;
    final canSave = done || isMe;
    return GestureDetector(
      onTap: canSave ? () => _saveFile(f, isMe) : null,
      child: Row(mainAxisSize: MainAxisSize.min, children: [
        Icon(failed ? Icons.error_outline : Icons.insert_drive_file,
            size: 18, color: failed ? Colors.red : null),
        const SizedBox(width: 6),
        Flexible(child: Text(f.fileName, overflow: TextOverflow.ellipsis)),
        const SizedBox(width: 6),
        Text(_progress(f), style: const TextStyle(fontSize: 11, color: Colors.grey)),
        if (canSave) ...[
          const SizedBox(width: 4),
          const Icon(Icons.download, size: 16, color: Colors.grey),
        ],
      ]),
    );
  }

  String _progress(SessionFile f) {
    if (f.transferStatus == 2) return AppStrings.transferFailed;
    if (f.transferStatus == 1) return _fmtSize(f.fileSize);
    if (f.totalChunks > 0) return '${f.receivedChunks}/${f.totalChunks}';
    return '';
  }

  Future<void> _saveFile(SessionFile f, bool isMe) async {
    try {
      final dir = await getExternalStorageDirectory();
      if (dir == null) return;
      final resultJson = isMe
          ? native.EmailCore.fileTransferCopyOriginal(f.fileId, dir.path)
          : native.EmailCore.fileTransferReassemble(f.fileId, dir.path);
      final result = jsonDecode(resultJson);
      final ok = result['status'] == 'success';
      if (!mounted) return;
      _toast(ok
          ? AppStrings.savedTo('${result['output_path'] ?? dir.path}')
          : AppStrings.sendFailedWith('${result['error'] ?? resultJson}'));
    } catch (e) {
      if (mounted) _toast(AppStrings.sendFailedWith('$e'));
    }
  }

  static String _fmtSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    if (bytes < 1024 * 1024 * 1024) return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(2)} GB';
  }

  // --- input bar ---

  Widget _inputBar() {
    return SafeArea(
      top: false,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (_pickedFiles.isNotEmpty)
              SizedBox(
                height: 32,
                child: ListView(
                  scrollDirection: Axis.horizontal,
                  children: [
                    for (var i = 0; i < _pickedFiles.length; i++)
                      Padding(
                        padding: const EdgeInsets.only(right: 6),
                        child: Chip(
                          label: Text(_pickedFiles[i].name,
                              style: const TextStyle(fontSize: 12)),
                          onDeleted: () => setState(() => _pickedFiles.removeAt(i)),
                        ),
                      ),
                  ],
                ),
              ),
            Container(
              decoration: BoxDecoration(
                border: Border.all(color: Theme.of(context).dividerColor),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  TextField(
                    controller: _input,
                    focusNode: _inputFocus,
                    minLines: 2,
                    maxLines: 5,
                    onTap: () {
                      if (_showEmoji) setState(() => _showEmoji = false);
                    },
                    decoration: InputDecoration(
                      hintText: AppStrings.sendMessageHint,
                      border: InputBorder.none,
                      enabledBorder: InputBorder.none,
                      focusedBorder: InputBorder.none,
                      contentPadding: const EdgeInsets.symmetric(
                          horizontal: 10, vertical: 8),
                      isDense: true,
                    ),
                  ),
                  Padding(
                    padding: const EdgeInsets.only(left: 4, right: 8, bottom: 4),
                    child: Row(children: [
                      IconButton(
                        icon: const Icon(Icons.attach_file, size: 20),
                        onPressed: _pickFiles,
                        constraints:
                            const BoxConstraints(minWidth: 32, minHeight: 28),
                        padding: EdgeInsets.zero,
                      ),
                      IconButton(
                        icon: Icon(
                          _showEmoji
                              ? Icons.emoji_emotions
                              : Icons.emoji_emotions_outlined,
                          size: 20,
                          color: _showEmoji
                              ? Theme.of(context).colorScheme.primary
                              : null,
                        ),
                        onPressed: _toggleEmoji,
                        constraints:
                            const BoxConstraints(minWidth: 32, minHeight: 28),
                        padding: EdgeInsets.zero,
                      ),
                      const Spacer(),
                      TextButton(
                        style: TextButton.styleFrom(
                          backgroundColor:
                              Theme.of(context).colorScheme.primary,
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(
                              horizontal: 16, vertical: 4),
                          minimumSize: const Size(0, 28),
                          shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(4)),
                        ),
                        onPressed: _send,
                        child: Text(AppStrings.send,
                            style: const TextStyle(
                                fontSize: 14, fontWeight: FontWeight.w500)),
                      ),
                    ]),
                  ),
                ],
              ),
            ),
            if (_showEmoji)
              SizedBox(
                height: 250,
                child: EmojiPicker(
                  onEmojiSelected: (category, emoji) => _insertEmoji(emoji),
                  config: const Config(
                    height: 250,
                    checkPlatformCompatibility: true,
                    emojiViewConfig: EmojiViewConfig(emojiSizeMax: 28),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    if (MediaQuery.of(context).viewInsets.bottom > 0 && _atBottom) {
      _scrollToBottom();
    }
    final title = session.subject.isNotEmpty
        ? session.subject
        : session.members.where((m) => m != session.account).join(', ');
    final all = [..._messages, ..._pending];
    return PopScope(
      canPop: !_showEmoji,
      onPopInvokedWithResult: (didPop, _) {
        if (!didPop) setState(() => _showEmoji = false);
      },
      child: Scaffold(
        appBar: AppBar(title: Text(title)),
        body: Column(children: [
          Expanded(
            child: GestureDetector(
              behavior: HitTestBehavior.translucent,
              onTap: () => _inputFocus.unfocus(),
              child: all.isEmpty
                  ? Center(
                      child: Text(
                        AppStrings.isZh ? '暂无消息' : 'No messages',
                        style: const TextStyle(color: Colors.grey),
                      ),
                    )
                  : ListView.builder(
                      controller: _scroll,
                      keyboardDismissBehavior:
                          ScrollViewKeyboardDismissBehavior.onDrag,
                      padding: const EdgeInsets.only(top: 8, bottom: 8),
                      itemCount: all.length,
                      itemBuilder: (_, i) => _bubble(all[i]),
                    ),
            ),
          ),
          if (session.identityChanged) _identityBanner(),
          _inputBar(),
        ]),
      ),
    );
  }

  Widget _identityBanner() {
    final scheme = Theme.of(context).colorScheme;
    return Material(
      color: scheme.errorContainer,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Icon(Icons.gpp_maybe, color: scheme.onErrorContainer),
            const SizedBox(width: 8),
            Expanded(
              child: Text(AppStrings.identityChangedTitle,
                  style: TextStyle(fontWeight: FontWeight.w600, color: scheme.onErrorContainer)),
            ),
          ]),
          const SizedBox(height: 6),
          Text(AppStrings.identityChangedBody,
              style: TextStyle(fontSize: 13, color: scheme.onErrorContainer)),
          const SizedBox(height: 6),
          _fingerprintRow(AppStrings.myFingerprint, session.identityFp),
          _fingerprintRow(AppStrings.peerFingerprint, session.peerIdentityFp),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton(
              onPressed: () {
                if (UnifiedSessionManager.resetPeerIdentity(session.sessionId)) _load();
              },
              child: Text(AppStrings.trustNewIdentity),
            ),
          ),
        ]),
      ),
    );
  }

  Widget _fingerprintRow(String label, String fp) {
    final grouped = fp.isEmpty
        ? '-'
        : [for (var i = 0; i + 4 <= fp.length; i += 4) fp.substring(i, i + 4)].join(' ');
    return Padding(
      padding: const EdgeInsets.only(top: 2),
      child: SelectableText('$label: $grouped',
          style: const TextStyle(fontSize: 11, fontFamily: 'monospace')),
    );
  }
}
