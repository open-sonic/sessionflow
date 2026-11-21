#include "core/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version matches the CMake project version")
{
    CHECK(sessionflow::version() == "0.1.0");
}
