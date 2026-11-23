#include "library/database.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace sessionflow;

TEST_CASE("in-memory database runs statements and queries")
{
    std::optional<Database> database = Database::openInMemory();
    REQUIRE(database.has_value());

    CHECK(database->execute("CREATE TABLE samples (path TEXT); INSERT INTO samples VALUES ('kick.wav');"));

    CHECK(database->queryInteger("SELECT COUNT(*) FROM samples") == 1);
}

TEST_CASE("invalid SQL reports an error")
{
    std::optional<Database> database = Database::openInMemory();
    REQUIRE(database.has_value());

    CHECK_FALSE(database->execute("SELEKT nothing"));
    CHECK_FALSE(database->lastError().empty());
}
