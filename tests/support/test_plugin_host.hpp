#pragma once

#include <cstdint>

namespace sessionflow::test
{

// Parameters of the test plugin host. The host multiplies audio by the gain and crashes or
// hangs when the matching parameter arrives.
enum TestHostParameter : std::uint32_t
{
    testHostGain,
    testHostCrash,
    testHostHang,
};

} // namespace sessionflow::test
