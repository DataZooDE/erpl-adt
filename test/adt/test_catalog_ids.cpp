#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/catalog_ids.hpp>

using namespace erpl_adt;

TEST_CASE("DeriveEntityId: same inputs produce the same ID across calls",
          "[adt][catalog][ids]") {
    auto a = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "MARA");
    auto b = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "MARA");
    CHECK(a.Value() == b.Value());
}

TEST_CASE("DeriveEntityId: different technical_name produces a different ID",
          "[adt][catalog][ids]") {
    auto a = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "MARA");
    auto b = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "MARC");
    CHECK(a.Value() != b.Value());
}

TEST_CASE("DeriveEntityId: different domain produces a different ID even with same name",
          "[adt][catalog][ids]") {
    auto a = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "0MATERIAL");
    auto b = DeriveEntityId("A4H", CatalogDomain::Bw, "IOBJ", "0MATERIAL");
    CHECK(a.Value() != b.Value());
}

TEST_CASE("DeriveEntityId: different system_sid produces a different ID",
          "[adt][catalog][ids]") {
    auto a = DeriveEntityId("A4H", CatalogDomain::Bw, "IOBJ", "0MATERIAL");
    auto b = DeriveEntityId("Q4H", CatalogDomain::Bw, "IOBJ", "0MATERIAL");
    CHECK(a.Value() != b.Value());
}

TEST_CASE("DeriveEntityId: field-separator collisions are prevented by escaping",
          "[adt][catalog][ids]") {
    // Without escaping, ("A4H", Ddic, "TABL|DT", "MARA") and
    // ("A4H", Ddic, "TABL", "DT|MARA") would hash to the same bytes.
    auto a = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL|DT", "MARA");
    auto b = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "DT|MARA");
    CHECK(a.Value() != b.Value());
}

TEST_CASE("DeriveEntityId: produces a lowercase hex sha256 digest (64 chars)",
          "[adt][catalog][ids]") {
    auto id = DeriveEntityId("A4H", CatalogDomain::Ddic, "TABL", "MARA");
    REQUIRE(id.Value().size() == 64);
    for (char c : id.Value()) {
        CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
    }
}
