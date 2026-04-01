#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/PeakMeterAccumulator.hpp"

#include <array>
#include <cmath>
#include <limits>

TEST_CASE("PeakMeterAccumulator emits fixed-size windows across callback boundaries") {
    PeakMeterAccumulator accumulator(4);

    const std::array<float, 4> first_callback{
        0.10f, -0.20f,
        0.30f, -0.40f,
    };
    CHECK_FALSE(accumulator.consume(first_callback.data(), 2, 2).has_value());

    const std::array<float, 4> second_callback{
        0.90f, -0.60f,
        0.20f, -0.10f,
    };
    const auto completed = accumulator.consume(second_callback.data(), 2, 2);
    REQUIRE(completed.has_value());
    CHECK(completed->peak_abs == doctest::Approx(0.90f));
    CHECK(completed->rms_abs == doctest::Approx(std::sqrt(1.52f / 8.0f)));
    CHECK_FALSE(completed->clipped);
}

TEST_CASE("PeakMeterAccumulator flushes a trailing partial window") {
    PeakMeterAccumulator accumulator(4);

    const std::array<float, 6> samples{
        0.10f, -0.20f,
        0.30f, -0.40f,
        0.75f, -0.50f,
    };
    CHECK_FALSE(accumulator.consume(samples.data(), 3, 2).has_value());

    const auto flushed = accumulator.flush();
    REQUIRE(flushed.has_value());
    CHECK(flushed->peak_abs == doctest::Approx(0.75f));
    CHECK(flushed->rms_abs == doctest::Approx(std::sqrt(1.1125f / 6.0f)));
    CHECK_FALSE(flushed->clipped);

    CHECK_FALSE(accumulator.flush().has_value());
}

TEST_CASE("PeakMeterAccumulator flags clipped and non-finite samples") {
    PeakMeterAccumulator accumulator(1);

    const std::array<float, 2> clipped_samples{1.10f, -0.25f};
    const auto clipped = accumulator.consume(clipped_samples.data(), 1, 2);
    REQUIRE(clipped.has_value());
    CHECK(clipped->peak_abs == doctest::Approx(1.10f));
    CHECK(clipped->rms_abs == doctest::Approx(std::sqrt(1.2725f / 2.0f)));
    CHECK(clipped->clipped);

    const std::array<float, 2> non_finite_samples{
        std::numeric_limits<float>::infinity(),
        0.0f,
    };
    const auto non_finite = accumulator.consume(non_finite_samples.data(), 1, 2);
    REQUIRE(non_finite.has_value());
    CHECK(!std::isfinite(non_finite->peak_abs));
    CHECK(!std::isfinite(non_finite->rms_abs));
    CHECK(non_finite->clipped);
}
