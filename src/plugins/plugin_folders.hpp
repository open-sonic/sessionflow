#pragma once

#include <filesystem>
#include <vector>

namespace sessionflow
{

enum class PluginFormat
{
    Vst3,
    Clap,
};

// Standard install folders for a plugin format on this platform, in scan order.
// Folders that don't exist are included; the scanner skips them.
std::vector<std::filesystem::path> defaultPluginFolders(PluginFormat format);

} // namespace sessionflow
