#include "plugins/plugin_folders.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>

namespace sessionflow
{

namespace
{

std::optional<std::string> environment(const char* name)
{
#if defined(_WIN32)
    // MSVC deprecates getenv in favor of _dupenv_s.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

void appendFromEnvironment(std::vector<std::filesystem::path>& folders, const char* name,
                           const std::filesystem::path& relative)
{
    if (const std::optional<std::string> base = environment(name))
    {
        folders.push_back(std::filesystem::path(*base) / relative);
    }
}

// CLAP_PATH lists extra folders, separated like PATH on this platform.
void appendClapPath(std::vector<std::filesystem::path>& folders)
{
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    const std::optional<std::string> clapPath = environment("CLAP_PATH");
    if (!clapPath)
    {
        return;
    }
    std::size_t start = 0;
    while (start <= clapPath->size())
    {
        const std::size_t end = std::min(clapPath->find(separator, start), clapPath->size());
        if (end > start)
        {
            folders.emplace_back(clapPath->substr(start, end - start));
        }
        start = end + 1;
    }
}

} // namespace

std::vector<std::filesystem::path> defaultPluginFolders(PluginFormat format)
{
    std::vector<std::filesystem::path> folders;
    if (format == PluginFormat::Clap)
    {
        appendClapPath(folders);
    }

#if defined(_WIN32)
    if (format == PluginFormat::Vst3)
    {
        appendFromEnvironment(folders, "COMMONPROGRAMFILES", "VST3");
    }
    else
    {
        appendFromEnvironment(folders, "COMMONPROGRAMFILES", "CLAP");
        appendFromEnvironment(folders, "LOCALAPPDATA", "Programs/Common/CLAP");
    }
#elif defined(__APPLE__)
    const char* folder = format == PluginFormat::Vst3 ? "VST3" : "CLAP";
    appendFromEnvironment(folders, "HOME", std::filesystem::path("Library/Audio/Plug-Ins") / folder);
    folders.push_back(std::filesystem::path("/Library/Audio/Plug-Ins") / folder);
#else
    const char* folder = format == PluginFormat::Vst3 ? "vst3" : "clap";
    appendFromEnvironment(folders, "HOME", std::string(".") + folder);
    folders.push_back(std::filesystem::path("/usr/lib") / folder);
    folders.push_back(std::filesystem::path("/usr/local/lib") / folder);
#endif
    return folders;
}

} // namespace sessionflow
