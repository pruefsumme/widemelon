// WideMelon renderer profile. GPL-3.0-or-later, like the melonDS core.
#pragma once

#include <cstdint>
#include <cstdlib>

namespace WideMelon
{

// Immutable for the lifetime of a process: geometry and GL allocations must agree.
inline int Width()
{
    static const int width = []
    {
        const char* value = std::getenv("WIDEMELON_VIEW_WIDTH");
        if (!value)
            return 256;

        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end != value && *end == '\0' && parsed >= 256 && parsed <= 768 && parsed % 2 == 0)
            return static_cast<int>(parsed);
        return 256;
    }();
    return width;
}

inline bool Enabled()
{
    return Width() > 256;
}

inline int32_t ProjectX(int32_t x)
{
    return static_cast<int32_t>(static_cast<int64_t>(x) * 256 / Width());
}

}
