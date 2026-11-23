#include "model/project.hpp"

#include <utility>

namespace sessionflow
{

Project addTrack(const Project& project, Track track)
{
    Project result = project;
    result.tracks = std::move(result.tracks).push_back(std::move(track));
    return result;
}

} // namespace sessionflow
