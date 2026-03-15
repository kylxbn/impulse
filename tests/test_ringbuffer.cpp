#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "audio/RingBuffer.hpp"

#include <thread>
#include <vector>
#include <numeric>

// ---------------------------------------------------------------------------
// Single-threaded correctness
// ---------------------------------------------------------------------------

TEST_CASE("empty buffer reads zero elements") {
    RingBuffer<float, 16> rb;
    float dst[4]{};
    CHECK(rb.read(dst, 4) == 0);
    CHECK(rb.available_read() == 0);
    CHECK(rb.available_write() == 16);
}

TEST_CASE("write then read returns same data") {
    RingBuffer<float, 16> rb;
    float src[] = {1.f, 2.f, 3.f, 4.f};
    float dst[4]{};
    CHECK(rb.write(src, 4) == 4);
    CHECK(rb.available_read() == 4);
    CHECK(rb.read(dst, 4) == 4);
    CHECK(dst[0] == 1.f);
    CHECK(dst[3] == 4.f);
    CHECK(rb.available_read() == 0);
}

TEST_CASE("write is capped at available space") {
    RingBuffer<float, 4> rb;
    float src[8]{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    CHECK(rb.write(src, 8) == 4);
    CHECK(rb.available_write() == 0);
}

TEST_CASE("wrap-around works correctly") {
    RingBuffer<float, 8> rb;
    float src[6]{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    float dst[6]{};
    rb.write(src, 6);
    rb.read(dst, 4);         // read 4, leaving {5, 6} in positions 4..5
    float more[] = {7.f, 8.f, 9.f, 10.f};
    rb.write(more, 4);       // should wrap around
    CHECK(rb.available_read() == 6);
    float out[6]{};
    CHECK(rb.read(out, 6) == 6);
    CHECK(out[0] == 5.f);
    CHECK(out[1] == 6.f);
    CHECK(out[2] == 7.f);
    CHECK(out[5] == 10.f);
}

TEST_CASE("reset clears the buffer") {
    RingBuffer<float, 8> rb;
    float src[4]{1.f, 2.f, 3.f, 4.f};
    rb.write(src, 4);
    CHECK(rb.available_read() == 4);
    rb.reset();
    CHECK(rb.available_read() == 0);
    CHECK(rb.available_write() == 8);
}

// ---------------------------------------------------------------------------
// Concurrent producer / consumer stress test
// ---------------------------------------------------------------------------

TEST_CASE("concurrent producer/consumer preserves order and no data loss") {
    constexpr size_t BUF_CAP   = 1 << 14;  // 16384
    constexpr size_t N_SAMPLES = 1 << 20;  // 1M samples total

    RingBuffer<int32_t, BUF_CAP> rb;

    std::vector<int32_t> produced(N_SAMPLES);
    std::iota(produced.begin(), produced.end(), 0);

    std::vector<int32_t> consumed;
    consumed.reserve(N_SAMPLES);

    std::thread producer([&] {
        size_t sent = 0;
        while (sent < N_SAMPLES) {
            size_t chunk   = std::min<size_t>(512, N_SAMPLES - sent);
            size_t written = rb.write(produced.data() + sent, chunk);
            sent += written;
        }
    });

    std::thread consumer([&] {
        int32_t tmp[512];
        size_t received = 0;
        while (received < N_SAMPLES) {
            size_t n = rb.read(tmp, 512);
            for (size_t i = 0; i < n; ++i)
                consumed.push_back(tmp[i]);
            received += n;
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumed.size() == N_SAMPLES);
    CHECK(consumed == produced);
}
