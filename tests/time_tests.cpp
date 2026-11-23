#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a beat has 960 ticks")
{
    CHECK(sessionflow::ticksPerBeat == 960);
}
