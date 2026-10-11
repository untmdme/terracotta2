// Hello World: the smallest useful Terracotta2 app.
//
// Every widget is a flexbox container. add<T>(args...) creates a child (args go to T's
// constructor) and returns it, setters chain, and build() nests children.
#include <terracotta2/Core/Application.hpp>
#include <terracotta2/Core/Window.hpp>
#include <terracotta2/UI/Container.hpp>
#include <terracotta2/UI/Controls.hpp>
#include <string>

using namespace Terracotta2;

int main() {
    Application app; // platform + GPU; one per process
    Window* window = app.createWindow({.title = "Hello, Terracotta2", .width = 480, .height = 300});

    int clicks = 0;
    window->getRootWidget()->add<Panel>().grow(1).padding(24).gap(12).build([&](Panel& page) {
        auto& greeting = page.add<Label>("Hello, world!").fontSize(28).weight(700);
        auto& counter = page.add<Label>("Clicked 0 times").color(SK_ColorGRAY);

        page.add<InputText>("Your name", [g = &greeting](const std::string& name) {
            g->text("Hello, " + (name.empty() ? std::string("world") : name) + "!");
        });
        page.add<Button>("Click me", [&clicks, c = &counter] {
            c->text("Clicked " + std::to_string(++clicks) + " times");
        });
    });

    app.run(); // returns when the window closes; idles at 0% CPU
}
