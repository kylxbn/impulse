#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/PeakSpanQueue.hpp"

TEST_CASE("PeakSpanQueue consumes peaks in playback order") {
    PeakSpanQueue<8> queue;

    REQUIRE(queue.push(100, 0.50f, false));
    REQUIRE(queue.push(200, 1.25f, true));

    const PeakConsumeResult first_half = queue.consumeFrames(50);
    CHECK(first_half.peak_abs == doctest::Approx(0.50f));
    CHECK_FALSE(first_half.clipped);

    const PeakConsumeResult boundary = queue.consumeFrames(75);
    CHECK(boundary.peak_abs == doctest::Approx(1.25f));
    CHECK(boundary.clipped);

    const PeakConsumeResult remainder = queue.consumeFrames(175);
    CHECK(remainder.peak_abs == doctest::Approx(1.25f));
    CHECK(remainder.clipped);

    const PeakConsumeResult empty = queue.consumeFrames(1);
    CHECK(empty.peak_abs == doctest::Approx(0.0f));
    CHECK_FALSE(empty.clipped);
}

TEST_CASE("PeakSpanQueue reset clears queued telemetry") {
    PeakSpanQueue<8> queue;

    REQUIRE(queue.push(64, 1.10f, true));
    const PeakConsumeResult partial = queue.consumeFrames(16);
    CHECK(partial.peak_abs == doctest::Approx(1.10f));
    CHECK(partial.clipped);

    queue.reset();

    const PeakConsumeResult cleared = queue.consumeFrames(1);
    CHECK(cleared.peak_abs == doctest::Approx(0.0f));
    CHECK_FALSE(cleared.clipped);

    REQUIRE(queue.push(32, 0.25f, false));
    const PeakConsumeResult restored = queue.consumeFrames(32);
    CHECK(restored.peak_abs == doctest::Approx(0.25f));
    CHECK_FALSE(restored.clipped);
}
