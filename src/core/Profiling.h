#pragma once

#include <chrono>
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedN(name)
#define FrameMark
#define TracyPlot(name, value)
#endif

using ProfileClock = std::chrono::steady_clock;
inline double ProfileMilliseconds(ProfileClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(ProfileClock::now() - start).count();
}
