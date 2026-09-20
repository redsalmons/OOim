import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import '../../native/unified_session.dart';
import 'email_utils.dart';
import 'email_module_base.dart';
import 'conversation_view.dart';
import '../dialogs/email_config_dialog.dart';
import '../dialogs/create_session_dialog.dart';
import '../../i18n/app_strings.dart';

mixin EmailListViewMixin on State<EmailModule> {
  double get listWidth;
  set listWidth(double v);
  String get searchQuery;
  set searchQuery(String v);
  TextEditingController get searchController;
  List<native.EmailMessage> get emails;
  Set<String> get collapsedSections;
  Set<String> get collapsedGroups;
  int get selectedEmail;
  set selectedEmail(int v);
  Set<int> get unreadIndices;
  Map<String, int> get configIndexMap;
  String get configPath;

  // Unified session state
  List<UnifiedSessionInfo> get unifiedSessions;
  String? get selectedUnifiedSessionId;
  set selectedUnifiedSessionId(String? v);
  List<native.EmailMessage> get unifiedMessages;
  void loadUnifiedSessions();
  void loadUnifiedSessionMessages(String unifiedSessionId);

  void refreshEmails() {}
  void fetchEmailsFromAccounts() {}

  Widget buildEmailList() {
    return Container(
      width: listWidth,
      color: const Color(0xFFFAFAFA),
      child: Column(
        children: [
          buildSearchBar(),
          Expanded(
            child: ListView(
              children: buildGroupedEmailList(),
            ),
          ),
        ],
      ),
    );
  }

  List<Widget> buildGroupedEmailList() {
    final List<Widget> widgets = [];

    // Section 0: Unified sessions — grouped by account
    {
      final sectionKey = 'unified_sessions';
      final isCollapsed = collapsedSections.contains(sectionKey);
      widgets.add(_buildUnifiedSectionHeader(
          AppStrings.conversation, unifiedSessions.length, sectionKey, isCollapsed));
      if (!isCollapsed) {
        if (unifiedSessions.isEmpty) {
          widgets.add(Container(
            padding: const EdgeInsets.fromLTRB(28, 6, 8, 6),
            child: Text(AppStrings.noConversations, style: TextStyle(fontSize: 12, color: Colors.grey[400])),
          ));
        } else {
          // Group sessions by account
          final sessionsByAccount = <String, List<UnifiedSessionInfo>>{};
          for (final s in unifiedSessions) {
            final acc = s.account.isNotEmpty ? s.account : AppStrings.unknownAccount;
            sessionsByAccount.putIfAbsent(acc, () => []).add(s);
          }
          for (final entry in sessionsByAccount.entries) {
            final account = entry.key;
            final sessions = entry.value;
            final groupKey = 'unified:$account';
            final isGroupCollapsed = collapsedGroups.contains(groupKey);
            widgets.add(buildGroupHeader(account, sessions.length, groupKey, isGroupCollapsed));
            if (!isGroupCollapsed) {
              for (final session in sessions) {
                widgets.add(buildUnifiedSessionItem(session));
              }
            }
          }
        }
      }
    }

    // Group emails by account - 一级分组
    final accountEmails = <String, List<native.EmailMessage>>{};
    for (final email in emails) {
      final account = email.account.isNotEmpty ? email.account : (email.recipient.isNotEmpty ? email.recipient : AppStrings.unknownAccount);
      accountEmails.putIfAbsent(account, () => []).add(email);
    }

    for (final accountEntry in accountEmails.entries) {
      final account = accountEntry.key;
      final emailsList = accountEntry.value;

      final sectionKey = 'account:$account';
      final isSectionCollapsed = collapsedSections.contains(sectionKey);
      widgets.add(buildSectionHeader(account, emailsList.length, sectionKey, isSectionCollapsed));

      if (!isSectionCollapsed) {
        // 二级分组：收件箱
        final inboxEmails = emailsList.where((e) => e.folder != 'Sent' && e.folder != 'SENT').toList();
        if (inboxEmails.isNotEmpty) {
          final groupKey = '$sectionKey:inbox';
          final isGroupCollapsed = collapsedGroups.contains(groupKey);
          widgets.add(buildGroupHeader(AppStrings.inbox, inboxEmails.length, groupKey, isGroupCollapsed));
          if (!isGroupCollapsed) {
            for (final email in inboxEmails) {
              final index = emails.indexWhere((e) => e.uuid == email.uuid);
              if (index >= 0) widgets.add(buildEmailItem(index));
            }
          }
        }

        // 二级分组：已发送
        final sentEmails = emailsList.where((e) => e.folder == 'Sent' || e.folder == 'SENT' || e.folder == 'Sent Messages').toList();
        if (sentEmails.isNotEmpty) {
          final groupKey = '$sectionKey:sent';
          final isGroupCollapsed = collapsedGroups.contains(groupKey);
          widgets.add(buildGroupHeader(AppStrings.sent, sentEmails.length, groupKey, isGroupCollapsed));
          if (!isGroupCollapsed) {
            for (final email in sentEmails) {
              final index = emails.indexWhere((e) => e.uuid == email.uuid);
              if (index >= 0) widgets.add(buildEmailItem(index));
            }
          }
        }
      }
    }

    return widgets;
  }

  Widget buildGroupHeader(String title, int count, String key, bool isCollapsed) {
    return GestureDetector(
      onTap: () {
        setState(() {
          if (isCollapsed) {
            collapsedGroups.remove(key);
          } else {
            collapsedGroups.add(key);
          }
        });
      },
      child: Container(
        padding: const EdgeInsets.fromLTRB(28, 8, 8, 4),
        color: const Color(0xFFE3F2FD),
        child: Row(
          children: [
            Text(title, style: TextStyle(fontSize: 12, fontWeight: FontWeight.w500, color: Colors.grey[700])),
            const SizedBox(width: 6),
            Text('$count', style: TextStyle(fontSize: 11, color: Colors.grey[500])),
            const Spacer(),
            Icon(isCollapsed ? Icons.keyboard_arrow_right : Icons.keyboard_arrow_down, size: 16, color: Colors.grey[500]),
          ],
        ),
      ),
    );
  }

  Widget buildSectionHeader(String title, int count, String key, bool isCollapsed) {
    return GestureDetector(
      onTap: () {
        setState(() {
          if (isCollapsed) {
            collapsedSections.remove(key);
          } else {
            collapsedSections.add(key);
          }
        });
      },
      child: Container(
        padding: const EdgeInsets.fromLTRB(16, 12, 8, 8),
        color: const Color(0xFFBBDEFB),
        child: Row(
          children: [
            Text(title, style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey[800])),
            const SizedBox(width: 8),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
              decoration: BoxDecoration(color: Colors.grey[300], borderRadius: BorderRadius.circular(8)),
              child: Text('$count', style: TextStyle(fontSize: 11, color: Colors.grey[600])),
            ),
            const Spacer(),
            Icon(isCollapsed ? Icons.keyboard_arrow_right : Icons.keyboard_arrow_down, size: 18, color: Colors.grey[600]),
          ],
        ),
      ),
    );
  }

  void _showCreateSessionDialog() {
    final config = native.EmailCore.loadConfig(configPath);
    final accounts = config?.accounts
            .where((a) => a.email.isNotEmpty)
            .map((a) => a.email)
            .toList() ??
        <String>[];
    if (accounts.isEmpty) {
      showDialog(
        context: context,
        builder: (context) => EmailConfigDialog(
          configPath: configPath,
          onDone: () async {
            fetchEmailsFromAccounts();
          },
        ),
      );
      return;
    }
    showDialog(
      context: context,
      builder: (context) => CreateSessionDialog(
        accounts: accounts,
        configPath: configPath,
        onCreated: () => refreshEmails(),
      ),
    );
  }


  Widget _buildUnifiedSectionHeader(String title, int count, String key, bool isCollapsed) {
    return GestureDetector(
      onTap: () {
        setState(() {
          if (isCollapsed) {
            collapsedSections.remove(key);
          } else {
            collapsedSections.add(key);
          }
        });
      },
      child: Container(
        padding: const EdgeInsets.fromLTRB(16, 12, 8, 8),
        color: const Color(0xFFBBDEFB),
        child: Row(
          children: [
            Icon(Icons.chat, size: 16, color: Colors.blue[800]),
            const SizedBox(width: 6),
            Text(title, style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: Colors.grey[800])),
            const SizedBox(width: 8),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
              decoration: BoxDecoration(color: Colors.grey[300], borderRadius: BorderRadius.circular(8)),
              child: Text('$count', style: TextStyle(fontSize: 11, color: Colors.grey[600])),
            ),
            const Spacer(),
            GestureDetector(
              onTap: () => _showCreateSessionDialog(),
              child: Container(
                padding: const EdgeInsets.all(2),
                child: Icon(Icons.add, size: 18, color: Colors.blue[700]),
              ),
            ),
            const SizedBox(width: 4),
            Icon(isCollapsed ? Icons.keyboard_arrow_right : Icons.keyboard_arrow_down, size: 18, color: Colors.grey[600]),
          ],
        ),
      ),
    );
  }

  Widget buildUnifiedSessionItem(UnifiedSessionInfo session) {
    final isSelected = selectedUnifiedSessionId == session.sessionId;
    final memberCount = session.members.length;
    final isGroup = memberCount > 2;

    // Title: subject if available, otherwise member names
    String title = session.subject.isNotEmpty ? session.subject : '';
    if (title.isEmpty) {
      final names = session.members.where((m) => m != session.account).take(3).toList();
      title = names.join(', ');
      if (memberCount - 1 > 3) title += ' ...';
    }
    if (title.isEmpty) title = AppStrings.noSubject;

    return GestureDetector(
      onTap: () {
        setState(() {
          selectedUnifiedSessionId = session.sessionId;
          loadUnifiedSessionMessages(session.sessionId);
        });
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: isSelected ? const Color(0xFFE3F2FD) : Colors.transparent,
          border: Border(bottom: BorderSide(color: Colors.grey[200]!, width: 0.5)),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            CircleAvatar(
              radius: 18,
              backgroundColor: isGroup ? Colors.green[100] : Colors.blue[100],
              child: Icon(
                isGroup ? Icons.group : Icons.person,
                size: 18,
                color: isGroup ? Colors.green[700] : Colors.blue[700],
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500, color: Colors.grey[800]),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    '$memberCount${AppStrings.isZh ? "人" : " members"} · ${session.updatedAt}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 12, color: Colors.grey[500]),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget buildSearchBar() {
    return Container(
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 6),
      child: Container(
        height: 32,
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(color: Colors.grey[300]!, width: 0.5),
        ),
        child: TextField(
          controller: searchController,
          onChanged: (value) {
            searchQuery = value;
            refreshEmails();
          },
          decoration: InputDecoration(
            hintText: AppStrings.search,
            hintStyle: TextStyle(fontSize: 13, color: Colors.grey[400]),
            prefixIcon: Icon(Icons.search, size: 18, color: Colors.grey[400]),
            prefixIconConstraints: const BoxConstraints(minWidth: 36),
            border: InputBorder.none,
            contentPadding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
          ),
        ),
      ),
    );
  }

  Widget buildDraggableDivider() {
    return MouseRegion(
      cursor: SystemMouseCursors.resizeColumn,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onHorizontalDragUpdate: (details) {
          setState(() {
            listWidth = (listWidth + details.delta.dx).clamp(240.0, 600.0);
          });
        },
        child: Container(
          width: 6,
          height: double.infinity,
          color: Colors.transparent,
          child: Center(
            child: Container(width: 1, height: double.infinity, color: Colors.grey[300]),
          ),
        ),
      ),
    );
  }

  Widget buildEmailItem(int index) {
    final email = emails[index];
    final isSelected = selectedEmail == index;
    final unread = unreadIndices.contains(index);
    final displayName = extractName(email.sender);

    return GestureDetector(
      onTap: () {
        setState(() {
          selectedEmail = index;
          selectedUnifiedSessionId = null;
          unreadIndices.remove(index);
        });
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: isSelected ? const Color(0xFFE8F0FE) : Colors.transparent,
          border: Border(bottom: BorderSide(color: Colors.grey[200]!, width: 0.5)),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: SizedBox(
                width: 8,
                child: unread
                    ? Container(width: 8, height: 8, decoration: BoxDecoration(color: Theme.of(context).colorScheme.primary, shape: BoxShape.circle))
                    : null,
              ),
            ),
            const SizedBox(width: 8),
            buildAvatar(displayName, email: email.sender),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Expanded(child: Text(displayName, style: TextStyle(fontSize: 13, fontWeight: unread ? FontWeight.w600 : FontWeight.normal, color: Colors.grey[900]), maxLines: 1, overflow: TextOverflow.ellipsis)),
                      const SizedBox(width: 8),
                      Text(formatTimeShort(email.timestamp), style: TextStyle(fontSize: 11, color: unread ? Theme.of(context).colorScheme.primary : Colors.grey[500], fontWeight: unread ? FontWeight.w500 : FontWeight.normal)),
                    ],
                  ),
                  const SizedBox(height: 2),
                  Row(
                    children: [
                      Expanded(child: Text(email.subject.isEmpty ? AppStrings.noSubject : email.subject, style: TextStyle(fontSize: 12, fontWeight: unread ? FontWeight.w600 : FontWeight.normal, color: Colors.grey[800]), maxLines: 1, overflow: TextOverflow.ellipsis)),
                      if (hasAttachment(email.body))
                        Padding(padding: const EdgeInsets.only(left: 4), child: Icon(Icons.attach_file, size: 12, color: Colors.grey[500])),
                    ],
                  ),
                  const SizedBox(height: 2),
                  Text(previewFor(email), style: TextStyle(fontSize: 11, color: Colors.grey[500]), maxLines: 1, overflow: TextOverflow.ellipsis),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget buildAvatar(String name, {String? email}) {
    String displayName = name.isEmpty ? '?' : name[0];
    if (email != null && email.isNotEmpty) {
      final abName = ConversationViewMixin.addressbookNameForEmail(email);
      if (abName.isNotEmpty) {
        displayName = abName[0];
      }
    }
    return Container(
      width: 32,
      height: 32,
      decoration: BoxDecoration(color: avatarColor(name), borderRadius: BorderRadius.circular(16)),
      child: Center(child: Text(displayName, style: const TextStyle(color: Colors.white, fontSize: 13, fontWeight: FontWeight.w500))),
    );
  }
}
