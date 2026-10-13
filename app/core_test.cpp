// Checks for core behaviour that needs a live window: Tab focus order and VirtualList binding.
// Run: build/<preset>/app/Core_Test (prints "core_test: ok").
#include <terracotta2/Terracotta2.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
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

    // Menus: a focused Dropdown opens with Down, arrows move, Enter picks, focus stays on the dropdown.
    Dropdown& drop = root.add<Dropdown>(std::vector<std::string>{"one", "two", "three"}, 0);
    int picked = -1;
    drop.onChange([&](int i) { picked = i; });
    win->renderFrame();
    win->setFocus(&drop);
    key(Key::Down);                          // opens the list, "one" highlighted
    CHECK(Overlay::of(drop).isOpen() && win->focusedWidget() == &drop); // the list is its own window; keys are forwarded to it
    CHECK(Overlay::of(drop).popupWindow() != win);
    key(Key::Down); key(Key::Down); key(Key::Enter);
    CHECK(picked == 2 && drop.value() == 2 && !Overlay::of(drop).isOpen() && win->focusedWidget() == &drop);
    // The popup window sizes itself to its content and sits just below the dropdown, on screen.
    key(Key::Space);
    Window* popup = Overlay::of(drop).popupWindow();
    popup->renderFrame();
    const SkRect pf = popup->screenFrame();
    const SkPoint below = win->toScreen({drop.bounds().left(), drop.bounds().bottom()});
    CHECK(pf.width() >= 180 && pf.height() > 3 * theme().popup.itemHeight && pf.height() < 4 * theme().popup.itemHeight + 20);
    CHECK(std::abs(pf.left() - below.x()) < 1 && std::abs(pf.top() - (below.y() + 2)) < 1);
    // A press in another window closes it and is swallowed: had it reached the dropdown, the list would reopen.
    win->injectMouseButton(drop.bounds().centerX(), drop.bounds().centerY(), MouseButton::Left, true);
    win->injectMouseButton(drop.bounds().centerX(), drop.bounds().centerY(), MouseButton::Left, false);
    CHECK(!Overlay::of(drop).isOpen());
    app.pumpOnce(false);                     // the closed popup window is destroyed at the end of the iteration
    key(Key::Space); key(Key::Escape);       // Escape closes without picking
    CHECK(picked == 2 && !Overlay::of(drop).isOpen() && win->focusedWidget() == &drop);

    root.clearChildren();

    // Tooltips: shown after the pointer rests on a widget, hidden by a press, not re-shown until the pointer moves on.
    appTheme().tooltip.delaySec = 0.05f;
    Button& tipped = root.add<Button>("hover me").tooltip("Saves the file");
    root.add<Button>("plain");
    win->renderFrame();
    auto tipWindow = [&]() -> Window* { // the tip is its own popup window: root > box > label
        for (Window* w : app.windows())
            if (w != win && !w->root()->children().empty() && !w->root()->children()[0]->children().empty())
                if (auto* l = dynamic_cast<Label*>(w->root()->children()[0]->children()[0].get()); l && l->text() == "Saves the file")
                    return w;
        return nullptr;
    };
    auto tipShown = [&] { return tipWindow() != nullptr; };
    // Frames tick the delay. No OS event polling here: the real mouse cursor would move the hover.
    auto wait = [&] {
        for (int i = 0; i < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            for (Window* w : app.windows()) w->renderFrame();
        }
    };
    const SkRect tb = tipped.bounds();
    win->injectMouseMove(tb.centerX(), tb.centerY());
    CHECK(!tipShown());
    wait();
    CHECK(tipShown());
    CHECK(std::abs(tipWindow()->screenFrame().top() - (win->toScreen({0, tb.centerY()}).y() + 20)) < 1); // 20 px below the pointer
    win->injectMouseButton(tb.centerX(), tb.centerY(), MouseButton::Left, true);
    win->injectMouseButton(tb.centerX(), tb.centerY(), MouseButton::Left, false);
    app.pumpOnce(false); // destroys the closed tip window
    wait();
    CHECK(!tipShown()); // a press hides it; resting on the same widget doesn't bring it back
    win->injectMouseLeave();
    win->injectMouseMove(tb.centerX(), tb.centerY());
    wait();
    CHECK(tipShown());

    // Custom titlebar: presses on the bar's empty or plain parts go to the OS window drag, not to
    // widgets; widgets that take input inside it (a button) still get them.
    Button* goButton = nullptr;
    struct PressCounter : Widget {
        int presses = 0;
        PressCounter() { size(40, 20); }
        bool onMouseDown(const MouseEvent&) override { ++presses; return true; }
    };
    Window* bar = app.createWindow({.title = "titlebar", .width = 400, .height = 200, .customTitlebar = true});
    PressCounter* plain = nullptr;
    int clicked = 0;
    bar->root()->add<TitleBar>().build([&](TitleBar& tb) {
        plain = &tb.add<PressCounter>();
        tb.add<Button>("Go").onClick([&] { ++clicked; }).ref(goButton);
    });
    bar->renderFrame();
    auto click = [&](const SkRect& r) {
        bar->injectMouseButton(r.centerX(), r.centerY(), MouseButton::Left, true);
        bar->injectMouseButton(r.centerX(), r.centerY(), MouseButton::Left, false);
    };
    click(plain->bounds());
    CHECK(plain->presses == 0); // window drag area
    click(goButton->bounds());
    CHECK(clicked == 1);

    // Inspector highlight: a padding change repaints the whole overlay (its padding/content edge
    // moves although the widget's bounds do not).
    Window* hl = app.createWindow({.title = "highlight", .width = 300, .height = 200});
    Panel& boxed = hl->root()->add<Panel>().size(200, 120).padding(10);
    boxed.add<Label>("inside");
    hl->setHighlight(&boxed);
    hl->renderFrame();
    boxed.padding(40);
    hl->renderFrame();
    CHECK(hl->lastFrameStats().damage.contains(boxed.bounds()));
    hl->close();

    // Designer: docks to the right of the host's own UI, which moves (alive, unchanged) into the
    // central node of a new DockSpace; keeps its own theme; closing it collapses the dock again.
    Window* host = app.createWindow({.title = "host", .width = 900, .height = 500});
    Label& appUi = host->root()->add<Label>("app ui");
    host->renderFrame();
    Widget* designer = openDesigner(host);
    DockSpace* dock = DockSpace::of(host);
    CHECK(designer && dock && dock->centralNode() && dock->centralNode()->isAncestorOf(&appUi));
    CHECK(openDesigner(host) == designer); // one at a time
    host->renderFrame();
    CHECK(designer->bounds().left() > appUi.bounds().left() + 300 && designer->bounds().width() > 200);
    const SkColor panelColor = appTheme().panelBackground;
    appTheme().panelBackground = SK_ColorRED;
    applyTheme();
    { WidgetThemeScope scope(designer); CHECK(theme().panelBackground != SK_ColorRED); }
    { WidgetThemeScope scope(&appUi); CHECK(theme().panelBackground == SK_ColorRED); }
    appTheme().panelBackground = panelColor;
    applyTheme();
    // Drag its tab out of every window: it floats, keeping its theme; drop it on the app UI's right
    // edge: it docks back. The central node itself never takes it as a tab.
    std::function<Widget*(Widget*)> findTab = [&](Widget* w) -> Widget* {
        if (auto* l = dynamic_cast<Label*>(w); l && l->text() == "Designer") return w->parent();
        for (auto& c : w->children()) if (Widget* r = findTab(c.get())) return r;
        return nullptr;
    };
    Widget* tabButton = findTab(host->root());
    CHECK(tabButton);
    const SkRect tabRect = tabButton->bounds();
    host->injectMouseMove(tabRect.centerX(), tabRect.centerY());
    host->injectMouseButton(tabRect.centerX(), tabRect.centerY(), MouseButton::Left, true);
    host->injectMouseMove(tabRect.centerX() + 20, tabRect.centerY());  // starts the tab drag
    host->injectMouseMove(-5000, -5000);                     // outside every window: floats
    Window* floating = designer->window();
    CHECK(floating && floating != host && floating->isFloating());
    { WidgetThemeScope scope(designer); CHECK(&theme() == designer->ownTheme()); }
    host->injectMouseButton(-5000, -5000, MouseButton::Left, false); // dropped nowhere: stays floating
    CHECK(designer->window() == floating);
    // Back by the empty part of the floating window's tab bar: a tab drag (an OS window move could not dock).
    floating->renderFrame(); // lays it out
    Widget* floatTab = findTab(floating->root());
    const SkRect tabBar = floatTab->parent()->bounds();
    const SkPoint grabAt{std::min(tabBar.right() - 4, floatTab->bounds().right() + 30), tabBar.centerY()};
    floating->injectMouseMove(grabAt.x(), grabAt.y());
    floating->injectMouseButton(grabAt.x(), grabAt.y(), MouseButton::Left, true);
    floating->injectMouseMove(grabAt.x() + 20, grabAt.y()); // past the threshold: the tab drag starts
    const SkRect central = dock->centralNode()->bounds();
    host->injectMouseMove(central.right() - 10, central.centerY()); // right edge band of the app UI
    host->injectMouseButton(central.right() - 10, central.centerY(), MouseButton::Left, false);
    app.pumpOnce(false); // the emptied floating window closes
    CHECK(designer->window() == host && !dock->rootNode()->isLeaf() && dock->centralNode()->isAncestorOf(&appUi));

    closeDesigner();
    app.pumpOnce(false); // destroyed after the current event
    CHECK(dock->rootNode()->isCentral() && dock->centralNode()->isAncestorOf(&appUi));
    CHECK(openDesigner(host) != nullptr); // reopens beside the same central node
    closeDesigner();
    app.pumpOnce(false);

    // themeOverride: only that widget's subtree changes, and it still follows the app theme otherwise.
    {
        Window* tw = app.createWindow({.title = "override", .width = 300, .height = 100});
        Button& plainBtn = tw->root()->add<Button>("plain");
        Button& pill = tw->root()->add<Button>("pill").themeOverride([](Theme& t) { t.button.cornerRadius = 999; });
        { WidgetThemeScope scope(&pill); CHECK(theme().button.cornerRadius == 999); }
        { WidgetThemeScope scope(&plainBtn); CHECK(theme().button.cornerRadius != 999); }
        const SkColor oldText = appTheme().button.text;
        appTheme().button.text = SK_ColorRED;
        applyTheme();
        { WidgetThemeScope scope(&pill); CHECK(theme().button.text == SK_ColorRED && theme().button.cornerRadius == 999); }
        appTheme().button.text = oldText;
        applyTheme();
        tw->close();
    }

    // Timers: a blocking loop iteration sleeps only until the next timer, which then runs; a
    // cancelled one never does.
    {
        bool fired = false, cancelledFired = false;
        const int dead = app.setTimer(0.01, [&] { cancelledFired = true; });
        app.setTimer(0.02, [&] { fired = true; });
        app.cancelTimer(dead);
        const auto start = std::chrono::steady_clock::now();
        while (!fired && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) app.pumpOnce(true);
        CHECK(fired && !cancelledFired);
    }

    std::printf("core_test: ok\n");
}
