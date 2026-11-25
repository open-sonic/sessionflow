#pragma once

#include "engine/processor.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace sessionflow
{

// Called on the host's processing thread, before the block it applies to.
using HostParameterHandler = std::function<void(std::uint32_t id, float value)>;

// The loop inside a plugin host process. Connects to the engine that started this process,
// then prepares and runs processor for every request until the engine shuts the connection
// down or exits. Returns the process exit code.
int runPluginHost(const std::string& connection, Processor& processor, const HostParameterHandler& onParameter);

} // namespace sessionflow
