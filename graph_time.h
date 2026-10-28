// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct GraphSeries {
    const time_t *times;
    const float *values;
    size_t count;
    time_t maxGap;
};

inline int GraphTimeX(time_t time, time_t start, time_t end, int x, int width)
{
    return x + static_cast<int>((static_cast<int64_t>(time) - start) * width / (end - start));
}

inline bool ClipGraphTime(time_t a, time_t b, time_t start, time_t end, time_t maxGap, time_t &left, time_t &right)
{
    if (a >= b || b - a > maxGap || b <= start || a >= end) return false;
    left = a < start ? start : a;
    right = b > end ? end : b;
    return left < right;
}
