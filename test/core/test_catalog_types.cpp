#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/catalog_types.hpp>

using namespace erpl_adt;

TEST_CASE("CatalogDomain: ToString round-trips through CatalogDomainFromString",
          "[core][catalog][CatalogDomain]") {
    for (auto domain : {CatalogDomain::Abap, CatalogDomain::Ddic,
                         CatalogDomain::Cds, CatalogDomain::Bw}) {
        auto s = ToString(domain);
        auto parsed = CatalogDomainFromString(s);
        REQUIRE(parsed.IsOk());
        CHECK(parsed.Value() == domain);
    }
}

TEST_CASE("CatalogDomain: unknown string is an error", "[core][catalog][CatalogDomain]") {
    auto parsed = CatalogDomainFromString("NOPE");
    REQUIRE(parsed.IsErr());
}

TEST_CASE("EntityId: rejects empty string", "[core][catalog][EntityId]") {
    auto r = EntityId::Create("");
    REQUIRE(r.IsErr());
}

TEST_CASE("EntityId: accepts a non-empty opaque string", "[core][catalog][EntityId]") {
    auto r = EntityId::Create("abc123");
    REQUIRE(r.IsOk());
    CHECK(r.Value().Value() == "abc123");
}

TEST_CASE("EntityId: equality is value-based", "[core][catalog][EntityId]") {
    auto a = EntityId::Create("abc123").Value();
    auto b = EntityId::Create("abc123").Value();
    auto c = EntityId::Create("xyz789").Value();
    CHECK(a == b);
    CHECK(a != c);
}
