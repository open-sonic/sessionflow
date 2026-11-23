#include "plugins/plugin_folders.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <string>

using namespace sessionflow;

namespace
{

// Folders are named VST3, vst3 or .vst3 depending on the platform.
bool allNamed(const std::vector<std::filesystem::path>& folders, std::string_view formatName)
{
    return std::ranges::all_of(folders,
                               [&](const std::filesystem::path& folder)
                               {
                                   std::string name = folder.filename().string();
                                   if (name.starts_with('.'))
                                   {
                                       name.erase(0, 1);
                                   }
                                   std::ranges::transform(name, name.begin(), [](unsigned char c)
                                                          { return static_cast<char>(std::tolower(c)); });
                                   return name == formatName;
                               });
}

} // namespace

TEST_CASE("VST3 folders end in the format folder")
{
    const std::vector<std::filesystem::path> folders = defaultPluginFolders(PluginFormat::Vst3);

    CHECK_FALSE(folders.empty());
    CHECK(allNamed(folders, "vst3"));
}

TEST_CASE("CLAP folders end in the format folder")
{
    const std::vector<std::filesystem::path> folders = defaultPluginFolders(PluginFormat::Clap);

    CHECK_FALSE(folders.empty());
    CHECK(allNamed(folders, "clap"));
}
