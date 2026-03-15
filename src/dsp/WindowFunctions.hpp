#pragma once

#include <array>
#include <cmath>
#include <numbers>

// Precompute a Hann window of length N into the given array.
template <size_t N>
constexpr void makeHannWindow(std::array<float, N>& w) {
    for (size_t i = 0; i < N; ++i)
        w[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (N - 1)));
}
