#pragma once

extern "C" {
#include <libavutil/samplefmt.h>
}

struct AudioFormat {
    int            sample_rate = 48000;
    int            channels    = 2;
    AVSampleFormat av_format   = AV_SAMPLE_FMT_FLT;

    int bytes_per_sample() const { return av_get_bytes_per_sample(av_format); }
    int bytes_per_frame()  const { return channels * bytes_per_sample(); }
};
