#include "library/database.hpp"

#include <sqlite3.h>

#include <utility>

namespace sessionflow
{

namespace
{

std::optional<sqlite3*> openConnection(const char* utf8Path)
{
    sqlite3* connection = nullptr;
    if (sqlite3_open_v2(utf8Path, &connection, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
    {
        sqlite3_close(connection);
        return std::nullopt;
    }
    return connection;
}

} // namespace

std::optional<Database> Database::open(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    const std::optional<sqlite3*> connection = openConnection(reinterpret_cast<const char*>(utf8.c_str()));
    if (!connection)
    {
        return std::nullopt;
    }
    return Database(*connection);
}

std::optional<Database> Database::openInMemory()
{
    const std::optional<sqlite3*> connection = openConnection(":memory:");
    if (!connection)
    {
        return std::nullopt;
    }
    return Database(*connection);
}

Database::Database(sqlite3* connection) noexcept : m_connection(connection)
{
}

Database::Database(Database&& other) noexcept
    : m_connection(std::exchange(other.m_connection, nullptr)), m_lastError(std::move(other.m_lastError))
{
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other)
    {
        sqlite3_close(m_connection);
        m_connection = std::exchange(other.m_connection, nullptr);
        m_lastError = std::move(other.m_lastError);
    }
    return *this;
}

Database::~Database()
{
    sqlite3_close(m_connection);
}

bool Database::execute(std::string_view sql)
{
    const std::string statement(sql);
    char* message = nullptr;
    if (sqlite3_exec(m_connection, statement.c_str(), nullptr, nullptr, &message) != SQLITE_OK)
    {
        m_lastError = message != nullptr ? message : "unknown error";
        sqlite3_free(message);
        return false;
    }
    return true;
}

std::optional<std::int64_t> Database::queryInteger(std::string_view sql)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(m_connection, sql.data(), static_cast<int>(sql.size()), &statement, nullptr) != SQLITE_OK)
    {
        m_lastError = sqlite3_errmsg(m_connection);
        return std::nullopt;
    }

    std::optional<std::int64_t> result;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        result = sqlite3_column_int64(statement, 0);
    }
    sqlite3_finalize(statement);
    return result;
}

} // namespace sessionflow
