import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import 'email_utils.dart';
import 'email_module_base.dart';
import 'eml_parser.dart';
import '../../i18n/app_strings.dart';

mixin EmailDetailViewMixin on State<EmailModule> {
  List<native.EmailMessage> get emails;
  int get selectedEmail;
  set selectedEmail(int v);
  bool get isConversationView;
  set isConversationView(bool v);
  String? get selectedConversationMessageId;
  set selectedConversationMessageId(String? v);
  TextEditingController get replyController;
  String get emailDataPath;

  void saveEmails() {}

  Widget buildEmailDetail() {
    native.EmailCore.logWrite('[EmailDetail] buildEmailDetail: isConversationView=$isConversationView, selectedConversationMessageId=$selectedConversationMessageId');
    if (isConversationView && selectedConversationMessageId != null) {
      // Will be handled by ConversationViewMixin
      native.EmailCore.logWrite('[EmailDetail] Using conversation view, skipping detail view');
      return const SizedBox.shrink();
    }
    if (emails.isEmpty) {
      return Expanded(
        child: Container(
          color: Colors.white,
          child: Center(child: Text(AppStrings.noEmails, style: TextStyle(color: Colors.grey[500]))),
        ),
      );
    }
    if (selectedEmail >= emails.length) {
      selectedEmail = emails.isEmpty ? 0 : emails.length - 1;
    }
    final email = emails[selectedEmail];
    native.EmailCore.logWrite('[EmailDetail] Building detail for email: ${email.uuid}');

    return Expanded(
      child: Container(
        color: Colors.white,
        child: Column(
          children: [
            buildEmailDetailHeader(email),
            buildEmailActions(),
            Expanded(
              child: SingleChildScrollView(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(minHeight: 0),
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(email.subject.isEmpty ? AppStrings.noSubject : email.subject, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                        const SizedBox(height: 16),
                        Row(
                          children: [
                            _buildAvatar(extractName(email.sender)),
                            const SizedBox(width: 12),
                            Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Text(extractName(email.sender), style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w500)),
                                Text(formatTime(email.timestamp), style: TextStyle(fontSize: 12, color: Colors.grey[600])),
                              ],
                            ),
                          ],
                        ),
                        const SizedBox(height: 24),
                        const Divider(),
                        const SizedBox(height: 24),
                        _buildEmailBody(email),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget buildEmailDetailHeader(native.EmailMessage email) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: Colors.grey[50],
        border: Border(bottom: BorderSide(color: Colors.grey[200]!)),
      ),
      child: Row(
        children: [
          IconButton(icon: const Icon(Icons.arrow_back, size: 20), onPressed: () {}),
          const SizedBox(width: 8),
          Text(AppStrings.emailDetail, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w500)),
          const Spacer(),
          IconButton(icon: const Icon(Icons.archive, size: 20), onPressed: () {}),
          IconButton(icon: const Icon(Icons.delete, size: 20), onPressed: () {}),
          IconButton(icon: const Icon(Icons.more_vert, size: 20), onPressed: () {}),
        ],
      ),
    );
  }

  Widget buildEmailActions() {
    final email = emails[selectedEmail];
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      decoration: BoxDecoration(
        color: Colors.white,
        border: Border(bottom: BorderSide(color: Colors.grey[200]!)),
      ),
      child: Row(
        children: [
          _buildActionButton(Icons.reply, AppStrings.reply),
          _buildActionButton(Icons.reply_all, AppStrings.replyAll),
          _buildActionButton(Icons.forward, AppStrings.forward),
          _buildActionButton(Icons.forum, AppStrings.conversation, onTap: () {
            if (email.sessionId.isNotEmpty || email.messageId.isNotEmpty) {
              setState(() {
                selectedConversationMessageId = email.sessionId.isNotEmpty ? email.sessionId : email.messageId;
                isConversationView = true;
              });
            }
          }),
        ],
      ),
    );
  }

  Widget _buildActionButton(IconData icon, String label, {VoidCallback? onTap}) {
    final widget = Padding(
      padding: const EdgeInsets.only(right: 16),
      child: Row(
        children: [
          Icon(icon, size: 18, color: Colors.grey[700]),
          const SizedBox(width: 4),
          Text(label, style: TextStyle(fontSize: 13, color: Colors.grey[700])),
        ],
      ),
    );
    if (onTap != null) {
      return GestureDetector(onTap: onTap, child: widget);
    }
    return widget;
  }

  Widget _buildEmailBody(native.EmailMessage email) {
    native.EmailCore.logWrite('[EmailDetail] email.uuid=${email.uuid}, email.file="${email.file}", email.isLocal=${email.isLocal}, email.recipient=${email.recipient}');
    if (email.file.isEmpty) {
      native.EmailCore.logWrite('[EmailDetail] file is empty, showing downloading indicator');
      return Center(
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 40),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              SizedBox(
                width: 16,
                height: 16,
                child: CircularProgressIndicator(
                  strokeWidth: 2,
                  color: Colors.grey[400],
                ),
              ),
              const SizedBox(width: 10),
              Text(AppStrings.emailDownloading, style: TextStyle(fontSize: 14, color: Colors.grey[400])),
            ],
          ),
        ),
      );
    }

    // file is not empty: parse .eml file using file field
    final emlPath = '$emailDataPath/${email.account}/${email.file}.eml';
    native.EmailCore.logWrite('[EmailDetail] emlPath=$emlPath');
    final parsed = parseEmlFile(emlPath, account: email.account);
    final bodyText = parsed.textBody.isNotEmpty ? parsed.textBody : AppStrings.noContent;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        if (parsed.hasAttachments)
          Padding(
            padding: const EdgeInsets.only(bottom: 12),
            child: Row(
              children: [
                Icon(Icons.attach_file, size: 18, color: Colors.orange[700]),
                const SizedBox(width: 4),
                Text('${parsed.attachments.length} ${AppStrings.attachmentsCount}', style: TextStyle(fontSize: 13, color: Colors.orange[700])),
              ],
            ),
          ),
        SelectableText(bodyText, style: const TextStyle(fontSize: 14, height: 1.6)),
      ],
    );
  }

  Widget _buildAvatar(String name) {
    final displayName = name.isEmpty ? '?' : name[0];
    return Container(
      width: 32,
      height: 32,
      decoration: BoxDecoration(color: avatarColor(name), borderRadius: BorderRadius.circular(16)),
      child: Center(child: Text(displayName, style: const TextStyle(color: Colors.white, fontSize: 13, fontWeight: FontWeight.w500))),
    );
  }

  void sendReply() {
    if (emails.isEmpty || replyController.text.trim().isEmpty) return;
    final email = emails[selectedEmail];
    final ok = native.EmailCore.sendEmail(
      recipient: email.sender,
      subject: 'Re: ${email.subject}',
      body: replyController.text,
    );
    if (ok) {
      replyController.clear();
      saveEmails();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.replySent), duration: const Duration(seconds: 2)),
      );
    }
  }
}
