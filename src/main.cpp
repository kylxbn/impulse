#include "app/Application.hpp"

#include <memory>

int main(int /*argc*/, char* /*argv*/[]) {
    auto app = std::make_unique<Application>();
    return app->run();
}
