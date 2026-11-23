#pragma once

#include <cstdint>

namespace sessionflow
{

// Positions and lengths on the timeline, in samples at the project sample rate.
using SampleCount = std::int64_t;

// Musical positions and lengths. Clips, notes and automation live in ticks so they stay on
// their beats when the tempo changes. The transport converts ticks to samples.
using TickCount = std::int64_t;

// Saved in project files. Changing it shifts every stored position.
inline constexpr TickCount ticksPerBeat = 960;

} // namespace sessionflow
