#include "library/library_index.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace sessionflow;

namespace
{

LibraryEntry sample(std::string name)
{
    return {.path = "samples/" + name + ".wav", .name = name, .durationSeconds = 1.5, .sampleRate = 44100.0};
}

std::vector<std::string> names(const std::vector<LibraryEntry>& entries)
{
    std::vector<std::string> result;
    for (const LibraryEntry& entry : entries)
    {
        result.push_back(entry.name);
    }
    return result;
}

struct TemporaryFile
{
    std::filesystem::path path;

    explicit TemporaryFile(const char* name) : path(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove(path);
    }

    ~TemporaryFile()
    {
        std::filesystem::remove(path);
    }
};

} // namespace

TEST_CASE("entries are found by any part of their name, ignoring case")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    const std::vector<LibraryEntry> entries{sample("Kick Hard"), sample("808 kick"), sample("Snare"),
                                            sample("Hat open")};
    REQUIRE(index->put(entries));

    CHECK(names(index->search("KICK")) == std::vector<std::string>{"808 kick", "Kick Hard"});
    CHECK(names(index->search("ar")) == std::vector<std::string>{"Kick Hard", "Snare"});
    CHECK(names(index->search("")) == std::vector<std::string>{"808 kick", "Hat open", "Kick Hard", "Snare"});
    CHECK(index->search("clap").empty());
}

TEST_CASE("every word of the search must match")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    const std::vector<LibraryEntry> entries{sample("Kick Hard"), sample("Kick Soft"), sample("Snare Hard")};
    REQUIRE(index->put(entries));

    CHECK(names(index->search("  hard   kick ")) == std::vector<std::string>{"Kick Hard"});
}

TEST_CASE("LIKE wildcards in a search are taken literally")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    const std::vector<LibraryEntry> entries{sample("100% wet"), sample("100 dry"), sample("pad_a"), sample("pada")};
    REQUIRE(index->put(entries));

    CHECK(names(index->search("100%")) == std::vector<std::string>{"100% wet"});
    CHECK(names(index->search("d_a")) == std::vector<std::string>{"pad_a"});
}

TEST_CASE("a search can be limited to one kind and a number of results")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    const std::vector<LibraryEntry> entries{
        sample("Bass one"),
        sample("Bass two"),
        {.path = "C:/Plugins/Bass.vst3", .name = "Bass synth", .kind = LibraryEntryKind::Plugin},
    };
    REQUIRE(index->put(entries));

    CHECK(names(index->search("bass", LibraryEntryKind::Plugin)) == std::vector<std::string>{"Bass synth"});
    CHECK(names(index->search("bass", std::nullopt, 2)) == std::vector<std::string>{"Bass one", "Bass synth"});
}

TEST_CASE("entries keep their fields and tags")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    const LibraryEntry kick{.path = std::filesystem::path(u8"Drums/Kick ä.wav"),
                            .name = "Kick ä",
                            .durationSeconds = 0.25,
                            .sampleRate = 48000.0,
                            .tags = {"808", "drums", "punchy"}};
    const LibraryEntry preset{.path = "presets/lead.fxp", .name = "Lead", .kind = LibraryEntryKind::Preset};
    REQUIRE(index->put(std::vector{kick, preset}));

    CHECK(index->search("kick") == std::vector{kick});
    CHECK(index->search("lead") == std::vector{preset});
}

TEST_CASE("putting an entry with a known path replaces it")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    LibraryEntry entry = sample("Loop");
    entry.tags = {"old"};
    REQUIRE(index->put(std::vector{entry}));

    entry.name = "Loop 120";
    entry.tags = {"new", "tempo"};
    REQUIRE(index->put(std::vector{entry}));

    CHECK(index->size() == 1);
    CHECK(index->search("loop") == std::vector{entry});
}

TEST_CASE("removed entries are gone with their tags")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    LibraryEntry entry = sample("Clap");
    entry.tags = {"drums"};
    REQUIRE(index->put(std::vector{entry, sample("Rim")}));

    REQUIRE(index->remove(entry.path));
    REQUIRE(index->put(std::vector{sample("Clap")}));

    CHECK(index->size() == 2);
    CHECK(index->search("clap")[0].tags.empty());
}

TEST_CASE("an index file keeps its entries")
{
    const TemporaryFile file("sessionflow_library_index_test.db");
    {
        std::optional<LibraryIndex> index = LibraryIndex::open(file.path);
        REQUIRE(index);
        REQUIRE(index->put(std::vector{sample("Kick")}));
    }

    std::optional<LibraryIndex> reopened = LibraryIndex::open(file.path);
    REQUIRE(reopened);
    CHECK(names(reopened->search("kick")) == std::vector<std::string>{"Kick"});
}

TEST_CASE("files that aren't an index are not opened")
{
    const TemporaryFile file("sessionflow_library_index_foreign.db");

    SECTION("another SQLite database")
    {
        std::optional<Database> database = Database::open(file.path);
        REQUIRE(database);
        REQUIRE(database->execute("CREATE TABLE songs (title TEXT)"));
    }

    SECTION("an index from a newer version")
    {
        std::optional<Database> database = Database::open(file.path);
        REQUIRE(database);
        REQUIRE(database->execute("PRAGMA user_version = 99"));
    }

    SECTION("not SQLite at all")
    {
        std::ofstream(file.path) << "not a database, but long enough to look like a header to SQLite.......";
    }

    CHECK_FALSE(LibraryIndex::open(file.path));
}

TEST_CASE("100k entries are searched by name in under 50 ms")
{
    std::optional<LibraryIndex> index = LibraryIndex::openInMemory();
    REQUIRE(index);
    std::vector<LibraryEntry> entries;
    constexpr std::size_t s_count = 100'000;
    entries.reserve(s_count);
    for (std::size_t number = 0; number < s_count; ++number)
    {
        const char* kind = number % 3 == 0 ? "Kick" : number % 3 == 1 ? "Snare" : "Hat";
        entries.push_back(sample(std::string(kind) + " " + std::to_string(number)));
    }
    REQUIRE(index->put(entries));

    const auto searchTime = [&](std::string_view text, std::size_t expected)
    {
        const auto start = std::chrono::steady_clock::now();
        const std::vector<LibraryEntry> found = index->search(text);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        CHECK(found.size() == expected);
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
    };

    // A common word stops after the first page, a rare one scans everything.
    const std::chrono::milliseconds common = searchTime("kick", 200);
    const std::chrono::milliseconds rare = searchTime("99999", 1);
    INFO("common " << common.count() << " ms, rare " << rare.count() << " ms");
#ifdef NDEBUG
    CHECK(common.count() < 50);
    CHECK(rare.count() < 50);
#endif
}
