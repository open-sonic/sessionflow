#pragma once

#include <immer/flex_vector.hpp>

#include <string>

namespace sessionflow
{

// Immutable project state. Every edit returns a new Project that shares unchanged parts
// with the old one, so undo keeps old versions and the audio thread reads them lock-free.
struct Track
{
    std::string name;

    bool operator==(const Track&) const = default;
};

struct Project
{
    immer::flex_vector<Track> tracks;

    bool operator==(const Project&) const = default;
};

Project addTrack(const Project& project, Track track);

} // namespace sessionflow
