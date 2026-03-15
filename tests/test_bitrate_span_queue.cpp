#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/BitrateSpanQueue.hpp"

TEST_CASE("BitrateSpanQueue consumes spans in playback order") {
    BitrateSpanQueue<8> queue;

    REQUIRE(queue.push(100, 192000));
    REQUIRE(queue.push(200, 256000));

    CHECK(queue.consumeFrames(50) == 192000);
    CHECK(queue.consumeFrames(50) == 192000);
    CHECK(queue.consumeFrames(1) == 256000);
    CHECK(queue.consumeFrames(199) == 256000);
    CHECK(queue.consumeFrames(1) == 0);
}

TEST_CASE("BitrateSpanQueue reset clears queued telemetry") {
    BitrateSpanQueue<8> queue;

    REQUIRE(queue.push(120, 320000));
    CHECK(queue.consumeFrames(40) == 320000);

    queue.reset();

    CHECK(queue.consumeFrames(1) == 0);
    REQUIRE(queue.push(60, 128000));
    CHECK(queue.consumeFrames(60) == 128000);
}
