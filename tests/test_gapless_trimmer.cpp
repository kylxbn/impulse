#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/GaplessTrimmer.hpp"

TEST_CASE("GaplessTrimmer applies explicit per-frame trims") {
    GaplessTrimmer trimmer;
    trimmer.reset(44100, 44100, 2, 0, 0);

    std::vector<float> pcm{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
    };
    trimmer.applyExplicitFrameTrim(pcm, 44100, 1, 2);

    const std::vector<float> expected{2, 3, 4, 5, 6, 7};
    CHECK(pcm == expected);
}

TEST_CASE("GaplessTrimmer fallback trims leading and trailing padding across chunks") {
    GaplessTrimmer trimmer;
    trimmer.reset(48000, 48000, 2, 2, 3);

    std::vector<float> first_chunk{
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15
    };
    trimmer.applyFallbackTrim(first_chunk, false);
    CHECK(first_chunk == std::vector<float>{4, 5, 6, 7, 8, 9});

    std::vector<float> second_chunk{
        16, 17, 18, 19, 20, 21, 22, 23
    };
    trimmer.applyFallbackTrim(second_chunk, false);
    CHECK(second_chunk == std::vector<float>{10, 11, 12, 13, 14, 15, 16, 17});

    std::vector<float> eof_chunk;
    trimmer.applyFallbackTrim(eof_chunk, true);
    CHECK(eof_chunk.empty());
}

TEST_CASE("GaplessTrimmer seek offsets and duration adjust for trim metadata") {
    GaplessTrimmer trimmer;
    trimmer.reset(48000, 48000, 2, 480, 240);

    CHECK(trimmer.rawSeekSeconds(0.0) == doctest::Approx(0.0));
    CHECK(trimmer.rawSeekSeconds(10.0) == doctest::Approx(10.01));
    CHECK(trimmer.adjustedTotalFrames(48000) == 47280);
}

TEST_CASE("GaplessTrimmer clears leading fallback after non-zero seek") {
    GaplessTrimmer trimmer;
    trimmer.reset(48000, 48000, 2, 2, 0);
    trimmer.resetForSeek(5.0);

    std::vector<float> pcm{0, 1, 2, 3, 4, 5, 6, 7};
    trimmer.applyFallbackTrim(pcm, false);
    CHECK(pcm == std::vector<float>{0, 1, 2, 3, 4, 5, 6, 7});
}
