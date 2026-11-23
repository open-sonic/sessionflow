#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;

namespace sessionflow
{

// Owns one SQLite connection. Blocking, so use it from worker threads, never the audio thread.
class Database
{
  public:
    static std::optional<Database> open(const std::filesystem::path& path);
    static std::optional<Database> openInMemory();

    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    ~Database();

    // Runs one or more statements without results. Returns false and keeps the message on error.
    bool execute(std::string_view sql);

    // First column of the first row, for counts and lookups.
    std::optional<std::int64_t> queryInteger(std::string_view sql);

    const std::string& lastError() const noexcept
    {
        return m_lastError;
    }

  private:
    explicit Database(sqlite3* connection) noexcept;

    sqlite3* m_connection = nullptr;
    std::string m_lastError;
};

} // namespace sessionflow
