import 'dart:convert';
import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import 'email_utils.dart';
import 'email_module_base.dart';
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
  List<native.EmailMessage> get conversationEmails;
  Set<String> get collapsedSections;
  Set<String> get collapsedGroups;
  int get selectedEmail;
  set selectedEmail(int v);
  String? get selectedConversationMessageId;
  set selectedConversationMessageId(String? v);
  bool get isConversationView;
  set isConversationView(bool v);
  Set<int> get unreadIndices;
  Map<String, int> get configIndexMap;
  String get configPath;

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
            child: emails.isEmpty
                ? Center(child: Text(AppStrings.noEmails, style: TextStyle(color: Colors.grey[400], fontSize: 14)))
                : ListView(
                    children: buildGroupedEmailList(),
                  ),
          ),
        ],
      ),
    );
  }

  List<Widget> buildGroupedEmailList() {
    final List<Widget> widgets = [];

    // Section 1: 会话 (conversations) - 一级分组
    {
      final sectionKey = 'conversations';
      final isCollapsed = collapsedSections.contains(sectionKey);
      widgets.add(buildConversationSectionHeader(AppStrings.conversation, conversationEmails.length, sectionKey, isCollapsed));
      if (!isCollapsed) {
        // 二级分组：按接收邮箱分组
        final accountConversations = <String, List<native.EmailMessage>>{};
        for (final email in conversationEmails) {
          final account = email.account.isNotEmpty ? email.account : (email.recipient.isNotEmpty ? email.recipient : AppStrings.unknownAccount);
          accountConversations.putIfAbsent(account, () => []).add(email);
        }

        for (final accountEntry in accountConversations.entries) {
          final account = accountEntry.key;
          final convList = accountEntry.value;

          final groupKey = 'conversations:$account';
          final isGroupCollapsed = collapsedGroups.contains(groupKey);
          widgets.add(buildGroupHeader(account, convList.length, groupKey, isGroupCollapsed));
          if (!isGroupCollapsed) {
            if (convList.isEmpty) {
              widgets.add(Container(
                padding: const EdgeInsets.fromLTRB(28, 6, 8, 6),
                child: Text(AppStrings.noConversations, style: TextStyle(fontSize: 12, color: Colors.grey[400])),
              ));
            } else {
              for (final email in convList) {
                widgets.add(buildConversationItem(email));
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

  Widget buildConversationSectionHeader(String title, int count, String key, bool isCollapsed) {
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

  Widget buildConversationItem(native.EmailMessage email) {
    final sessionId = email.sessionId.isNotEmpty ? email.sessionId : email.messageId;
    final isSelected = isConversationView && selectedConversationMessageId == sessionId;

    // Query unread count for this session
    final unreadCount = native.EmailCore.querySessionUnread(sessionId);

    // Extract all members from thread emails (sender + to_addr + recipient)
    final memberNames = <String>{};
    final thread = emails.where((e) => e.sessionId == sessionId).toList();
    for (final e in thread) {
      if (e.sender.isNotEmpty) memberNames.add(extractName(e.sender));
      if (e.toAddr.isNotEmpty) {
        for (final addr in e.toAddr.split(',')) {
          final trimmed = addr.trim();
          if (trimmed.isNotEmpty) memberNames.add(extractName(trimmed));
        }
      }
      if (e.recipient.isNotEmpty) memberNames.add(extractName(e.recipient));
    }
    // If still empty, use root email sender
    if (memberNames.isEmpty) {
      memberNames.add(extractName(email.sender));
    }
    
    // Convert to a List and limit to avoid UI overflow (e.g., max 3-4 names)
    var displayList = memberNames.toList();
    if (displayList.length > 3) {
      displayList = displayList.sublist(0, 3);
      displayList.add('${memberNames.length}${AppStrings.andMore}');
    }
    final displayName = displayList.join(', ');
    final index = emails.indexWhere((e) => e.uuid == email.uuid);
    final unread = index >= 0 && unreadIndices.contains(index);

    return GestureDetector(
      onSecondaryTapDown: (details) {
        showMenu<String>(
          context: context,
          position: RelativeRect.fromLTRB(
            details.globalPosition.dx,
            details.globalPosition.dy,
            details.globalPosition.dx,
            details.globalPosition.dy,
          ),
          items: [
            PopupMenuItem<String>(
              value: 'delete',
              child: Text(AppStrings.delete),
            ),
          ],
        ).then((value) {
          if (value == 'delete') {
            native.EmailCore.hideSession(sessionId);
            setState(() {
              conversationEmails.removeWhere((e) =>
                (e.sessionId.isNotEmpty ? e.sessionId : e.messageId) == sessionId);
            });
          }
        });
      },
      onTap: () {
        setState(() {
          selectedConversationMessageId = sessionId;
          isConversationView = true;
          if (index >= 0) {
            selectedEmail = index;
            unreadIndices.remove(index);
          }
        });
        // Mark session as read
        native.EmailCore.updateSessionRead(sessionId);
      },
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: isSelected ? const Color(0xFFE8F0FE) : Colors.transparent,
          border: Border(bottom: BorderSide(color: Colors.grey[200]!, width: 0.5)),
        ),
        child: Stack(
          clipBehavior: Clip.none,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                buildAvatar(displayName),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Expanded(child: Text(displayName, style: TextStyle(fontSize: 13, fontWeight: unreadCount > 0 ? FontWeight.w600 : FontWeight.normal, color: Colors.grey[800]), maxLines: 1, overflow: TextOverflow.ellipsis)),
                          Text(formatTime(email.timestamp), style: TextStyle(fontSize: 11, color: Colors.grey[500])),
                        ],
                      ),
                      const SizedBox(height: 2),
                      Text(email.subject.isEmpty ? AppStrings.noSubject : email.subject, style: TextStyle(fontSize: 12, fontWeight: unreadCount > 0 ? FontWeight.w600 : FontWeight.normal, color: Colors.grey[800]), maxLines: 1, overflow: TextOverflow.ellipsis),
                      const SizedBox(height: 2),
                      Text(previewFor(email), style: TextStyle(fontSize: 11, color: Colors.grey[500]), maxLines: 1, overflow: TextOverflow.ellipsis),
                    ],
                  ),
                ),
              ],
            ),
            if (unreadCount > 0)
              Positioned(
                top: -2,
                right: 0,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                  decoration: BoxDecoration(
                    color: Colors.red,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Text(
                    unreadCount > 99 ? '99+' : '$unreadCount',
                    style: const TextStyle(fontSize: 10, color: Colors.white, fontWeight: FontWeight.bold),
                  ),
                ),
              ),
          ],
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
          isConversationView = false;
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
            buildAvatar(displayName),
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

  Widget buildAvatar(String name) {
    final displayName = name.isEmpty ? '?' : name[0];
    return Container(
      width: 32,
      height: 32,
      decoration: BoxDecoration(color: avatarColor(name), borderRadius: BorderRadius.circular(16)),
      child: Center(child: Text(displayName, style: const TextStyle(color: Colors.white, fontSize: 13, fontWeight: FontWeight.w500))),
    );
  }
}
