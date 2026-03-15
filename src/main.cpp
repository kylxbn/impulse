#include "app/Application.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

#include <memory>

int main(int /*argc*/, char* /*argv*/[]) {
    avformat_network_init();

    auto app = std::make_unique<Application>();
    const int exit_code = app->run();

    avformat_network_deinit();
    return exit_code;
}
