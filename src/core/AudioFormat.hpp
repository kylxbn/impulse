#pragma once

struct AudioFormat {
    int            sample_rate = 48000;
    int            channels    = 2;

    int bytes_per_sample() const { return static_cast<int>(sizeof(float)); }
    int bytes_per_frame()  const { return channels * bytes_per_sample(); }
};
