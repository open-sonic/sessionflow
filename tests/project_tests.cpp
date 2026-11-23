#include "model/project.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace sessionflow;

TEST_CASE("adding a track returns a new project and keeps the old one")
{
    const Project empty;

    const Project withDrums = addTrack(empty, {.name = "Drums"});

    CHECK(empty.tracks.empty());
    REQUIRE(withDrums.tracks.size() == 1);
    CHECK(withDrums.tracks[0].name == "Drums");
}

TEST_CASE("projects compare by value")
{
    const Project first = addTrack({}, {.name = "Bass"});
    const Project second = addTrack({}, {.name = "Bass"});

    CHECK(first == second);
    CHECK(first != addTrack(first, {.name = "Keys"}));
}
