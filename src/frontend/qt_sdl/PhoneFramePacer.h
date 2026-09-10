// Deadline-based 30 FPS capture pacing, independent of display refresh jitter.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

class PhoneFramePacer
{
public:
    bool due(std::int64_t now)
    {
        constexpr std::int64_t interval = 1000000000 / 30;
        if (now < nextCapture) return false;
        // Advance the deadline instead of discarding each frame's lateness.
        // After a pause, capture once and resume without a catch-up burst.
        if (nextCapture == 0 || now - nextCapture >= interval) nextCapture = now;
        nextCapture += interval;
        return true;
    }

    void reset() { nextCapture = 0; }

private:
    std::int64_t nextCapture = 0;
};
