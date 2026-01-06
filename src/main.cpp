#include "core/application.hpp"

auto main() -> i32
{
    Application* app = new Application();
    app->run();
    delete app;
}
