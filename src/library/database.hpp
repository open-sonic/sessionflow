#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace sessionflow
{

// A prepared SQL statement. Parameters and columns count from 0. Valid only while the
// Database that prepared it lives.
class Statement
{
  public:
    enum class StepResult
    {
        Row,
        Done,
        Error,
    };

    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement();

    bool bind(int index, std::int64_t value);
    bool bind(int index, double value);
    // Copies the text, so it may go out of scope before step().
    bool bind(int index, std::string_view value);
    bool bindNull(int index);

    StepResult step();
    // Clears the bindings too, so the statement can run again with new ones.
    void reset();

    bool isNull(int column) const;
    std::int64_t integer(int column) const;
    double real(int column) const;
    std::string text(int column) const;

  private:
    friend class Database;
    explicit Statement(sqlite3_stmt* statement) noexcept;

    sqlite3_stmt* m_statement = nullptr;
};

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

    // Returns nothing and keeps the message on error.
    std::optional<Statement> prepare(std::string_view sql);

    // The message of the last failed call on this connection, including failed statement steps.
    const std::string& lastError() const noexcept
    {
        return m_lastError;
    }

    // Copies the connection's current error message into lastError().
    void captureError();

  private:
    explicit Database(sqlite3* connection) noexcept;

    sqlite3* m_connection = nullptr;
    std::string m_lastError;
};

} // namespace sessionflow
