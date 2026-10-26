import 'dart:io';
import 'dart:isolate';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

import 'i18n/app_strings.dart';
import 'native/email_background_service.dart';
import 'native/email_core.dart' as native;

class BackupPage extends StatefulWidget {
  const BackupPage({super.key});

  @override
  State<BackupPage> createState() => _BackupPageState();
}

class _BackupPageState extends State<BackupPage> {
  bool _busy = false;
  bool _restartRequired = false;
  String _progress = '';
  final _exportPassword = TextEditingController();
  final _exportConfirm = TextEditingController();
  final _importPassword = TextEditingController();
  String? _exportError;
  String? _importError;

  String _t(String zh, String en) => AppStrings.isZh ? zh : en;

  @override
  void dispose() {
    _exportPassword.dispose();
    _exportConfirm.dispose();
    _importPassword.dispose();
    super.dispose();
  }

  void _message(String message) {
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
    }
  }

  Future<Map<String, dynamic>> _native(String op, String path, String password) async {
    final root = (await getApplicationSupportDirectory()).path;
    final result = await Isolate.run(() =>
        native.EmailCore.backupAction(op, root, file: path, password: password));
    if (result['status'] != 'success') {
      native.EmailCore.logWrite('[backup] $op failed: ${result['error']}');
      throw StateError('${result['error'] ?? 'recovery_failed'}');
    }
    return result;
  }

  String _error(Object error) {
    final code = error.toString();
    if (code.contains('backup_container_open:-2')) {
      return _t('口令错误或备份文件已损坏。', 'Incorrect password or damaged backup.');
    }
    if (code.contains('too_large') || code.contains('export:-5')) {
      return _t('备份超过当前 256 MiB 容量限制。', 'Backup exceeds the current 256 MiB limit.');
    }
    if (code.contains('original_missing') || code.contains('original_unreadable')) {
      return _t('部分附件原文件已丢失或无法读取，未生成不完整备份。', 'Some original attachments are missing or unreadable. No incomplete backup was created.');
    }
    if (code.contains('schema_mismatch')) {
      return _t('备份的数据结构与当前版本不一致，请使用相同版本恢复。', 'Backup schema differs. Restore with the same app version.');
    }
    if (code.contains('credential_store')) {
      return _t('无法读取邮箱凭据，请解锁设备后重试。', 'Cannot read account credentials. Unlock the device and retry.');
    }
    return _t('操作未完成。请保留原数据和备份文件后重试。', 'Operation did not complete. Keep your original data and backup, then retry.');
  }

  Future<void> _export() async {
    final password = _exportPassword.text;
    if (password.runes.length < 8) {
      setState(() => _exportError = _t('口令至少需要 8 个字符', 'Use at least 8 characters'));
      return;
    }
    if (password != _exportConfirm.text) {
      setState(() => _exportError = _t('两次口令不一致', 'Passwords do not match'));
      return;
    }
    setState(() { _busy = true; _exportError = null; });
    Directory? temporary;
    bool paused = false;
    try {
      setState(() => _progress = _t('正在等待收发完成…', 'Waiting for mail operations to finish…'));
      await EmailBackgroundService.pauseAll();
      paused = true;
      final tmpBase = await getTemporaryDirectory();
      // Sandboxed macOS: Library/Caches/<id> may not exist until created.
      await Directory(tmpBase.path).create(recursive: true);
      temporary = await tmpBase.createTemp('oim-backup-');
      final file = File('${temporary.path}/full.oimbak');
      setState(() => _progress = _t('正在加密备份…', 'Encrypting backup…'));
      await _native('export', file.path, password);
      final stamp = DateTime.now().toIso8601String().replaceAll(RegExp(r'[:.]'), '-');
      final destination = await FilePicker.saveFile(
        dialogTitle: _t('保存完整备份', 'Save full backup'),
        fileName: 'OceanTalk-$stamp.oimbak',
        bytes: await file.readAsBytes(),
        type: FileType.custom,
        allowedExtensions: ['oimbak'],
      );
      if (destination != null) {
        _exportPassword.clear();
        _exportConfirm.clear();
        _message(_t('加密备份已保存，请自行保管文件和口令。', 'Encrypted backup saved. Keep the file and password safe.'));
      }
    } catch (e) {
      native.EmailCore.logWrite('[backup] export error: $e');
      if (e.toString().contains('recovery_requires_restart')) {
        _restartRequired = true;
      }
      _exportError = _error(e);
    } finally {
      if (paused && !_restartRequired) EmailBackgroundService.resumeAll();
      if (temporary != null) await temporary.delete(recursive: true);
      if (mounted) setState(() { _busy = false; _progress = ''; });
    }
  }

  Future<bool> _confirmRestore(Map<String, dynamic> preview) async {
    bool latest = false;
    return await showDialog<bool>(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (context, update) => AlertDialog(
          title: Text(_t('确认恢复完整备份', 'Restore full backup')),
          content: SingleChildScrollView(child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(_t(
                '${preview['accounts']} 个账号，${preview['messages']} 条消息，${preview['originals']} 个附件原文件。',
                '${preview['accounts']} accounts, ${preview['messages']} messages, ${preview['originals']} original attachments.',
              )),
              if ((preview['missing_originals'] ?? 0) > 0) ...[
                const SizedBox(height: 8),
                Text(_t(
                  '${preview['missing_originals']} 个已发送附件的原始文件已不在本机，恢复后这些附件不可打开。',
                  '${preview['missing_originals']} sent attachment(s) no longer exist on this device; they cannot be opened after restore.',
                ), style: TextStyle(color: Theme.of(context).colorScheme.error, fontSize: 13)),
              ],
              const SizedBox(height: 12),
              Text(_t('恢复将替换本机账号和聊天数据，不会与现有数据合并。退出后重新打开应用完成恢复。',
                  'Restoring replaces this device’s accounts and chats without merging. Quit and reopen the app to complete restoration.')),
              const SizedBox(height: 12),
              Text(_t('继续会话需要旧设备停用前的最新备份。较早的备份不能保证安全续聊。',
                  'Continuing conversations requires the latest backup from before retiring the old device. Older backups cannot guarantee safe continuation.')),
              CheckboxListTile(
                contentPadding: EdgeInsets.zero,
                title: Text(_t('这是最新备份，旧设备将停止使用此账号。',
                    'This is the latest backup, and the old device will stop using this account.')),
                value: latest,
                onChanged: (v) => update(() => latest = v == true),
              ),
            ],
          )),
          actions: [
            TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: Text(AppStrings.cancel)),
            FilledButton(
              onPressed: latest ? () => Navigator.pop(dialogContext, true) : null,
              child: Text(_t('替换并恢复', 'Replace and restore')),
            ),
          ],
        ),
      ),
    ) ?? false;
  }

  Future<void> _import() async {
    final password = _importPassword.text;
    if (password.isEmpty) {
      setState(() => _importError = _t('请输入口令', 'Enter a password'));
      return;
    }
    setState(() { _busy = true; _importError = null; });
    bool paused = false;
    Directory? staged;
    try {
      final picked = await FilePicker.pickFiles(
        type: FileType.custom,
        allowedExtensions: ['oimbak'],
      );
      if (picked.isEmpty || !mounted) return;
      var path = picked.first.path;
      if (path == null) {
        // Android SAF may return a content:// URI without a filesystem path.
        staged = await (await getTemporaryDirectory()).createTemp('oim-import-');
        final stagedFile = File('${staged.path}/import.oimbak');
        await stagedFile.writeAsBytes(await picked.first.readAsBytes());
        path = stagedFile.path;
      }
      if (!mounted) return;
      setState(() => _progress = _t('正在解密并校验备份…', 'Decrypting and validating backup…'));
      final preview = await _native('preview', path, password);
      if (!mounted || !await _confirmRestore(preview)) return;
      setState(() => _progress = _t('正在等待收发完成…', 'Waiting for mail operations to finish…'));
      await EmailBackgroundService.pauseAll();
      paused = true;
      setState(() => _progress = _t('正在准备恢复数据…', 'Preparing restored data…'));
      await _native('prepare', path, password);
      _importPassword.clear();
      setState(() => _restartRequired = true);
    } catch (e) {
      native.EmailCore.logWrite('[backup] import error: $e');
      if (e.toString().contains('recovery_requires_restart')) {
        _restartRequired = true;
      }
      _importError = _error(e);
    } finally {
      if (paused && !_restartRequired) EmailBackgroundService.resumeAll();
      if (staged != null) staged.delete(recursive: true);
      if (mounted) setState(() { _busy = false; _progress = ''; });
    }
  }

  Widget _sectionTitle(String text) => Padding(
    padding: const EdgeInsets.only(bottom: 12),
    child: Text(text, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
  );

  Widget _passwordField(TextEditingController controller, String label) => Padding(
    padding: const EdgeInsets.only(bottom: 10),
    child: TextField(
      controller: controller,
      obscureText: true,
      autocorrect: false,
      enableSuggestions: false,
      enabled: !_busy,
      decoration: InputDecoration(
        labelText: label,
        border: const OutlineInputBorder(),
        isDense: true,
      ),
    ),
  );

  Widget _errorText(String text) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Text(text, style: TextStyle(color: Theme.of(context).colorScheme.error, fontSize: 13)),
  );

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: !_busy && !_restartRequired,
      child: Scaffold(
        appBar: AppBar(
          title: Text(_t('备份与恢复', 'Backup and restore')),
          automaticallyImplyLeading: !_busy && !_restartRequired,
        ),
        body: ListView(padding: const EdgeInsets.all(24), children: [
          if (_restartRequired) ...[
            Text(_t('请退出并重新打开 OceanTalk。应用会完成恢复，若切换未完成则回到原数据。',
                'Quit and reopen OceanTalk. The app will finish restoring, or roll back an incomplete switch.')),
            const SizedBox(height: 16),
            FilledButton(onPressed: () => exit(0), child: Text(_t('退出应用', 'Quit app'))),
          ] else ...[
            Text(_t('完整备份包含账号凭据、聊天记录和附件，并使用独立口令加密。文件只保存在本地，由你自行保管和转移。',
                'Full backups include credentials, chats and attachments, encrypted with a separate password. Files stay local for you to store and transfer.')),
            const SizedBox(height: 12),
            Text(_t('请使用长口令。忘记口令将无法恢复。当前备份容量上限为 256 MiB。',
                'Use a long password. A forgotten password cannot be recovered. Current backup limit: 256 MiB.')),
            const SizedBox(height: 24),
            _sectionTitle(_t('导出完整备份', 'Export full backup')),
            _passwordField(_exportPassword, _t('备份口令（至少 8 个字符）', 'Backup password (8+ characters)')),
            _passwordField(_exportConfirm, _t('再次输入口令', 'Confirm password')),
            if (_exportError != null) _errorText(_exportError!),
            const SizedBox(height: 8),
            FilledButton.icon(
              onPressed: _busy ? null : _export,
              icon: const Icon(Icons.save_alt),
              label: Text(_t('导出完整备份', 'Export full backup')),
            ),
            const SizedBox(height: 24),
            const Divider(),
            const SizedBox(height: 16),
            _sectionTitle(_t('导入备份', 'Import backup')),
            _passwordField(_importPassword, _t('备份口令', 'Backup password')),
            if (_importError != null) _errorText(_importError!),
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: _busy || Platform.isWindows ? null : _import,
              icon: const Icon(Icons.restore),
              label: Text(_t('导入备份', 'Import backup')),
            ),
            if (_busy) ...[
              const SizedBox(height: 24),
              const LinearProgressIndicator(),
              const SizedBox(height: 12),
              Text(_progress),
            ],
          ],
        ]),
      ),
    );
  }
}
