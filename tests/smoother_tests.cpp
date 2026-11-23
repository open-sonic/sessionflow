#include "core/smoother.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sessionflow;

TEST_CASE("smoother reaches the target after the ramp length")
{
    LinearSmoother smoother;
    smoother.prepare(1000.0, 0.01);
    smoother.snapTo(0.0f);

    smoother.setTarget(1.0f);
    for (int sample = 0; sample < 9; ++sample)
    {
        smoother.next();
    }

    CHECK(smoother.isSmoothing());
    CHECK(smoother.next() == 1.0f);
    CHECK_FALSE(smoother.isSmoothing());
}

TEST_CASE("smoother ramps linearly")
{
    LinearSmoother smoother;
    smoother.prepare(1000.0, 0.004);
    smoother.snapTo(0.0f);

    smoother.setTarget(1.0f);

    CHECK(smoother.next() == Catch::Approx(0.25f));
    CHECK(smoother.next() == Catch::Approx(0.5f));
}

TEST_CASE("snapTo jumps without a ramp")
{
    LinearSmoother smoother;
    smoother.prepare(1000.0, 0.01);
    smoother.setTarget(1.0f);

    smoother.snapTo(0.5f);

    CHECK_FALSE(smoother.isSmoothing());
    CHECK(smoother.next() == 0.5f);
}
