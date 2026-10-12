import 'package:emoji_picker_flutter/emoji_picker_flutter.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:oim_mobile/conversation_page.dart';
import 'package:oim_core/native/unified_session.dart';

Widget _app() => MaterialApp(
      home: ConversationPage(
        session: UnifiedSessionInfo(
          sessionId: 's1',
          account: 'a@test.com',
          subject: 'Test',
          members: const ['a@test.com', 'b@test.com'],
        ),
      ),
    );

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  // EmojiPicker reads recent-emoji from SharedPreferences (a platform
  // channel); return empty storage so its internal init completes in tests.
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
    const MethodChannel('plugins.flutter.io/shared_preferences'),
    (call) async => call.method.startsWith('getAll')
        ? <String, dynamic>{}
        : call.method.startsWith('set') ||
                call.method == 'remove' ||
                call.method == 'clear'
            ? true
            : null,
  );

  testWidgets('input bar: text field above, tools row below', (tester) async {
    await tester.pumpWidget(_app());
    await tester.pump();

    // Text input occupies its own row inside the bordered input card.
    final field = find.byType(TextField);
    expect(field, findsOneWidget);
    final fieldTop = tester.getTopLeft(field).dy;

    // Tools row: folder + emoji left, send TextButton right — all below.
    final attach = find.byIcon(Icons.attach_file);
    final emoji = find.byIcon(Icons.emoji_emotions_outlined);
    final send = find.text('Send');
    for (final f in [attach, emoji, send]) {
      expect(f, findsOneWidget);
      expect(tester.getTopLeft(f).dy, greaterThan(fieldTop));
    }

    // Left group ordered attach then emoji; send is rightmost.
    final attachX = tester.getTopLeft(attach).dx;
    final emojiX = tester.getTopLeft(emoji).dx;
    final sendX = tester.getTopLeft(send).dx;
    expect(attachX, lessThan(emojiX));
    expect(sendX, greaterThan(emojiX));

    // Emoji panel hidden initially.
    expect(find.byType(EmojiPicker), findsNothing);
  });

  testWidgets('emoji button toggles panel; icon switches to filled',
      (tester) async {
    await tester.pumpWidget(_app());
    await tester.pump();

    await tester.tap(find.byIcon(Icons.emoji_emotions_outlined));
    await tester.pump();

    expect(find.byType(EmojiPicker), findsOneWidget);
    expect(find.byIcon(Icons.emoji_emotions), findsOneWidget);

    // Tapping the (now filled) button closes the panel.
    await tester.tap(find.byIcon(Icons.emoji_emotions));
    await tester.pumpAndSettle();
    expect(find.byType(EmojiPicker), findsNothing);
    expect(find.byIcon(Icons.emoji_emotions_outlined), findsOneWidget);
  });

  testWidgets('selecting emoji inserts at cursor', (tester) async {
    await tester.pumpWidget(_app());
    await tester.pump();

    await tester.enterText(find.byType(TextField), 'hi');
    await tester.tap(find.byIcon(Icons.emoji_emotions_outlined));
    await tester.pumpAndSettle(const Duration(milliseconds: 100));

    // Default tab is RECENT (empty); switch to the smileys category.
    final ep = find.byType(EmojiPicker);
    await tester.tap(find.descendant(of: ep, matching: find.byType(Icon)).at(1));
    await tester.pumpAndSettle(const Duration(milliseconds: 100));

    // Pick the first emoji glyph (tapping its Text hits the cell's
    // GestureDetector).
    final glyphs = find.descendant(
      of: ep,
      matching: find.byWidgetPredicate(
          (w) => w is Text && (w.data?.isNotEmpty ?? false)),
    );
    expect(glyphs, findsWidgets);
    await tester.tap(glyphs.first);
    await tester.pump();

    final field = tester.widget<TextField>(find.byType(TextField));
    expect(field.controller!.text.length, greaterThan('hi'.length));
    expect(field.controller!.text.startsWith('hi'), isTrue);
  });

  testWidgets('keyboard inset pushes input bar up', (tester) async {
    await tester.pumpWidget(_app());
    await tester.pump();

    final before = tester.getTopLeft(find.text('Send')).dy;

    // Simulate an open keyboard via viewInsets.
    tester.view.viewInsets = const FakeViewPadding(bottom: 336);
    addTearDown(tester.view.resetViewInsets);
    await tester.pump();

    final after = tester.getTopLeft(find.text('Send')).dy;
    expect(after, lessThan(before));
  });
}
