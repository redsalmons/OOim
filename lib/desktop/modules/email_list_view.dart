import 'package:flutter/material.dart';
import '../../native/email_core.dart' as native;
import '../../native/unified_session.dart';
import 'email_module_base.dart';
import '../dialogs/email_config_dialog.dart';
import '../dialogs/create_session_dialog.dart';
import '../../i18n/app_strings.dart';
import '../app_theme.dart';

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
      color: context.oim.listPane,
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

  String _sessionTitle(UnifiedSessionInfo session) {
    String title = session.subject.isNotEmpty ? session.subject : '';
    if (title.isEmpty) {
      final names = session.members.where((m) => m != session.account).take(3).toList();
      title = names.join(', ');
      if (session.members.length - 1 > 3) title += ' ...';
    }
    return title;
  }

  List<Widget> buildGroupedEmailList() {
    final List<Widget> widgets = [];
    final q = searchQuery.trim().toLowerCase();

    // Sessions whose title or any member matches the query.
    final filteredSessions = q.isEmpty
        ? unifiedSessions
        : unifiedSessions
            .where((s) =>
                _sessionTitle(s).toLowerCase().contains(q) ||
                s.members.any((m) => m.toLowerCase().contains(q)))
            .toList();

    // Section 0: Unified sessions — grouped by account
    {
      final sectionKey = 'unified_sessions';
      final isCollapsed = collapsedSections.contains(sectionKey);
      widgets.add(_buildUnifiedSectionHeader(
          AppStrings.conversation, filteredSessions.length, sectionKey, isCollapsed));
      if (!isCollapsed) {
        if (filteredSessions.isEmpty) {
          widgets.add(Container(
            padding: const EdgeInsets.fromLTRB(28, 6, 8, 6),
            child: Text(AppStrings.noConversations, style: TextStyle(fontSize: 12, color: context.oim.textMuted)),
          ));
        } else {
          // 三个子 section：置顶（上）/ 会话（中）/ 隐藏（下）。隐藏优先于置顶。
          final pinnedList = filteredSessions.where((s) => s.pinned && !s.hidden).toList();
          final normalList = filteredSessions.where((s) => !s.pinned && !s.hidden).toList();
          final hiddenList = filteredSessions.where((s) => s.hidden).toList();

          if (pinnedList.isNotEmpty) {
            const key = 'unified:pinned';
            final collapsed = collapsedGroups.contains(key);
            widgets.add(buildGroupHeader(AppStrings.pinnedSection, pinnedList.length, key, collapsed));
            if (!collapsed) {
              for (final session in pinnedList) {
                widgets.add(buildUnifiedSessionItem(session));
              }
            }
          }

          {
            const key = 'unified:normal';
            final collapsed = collapsedGroups.contains(key);
            widgets.add(buildGroupHeader(AppStrings.conversation, normalList.length, key, collapsed));
            if (!collapsed) {
              for (final session in normalList) {
                widgets.add(buildUnifiedSessionItem(session));
              }
            }
          }

          if (hiddenList.isNotEmpty) {
            const key = 'unified:hidden';
            final collapsed = collapsedGroups.contains(key);
            widgets.add(buildGroupHeader(AppStrings.hiddenSection, hiddenList.length, key, collapsed));
            if (!collapsed) {
              for (final session in hiddenList) {
                widgets.add(buildUnifiedSessionItem(session));
              }
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
        color: context.oim.groupHeader,
        child: Row(
          children: [
            Text(title, style: TextStyle(fontSize: 12, fontWeight: FontWeight.w500, color: context.oim.textSecondary)),
            const SizedBox(width: 6),
            Text('$count', style: TextStyle(fontSize: 11, color: context.oim.textMuted)),
            const Spacer(),
            Icon(isCollapsed ? Icons.keyboard_arrow_right : Icons.keyboard_arrow_down, size: 16, color: context.oim.textMuted),
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
        color: context.oim.sectionHeader,
        child: Row(
          children: [
            Icon(Icons.chat, size: 16, color: context.scheme.primary),
            const SizedBox(width: 6),
            Text(title, style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: context.oim.textPrimary)),
            const SizedBox(width: 8),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
              decoration: BoxDecoration(color: context.oim.border, borderRadius: BorderRadius.circular(8)),
              child: Text('$count', style: TextStyle(fontSize: 11, color: context.oim.textSecondary)),
            ),
            const Spacer(),
            GestureDetector(
              onTap: () => _showCreateSessionDialog(),
              child: Container(
                padding: const EdgeInsets.all(2),
                child: Icon(Icons.add, size: 18, color: context.scheme.primary),
              ),
            ),
            const SizedBox(width: 4),
            Icon(isCollapsed ? Icons.keyboard_arrow_right : Icons.keyboard_arrow_down, size: 18, color: context.oim.textSecondary),
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
    String title = _sessionTitle(session);
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
          color: isSelected ? context.oim.selectedItem : Colors.transparent,
          border: Border(bottom: BorderSide(color: context.oim.border, width: 0.5)),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            CircleAvatar(
              radius: 18,
              backgroundColor: isGroup ? context.oim.avatarGroupBg : context.oim.avatarTheirsBg,
              child: Icon(
                isGroup ? Icons.group : Icons.person,
                size: 18,
                color: isGroup ? context.oim.avatarGroupFg : context.oim.avatarTheirsFg,
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
                    style: TextStyle(fontSize: 14, fontWeight: FontWeight.w500, color: context.oim.textPrimary),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    '$memberCount${AppStrings.isZh ? "人" : " members"} · ${session.updatedAt}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 12, color: context.oim.textMuted),
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
          color: context.oim.inputField,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(color: context.oim.border, width: 0.5),
        ),
        child: TextField(
          controller: searchController,
          onChanged: (value) {
            searchQuery = value;
            setState(() {});
          },
          decoration: InputDecoration(
            hintText: AppStrings.search,
            hintStyle: TextStyle(fontSize: 13, color: context.oim.textMuted),
            prefixIcon: Icon(Icons.search, size: 18, color: context.oim.textMuted),
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
            child: Container(width: 1, height: double.infinity, color: context.oim.border),
          ),
        ),
      ),
    );
  }

}
