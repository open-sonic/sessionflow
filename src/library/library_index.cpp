#include "library/library_index.hpp"

#include <cstdint>
#include <utility>

namespace sessionflow
{

namespace
{

constexpr std::int64_t s_schemaVersion = 1;

// The name index serves both the ORDER BY and LIMIT of a search, so a common word stops after
// the first page of matches instead of sorting all of them.
constexpr std::string_view s_schema = R"sql(
BEGIN;
CREATE TABLE entries (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    kind INTEGER NOT NULL,
    duration_seconds REAL,
    sample_rate REAL
);
CREATE INDEX entries_by_name ON entries (name COLLATE NOCASE);
CREATE TABLE tags (
    entry_id INTEGER NOT NULL REFERENCES entries (id) ON DELETE CASCADE,
    tag TEXT NOT NULL,
    PRIMARY KEY (entry_id, tag)
) WITHOUT ROWID;
CREATE INDEX tags_by_tag ON tags (tag);
PRAGMA user_version = 1;
COMMIT;
)sql";

constexpr std::string_view s_upsertEntry = R"sql(
INSERT INTO entries (path, name, kind, duration_seconds, sample_rate) VALUES (?, ?, ?, ?, ?)
ON CONFLICT (path) DO UPDATE SET
    name = excluded.name,
    kind = excluded.kind,
    duration_seconds = excluded.duration_seconds,
    sample_rate = excluded.sample_rate
RETURNING id
)sql";

// Tags come back joined by the unit separator, which no tag contains.
constexpr char s_tagSeparator = '\x1F';
constexpr std::string_view s_selectEntries = R"sql(
SELECT path, name, kind, duration_seconds, sample_rate,
    (SELECT group_concat(tag, char(31) ORDER BY tag) FROM tags WHERE entry_id = entries.id)
FROM entries WHERE 1
)sql";

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::filesystem::path fromUtf8(const std::string& text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// A LIKE pattern that matches word anywhere, with LIKE's own wildcards taken literally.
std::string containsPattern(std::string_view word)
{
    std::string pattern = "%";
    for (const char character : word)
    {
        if (character == '%' || character == '_' || character == '\\')
        {
            pattern += '\\';
        }
        pattern += character;
    }
    pattern += '%';
    return pattern;
}

std::vector<std::string_view> words(std::string_view text)
{
    std::vector<std::string_view> result;
    constexpr std::string_view whitespace = " \t\r\n";
    std::size_t start = text.find_first_not_of(whitespace);
    while (start != std::string_view::npos)
    {
        const std::size_t end = text.find_first_of(whitespace, start);
        result.push_back(text.substr(start, end - start));
        start = text.find_first_not_of(whitespace, end);
    }
    return result;
}

std::vector<std::string> splitTags(const std::string& joined)
{
    std::vector<std::string> tags;
    std::size_t start = 0;
    while (start < joined.size())
    {
        std::size_t end = joined.find(s_tagSeparator, start);
        if (end == std::string::npos)
        {
            end = joined.size();
        }
        tags.push_back(joined.substr(start, end - start));
        start = end + 1;
    }
    return tags;
}

} // namespace

LibraryIndex::LibraryIndex(Database database) noexcept : m_database(std::move(database))
{
}

std::optional<LibraryIndex> LibraryIndex::open(const std::filesystem::path& path)
{
    return fromDatabase(Database::open(path));
}

std::optional<LibraryIndex> LibraryIndex::openInMemory()
{
    return fromDatabase(Database::openInMemory());
}

std::optional<LibraryIndex> LibraryIndex::fromDatabase(std::optional<Database> database)
{
    if (!database || !database->execute("PRAGMA foreign_keys = ON"))
    {
        return std::nullopt;
    }
    const std::optional<std::int64_t> version = database->queryInteger("PRAGMA user_version");
    if (!version || *version > s_schemaVersion)
    {
        return std::nullopt;
    }
    if (*version == 0)
    {
        // Version 0 is also any other SQLite file. Only an empty one becomes an index.
        if (database->queryInteger("SELECT COUNT(*) FROM sqlite_master") != 0 || !database->execute(s_schema))
        {
            return std::nullopt;
        }
    }
    return LibraryIndex(std::move(*database));
}

bool LibraryIndex::put(std::span<const LibraryEntry> entries)
{
    std::optional<Statement> upsertEntry = m_database.prepare(s_upsertEntry);
    std::optional<Statement> deleteTags = m_database.prepare("DELETE FROM tags WHERE entry_id = ?");
    std::optional<Statement> insertTag = m_database.prepare("INSERT OR IGNORE INTO tags (entry_id, tag) VALUES (?, ?)");
    if (!upsertEntry || !deleteTags || !insertTag || !m_database.execute("BEGIN"))
    {
        return false;
    }

    const auto fail = [this]
    {
        m_database.captureError();
        m_database.execute("ROLLBACK");
        return false;
    };

    for (const LibraryEntry& entry : entries)
    {
        upsertEntry->bind(0, toUtf8(entry.path));
        upsertEntry->bind(1, entry.name);
        upsertEntry->bind(2, static_cast<std::int64_t>(entry.kind));
        entry.durationSeconds ? upsertEntry->bind(3, *entry.durationSeconds) : upsertEntry->bindNull(3);
        entry.sampleRate ? upsertEntry->bind(4, *entry.sampleRate) : upsertEntry->bindNull(4);
        if (upsertEntry->step() != Statement::StepResult::Row)
        {
            return fail();
        }
        const std::int64_t id = upsertEntry->integer(0);
        if (upsertEntry->step() != Statement::StepResult::Done)
        {
            return fail();
        }
        upsertEntry->reset();

        deleteTags->bind(0, id);
        if (deleteTags->step() != Statement::StepResult::Done)
        {
            return fail();
        }
        deleteTags->reset();

        for (const std::string& tag : entry.tags)
        {
            insertTag->bind(0, id);
            insertTag->bind(1, tag);
            if (insertTag->step() != Statement::StepResult::Done)
            {
                return fail();
            }
            insertTag->reset();
        }
    }
    return m_database.execute("COMMIT");
}

bool LibraryIndex::remove(const std::filesystem::path& path)
{
    std::optional<Statement> statement = m_database.prepare("DELETE FROM entries WHERE path = ?");
    if (!statement)
    {
        return false;
    }
    statement->bind(0, toUtf8(path));
    if (statement->step() != Statement::StepResult::Done)
    {
        m_database.captureError();
        return false;
    }
    return true;
}

std::vector<LibraryEntry> LibraryIndex::search(std::string_view text, std::optional<LibraryEntryKind> kind,
                                               std::size_t limit)
{
    const std::vector<std::string_view> searchWords = words(text);

    std::string sql(s_selectEntries);
    if (kind)
    {
        sql += " AND kind = ?";
    }
    for (std::size_t index = 0; index < searchWords.size(); ++index)
    {
        sql += " AND name LIKE ? ESCAPE '\\'";
    }
    sql += " ORDER BY name COLLATE NOCASE LIMIT ?";

    std::optional<Statement> statement = m_database.prepare(sql);
    if (!statement)
    {
        return {};
    }
    int parameter = 0;
    if (kind)
    {
        statement->bind(parameter++, static_cast<std::int64_t>(*kind));
    }
    for (const std::string_view word : searchWords)
    {
        statement->bind(parameter++, containsPattern(word));
    }
    statement->bind(parameter, static_cast<std::int64_t>(limit));

    std::vector<LibraryEntry> entries;
    Statement::StepResult result = statement->step();
    while (result == Statement::StepResult::Row)
    {
        LibraryEntry entry{
            .path = fromUtf8(statement->text(0)),
            .name = statement->text(1),
            .kind = static_cast<LibraryEntryKind>(statement->integer(2)),
            .tags = splitTags(statement->text(5)),
        };
        if (!statement->isNull(3))
        {
            entry.durationSeconds = statement->real(3);
        }
        if (!statement->isNull(4))
        {
            entry.sampleRate = statement->real(4);
        }
        entries.push_back(std::move(entry));
        result = statement->step();
    }
    if (result == Statement::StepResult::Error)
    {
        m_database.captureError();
    }
    return entries;
}

std::size_t LibraryIndex::size()
{
    return static_cast<std::size_t>(m_database.queryInteger("SELECT COUNT(*) FROM entries").value_or(0));
}

} // namespace sessionflow
