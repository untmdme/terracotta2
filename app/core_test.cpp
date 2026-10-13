// Checks for core behaviour that needs a live window: Tab focus order and VirtualList binding.
// Run: build/<preset>/app/Core_Test (prints "core_test: ok").
#include <terracotta2/Terracotta2.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

// Not CHECK(): this must also fail in release builds.
#define CHECK(x) do { if (!(x)) { std::printf("core_test: FAILED %s (line %d)\n", #x, __LINE__); std::exit(1); } } while (0)

using namespace Terracotta2;

int main() {
    Application app;
    Window* win = app.createWindow({.title = "core_test", .width = 400, .height = 300});
    Widget& root = *win->root();

    // Tab order: tree order, skipping hidden subtrees and selectable labels; Shift+Tab reverses; wraps.
    InputText& a = root.add<InputText>("a");
    root.add<Label>("text").selectable();
    root.add<VBox>().visible(false).add<InputText>("hidden");
    Button& b = root.add<Button>("b");
    Slider& s = root.add<Slider>();
    win->renderFrame();
    auto tab = [&](int mods = 0) { win->injectKey(Key::Tab, KeyAction::Press, mods); };
    tab(); CHECK(win->focusedWidget() == &a && win->focusVisible());
    tab(); CHECK(win->focusedWidget() == &b);
    tab(); CHECK(win->focusedWidget() == &s);
    tab(); CHECK(win->focusedWidget() == &a);
    tab(ModShift); CHECK(win->focusedWidget() == &s);
    win->injectKey(Key::Right, KeyAction::Press, 0); CHECK(s.value() > 0); // arrows reach the focused slider
    root.clearChildren();

    // VirtualList: only rows in view exist, each bound to the right item, rebound on scroll.
    VirtualList& list = root.add<VirtualList>(20).rows([](Widget& p) -> Widget& { return p.add<Label>(""); },
                                                       [](Widget& row, int i) { static_cast<Label&>(row).text(std::to_string(i)); });
    list.count(10000);
    win->renderFrame();
    auto shown = [&](int item) {
        for (auto& r : list.children()[0]->children())
            if (r->isVisible() && static_cast<Label&>(*r).text() == std::to_string(item)) return true;
        return false;
    };
    CHECK(list.children()[0]->children().size() < 30);
    CHECK(shown(0) && shown(10) && !shown(100));
    list.scrollToRow(5000);
    win->renderFrame();
    CHECK(shown(5000) && !shown(0));

    root.clearChildren();

    // TextArea: Enter inserts a newline, Up/Down/Home/End move by line, the field grows with its text.
    TextArea& area = root.add<TextArea>().rows(2);
    win->renderFrame();
    const float h0 = area.bounds().height();
    win->setFocus(&area);
    auto type = [&](const char* s) { for (; *s; ++s) win->injectChar(uint32_t(*s)); };
    auto key = [&](Key k, int mods = 0) { win->injectKey(k, KeyAction::Press, mods); };
    type("abc"); key(Key::Enter); type("de"); key(Key::Enter); type("f"); key(Key::Enter); type("g");
    CHECK(area.value() == "abc\nde\nf\ng");
    win->renderFrame();
    CHECK(area.bounds().height() > h0); // 4 lines > rows(2)
    key(Key::Up); key(Key::Up); type("X");     // two lines up from "g": lands on the "de" line
    CHECK(area.value().find('X') != std::string::npos && area.value().find('X') < area.value().find('f'));
    key(Key::Home); type("<");
    key(Key::End); type(">");
    const std::string v = area.value();
    const size_t ln = v.find('<');
    CHECK(ln != std::string::npos && (ln == 0 || v[ln - 1] == '\n')); // Home went to the line start
    CHECK(v[v.find('>') + 1] == '\n');                                 // End went to the line end
    // Caret right after Enter sits at the start of the new (empty) line: Up from there reaches line 1's start.
    TextArea& fresh = root.add<TextArea>().width(300);
    win->renderFrame();
    win->setFocus(&fresh);
    type("abc"); key(Key::Enter); key(Key::Up); type("#");
    CHECK(fresh.value() == "#abc\n");
    InputText& single = root.add<InputText>();
    win->setFocus(&single);
    type("x"); key(Key::Enter);
    CHECK(single.value() == "x"); // single line: Enter is not text

    // Long single-line text scrolls to follow the caret, so clicks land on the text shown.
    InputText& longField = root.add<InputText>().width(120);
    win->renderFrame();
    win->setFocus(&longField);
    for (int i = 0; i < 6; ++i) type("0123456789");
    win->renderFrame();
    const SkRect lb = longField.bounds();
    auto clickAt = [&](float x) {
        win->injectMouseButton(x, lb.centerY(), MouseButton::Left, true);
        win->injectMouseButton(x, lb.centerY(), MouseButton::Left, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(450)); // not a double click
    };
    clickAt(lb.left() + 8); type("X"); // the field shows the end, so its left edge is far into the text
    CHECK(longField.value().find('X') > 30);
    key(Key::Home); win->renderFrame();
    clickAt(lb.right() - 8); type("Y"); // scrolled back to the start
    CHECK(longField.value().find('Y') < 25);

    // Word jumps (Option/Ctrl+arrows) and word delete, in single- and multi-line fields.
    InputText& words = root.add<InputText>().width(300);
    win->setFocus(&words);
    type("alpha beta, gamma");
    key(Key::Left, ModAlt); type("[");            // before "gamma"
    key(Key::Left, ModAlt); type("<");            // before "beta": "[" and ", " are skipped, not words
    key(Key::Right, ModCtrl); type(">");          // after "beta"
    CHECK(words.value() == "alpha <beta>, [gamma");
    key(Key::End); key(Key::Backspace, ModAlt);   // deletes "gamma"
    CHECK(words.value() == "alpha <beta>, [");
    TextArea& wordsArea = root.add<TextArea>();
    win->setFocus(&wordsArea);
    type("one two"); key(Key::Enter); type("three");
    key(Key::Left, ModAlt); key(Key::Left, ModAlt); type("|"); // crosses the newline to before "two"
    CHECK(wordsArea.value() == "one |two\nthree");

    // maxRows: the TextArea stops growing and scrolls instead; the wheel scrolls it, the caret is followed.
    TextArea& capped = root.add<TextArea>().rows(2).maxRows(3).width(300);
    win->setFocus(&capped);
    for (int i = 0; i < 10; ++i) { type("line"); if (i < 9) key(Key::Enter); }
    win->renderFrame();
    const SkRect cb = capped.bounds();
    CHECK(cb.height() < 4 * theme().label.fontSize * 1.6f); // ~3 lines tall, not 10
    auto clickTop = [&] {
        win->injectMouseButton(cb.left() + 20, cb.top() + 8, MouseButton::Left, true);
        win->injectMouseButton(cb.left() + 20, cb.top() + 8, MouseButton::Left, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        type("#");
        return capped.value().find('#');
    };
    CHECK(clickTop() > 20);                 // scrolled to the caret at the end: the top shows a late line
    win->injectScroll(0, 1000); win->renderFrame();
    CHECK(clickTop() < 5);                  // wheeled back to the start: the top shows the first line

    root.clearChildren();

    // Menus: a focused Dropdown opens with Down, arrows move, Enter picks, focus comes back.
    Dropdown& drop = root.add<Dropdown>(std::vector<std::string>{"one", "two", "three"}, 0);
    int picked = -1;
    drop.onChange([&](int i) { picked = i; });
    win->renderFrame();
    win->setFocus(&drop);
    key(Key::Down);                          // opens the list, "one" highlighted
    CHECK(Overlay::of(drop).isOpen() && win->focusedWidget() != &drop);
    key(Key::Down); key(Key::Down); key(Key::Enter);
    CHECK(picked == 2 && drop.value() == 2 && !Overlay::of(drop).isOpen() && win->focusedWidget() == &drop);
    key(Key::Space); key(Key::Escape);       // Escape closes without picking
    CHECK(picked == 2 && !Overlay::of(drop).isOpen() && win->focusedWidget() == &drop);

    root.clearChildren();

    // Tooltips: shown after the pointer rests on a widget, hidden by a press, not re-shown until the pointer moves on.
    appTheme().tooltip.delaySec = 0.05f;
    Button& tipped = root.add<Button>("hover me").tooltip("Saves the file");
    root.add<Button>("plain");
    win->renderFrame();
    auto tipShown = [&] {
        for (auto& c : root.children())
            if (c->isVisible() && !c->children().empty())
                if (auto* l = dynamic_cast<Label*>(c->children()[0].get()); l && l->text() == "Saves the file") return true;
        return false;
    };
    auto wait = [&] { for (int i = 0; i < 3; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(40)); win->renderFrame(); } };
    const SkRect tb = tipped.bounds();
    win->injectMouseMove(tb.centerX(), tb.centerY());
    CHECK(!tipShown());
    wait();
    CHECK(tipShown());
    win->injectMouseButton(tb.centerX(), tb.centerY(), MouseButton::Left, true);
    win->injectMouseButton(tb.centerX(), tb.centerY(), MouseButton::Left, false);
    wait();
    CHECK(!tipShown()); // a press hides it; resting on the same widget doesn't bring it back
    win->injectMouseLeave();
    win->injectMouseMove(tb.centerX(), tb.centerY());
    wait();
    CHECK(tipShown());

    std::printf("core_test: ok\n");
}
