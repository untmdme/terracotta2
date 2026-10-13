// Custom title bar: the window draws its own bar, with your widgets in it.
// Drag the bar to move the window, double-click it to maximize. Widgets that take input (the
// button) stay clickable. macOS keeps its native traffic lights; on Windows, WindowButtons draws
// minimize / maximize / close.
#include <terracotta2/Terracotta2.hpp>

using namespace Terracotta2;

int main() {
    Application app;
    Window* window = app.createWindow({.title = "Custom Title Bar", .width = 560, .height = 340, .minWidth = 320, .minHeight = 200, .customTitlebar = true});

    Label* status = nullptr;
    window->root()->add<TitleBar>().build([&](TitleBar& bar) {
        bar.add<Label>("Hello, Terracotta2").weight(600).grow(1);
        bar.add<Button>("Share").onClick([&] { status->text("Shared!"); });
        bar.add<WindowButtons>();
    });
    window->root()->add<Panel>().grow(1).justify(Justify::Center).alignItems(Align::Center).gap(8).build([&](Panel& page) {
        page.add<Label>("Drag the bar to move the window.").fontSize(18);
        page.add<Label>("The button in it still works.").color(theme().textMuted).ref(status);
    });

    app.run();
}
