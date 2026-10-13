
#include <terracotta2/Terracotta2.hpp>
#include <string>

using namespace Terracotta2;

int main() {
    Application app; // platform + GPU; one per process
    Window* window = app.createWindow({.title = "Hello, Terracotta2", .width = 480, .height = 300, .minWidth = 240, .minHeight = 150});

    int clicks = 0;
    window->root()->add<Panel>().grow(1).padding(24).gap(12).build([&](Panel& page) {
        auto& greeting = page.add<Label>("Hello, world!").fontSize(28).weight(700);
        auto& counter = page.add<Label>("Clicked 0 times").color(SK_ColorGRAY);

        page.add<InputText>("Your name").onChange([g = &greeting](const std::string& name) {
            g->text("Hello, " + (name.empty() ? std::string("world") : name) + "!");
        });
        page.add<Button>("Click me").onClick([&clicks, c = &counter] {
            c->text("Clicked " + std::to_string(++clicks) + " times");
        });
    });

    app.run(); 
}
