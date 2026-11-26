#pragma once

#include "library/database.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sessionflow
{

// Stored in the index file. Append new kinds, never renumber.
enum class LibraryEntryKind
{
    Sample = 0,
    Plugin = 1,
    Preset = 2,
};

struct LibraryEntry
{
    std::filesystem::path path{};
    std::string name{};
    LibraryEntryKind kind = LibraryEntryKind::Sample;
    std::optional<double> durationSeconds{};
    std::optional<double> sampleRate{};
    std::vector<std::string> tags{}; // sorted, without duplicates

    bool operator==(const LibraryEntry&) const = default;
};

// The samples, plugins and presets the browser shows, stored in one SQLite file. Blocking, so
// use it from worker threads and the UI, never the audio thread.
class LibraryIndex
{
  public:
    // Opens the index at path, creating it if needed. Returns nothing for files that aren't an
    // index or were written by a newer version.
    static std::optional<LibraryIndex> open(const std::filesystem::path& path);
    static std::optional<LibraryIndex> openInMemory();

    // Adds the entries, replacing those with the same path, in one transaction.
    bool put(std::span<const LibraryEntry> entries);
    bool remove(const std::filesystem::path& path);

    // Entries whose name contains every word of text, ignoring ASCII case, sorted by name.
    // Empty text matches everything.
    std::vector<LibraryEntry> search(std::string_view text, std::optional<LibraryEntryKind> kind = std::nullopt,
                                     std::size_t limit = 200);

    std::size_t size();

    const std::string& lastError() const noexcept
    {
        return m_database.lastError();
    }

  private:
    explicit LibraryIndex(Database database) noexcept;
    static std::optional<LibraryIndex> fromDatabase(std::optional<Database> database);

    Database m_database;
};

} // namespace sessionflow
