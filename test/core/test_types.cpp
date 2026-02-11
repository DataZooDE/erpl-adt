#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/types.hpp>

#include <functional>
#include <string>
#include <unordered_set>

using namespace erpl_adt;

// ===========================================================================
// PackageName
// ===========================================================================

TEST_CASE("PackageName: valid non-namespace names", "[types][PackageName]") {
    SECTION("simple Z-package") {
        auto r = PackageName::Create("ZTEST_PKG");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "ZTEST_PKG");
    }
    SECTION("Y-package") {
        auto r = PackageName::Create("YFOO");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "YFOO");
    }
    SECTION("$TMP") {
        auto r = PackageName::Create("$TMP");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "$TMP");
    }
    SECTION("single letter") {
        auto r = PackageName::Create("A");
        REQUIRE(r.IsOk());
    }
    SECTION("max 30 chars") {
        std::string name(30, 'Z');
        auto r = PackageName::Create(name);
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("PackageName: valid namespace names", "[types][PackageName]") {
    SECTION("/DMO/ prefix") {
        auto r = PackageName::Create("/DMO/FLIGHT");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "/DMO/FLIGHT");
    }
    SECTION("custom namespace") {
        auto r = PackageName::Create("/ERPL/CORE");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("PackageName: invalid names", "[types][PackageName]") {
    SECTION("empty") {
        auto r = PackageName::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("too long") {
        std::string name(31, 'Z');
        auto r = PackageName::Create(name);
        REQUIRE(r.IsErr());
    }
    SECTION("lowercase") {
        auto r = PackageName::Create("zfoo");
        REQUIRE(r.IsErr());
    }
    SECTION("invalid $ prefix") {
        auto r = PackageName::Create("$FOO");
        REQUIRE(r.IsErr());
    }
    SECTION("namespace missing second slash") {
        auto r = PackageName::Create("/DMO");
        REQUIRE(r.IsErr());
    }
    SECTION("namespace empty namespace part") {
        auto r = PackageName::Create("//NAME");
        REQUIRE(r.IsErr());
    }
    SECTION("namespace empty name part") {
        auto r = PackageName::Create("/DMO/");
        REQUIRE(r.IsErr());
    }
    SECTION("extra slashes") {
        auto r = PackageName::Create("/DMO/FOO/BAR");
        REQUIRE(r.IsErr());
    }
    SECTION("starts with digit") {
        auto r = PackageName::Create("1PKG");
        REQUIRE(r.IsErr());
    }
    SECTION("spaces") {
        auto r = PackageName::Create("Z FOO");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("PackageName: value semantics", "[types][PackageName]") {
    auto r1 = PackageName::Create("ZTEST");
    REQUIRE(r1.IsOk());
    auto p1 = r1.Value();

    SECTION("copy") {
        auto p2 = p1;
        CHECK(p1 == p2);
        CHECK(p2.Value() == "ZTEST");
    }
    SECTION("move") {
        auto p2 = std::move(p1);
        CHECK(p2.Value() == "ZTEST");
    }
    SECTION("equality") {
        auto r2 = PackageName::Create("ZTEST");
        REQUIRE(r2.IsOk());
        CHECK(p1 == r2.Value());
    }
    SECTION("inequality") {
        auto r2 = PackageName::Create("ZOTHER");
        REQUIRE(r2.IsOk());
        CHECK(p1 != r2.Value());
    }
    SECTION("hash") {
        std::unordered_set<PackageName> set;
        set.insert(p1);
        CHECK(set.count(p1) == 1);
    }
}

// ===========================================================================
// RepoUrl
// ===========================================================================

TEST_CASE("RepoUrl: valid URLs", "[types][RepoUrl]") {
    SECTION("GitHub URL") {
        auto r = RepoUrl::Create("https://github.com/SAP-samples/abap-flight.git");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "https://github.com/SAP-samples/abap-flight.git");
    }
    SECTION("minimal") {
        auto r = RepoUrl::Create("https://x");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("RepoUrl: invalid URLs", "[types][RepoUrl]") {
    SECTION("empty") {
        auto r = RepoUrl::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("http not https") {
        auto r = RepoUrl::Create("http://github.com/foo");
        REQUIRE(r.IsErr());
    }
    SECTION("no scheme") {
        auto r = RepoUrl::Create("github.com/foo");
        REQUIRE(r.IsErr());
    }
    SECTION("https:// only") {
        auto r = RepoUrl::Create("https://");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("RepoUrl: value semantics", "[types][RepoUrl]") {
    auto r = RepoUrl::Create("https://github.com/test/repo");
    REQUIRE(r.IsOk());
    auto u1 = r.Value();

    SECTION("copy") {
        auto u2 = u1;
        CHECK(u1 == u2);
    }
    SECTION("move") {
        auto u2 = std::move(u1);
        CHECK(u2.Value() == "https://github.com/test/repo");
    }
    SECTION("hash") {
        std::hash<RepoUrl> h;
        CHECK(h(u1) == h(u1));
    }
}

// ===========================================================================
// BranchRef
// ===========================================================================

TEST_CASE("BranchRef: valid refs", "[types][BranchRef]") {
    SECTION("standard") {
        auto r = BranchRef::Create("refs/heads/main");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "refs/heads/main");
    }
    SECTION("simple name") {
        auto r = BranchRef::Create("main");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("BranchRef: invalid refs", "[types][BranchRef]") {
    SECTION("empty") {
        auto r = BranchRef::Create("");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("BranchRef: value semantics", "[types][BranchRef]") {
    auto r = BranchRef::Create("refs/heads/main");
    REQUIRE(r.IsOk());
    auto b1 = r.Value();
    auto b2 = b1;
    CHECK(b1 == b2);

    auto r2 = BranchRef::Create("refs/heads/dev");
    REQUIRE(r2.IsOk());
    CHECK(b1 != r2.Value());
}

// ===========================================================================
// RepoKey
// ===========================================================================

TEST_CASE("RepoKey: valid keys", "[types][RepoKey]") {
    auto r = RepoKey::Create("ABC123");
    REQUIRE(r.IsOk());
    CHECK(r.Value().Value() == "ABC123");
}

TEST_CASE("RepoKey: invalid keys", "[types][RepoKey]") {
    auto r = RepoKey::Create("");
    REQUIRE(r.IsErr());
}

TEST_CASE("RepoKey: value semantics", "[types][RepoKey]") {
    auto r1 = RepoKey::Create("KEY1");
    auto r2 = RepoKey::Create("KEY1");
    auto r3 = RepoKey::Create("KEY2");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());
}

// ===========================================================================
// SapClient
// ===========================================================================

TEST_CASE("SapClient: valid clients", "[types][SapClient]") {
    SECTION("001") {
        auto r = SapClient::Create("001");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "001");
    }
    SECTION("100") {
        auto r = SapClient::Create("100");
        REQUIRE(r.IsOk());
    }
    SECTION("000") {
        auto r = SapClient::Create("000");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("SapClient: invalid clients", "[types][SapClient]") {
    SECTION("empty") {
        auto r = SapClient::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("too short") {
        auto r = SapClient::Create("01");
        REQUIRE(r.IsErr());
    }
    SECTION("too long") {
        auto r = SapClient::Create("0001");
        REQUIRE(r.IsErr());
    }
    SECTION("letters") {
        auto r = SapClient::Create("ABC");
        REQUIRE(r.IsErr());
    }
    SECTION("mixed") {
        auto r = SapClient::Create("0A1");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("SapClient: value semantics", "[types][SapClient]") {
    auto r1 = SapClient::Create("001");
    auto r2 = SapClient::Create("001");
    auto r3 = SapClient::Create("100");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());

    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    auto c = r1.Value();
    auto moved = std::move(c);
    CHECK(moved.Value() == "001");
}

// ===========================================================================
// ObjectUri
// ===========================================================================

TEST_CASE("ObjectUri: valid URIs", "[types][ObjectUri]") {
    SECTION("class URI") {
        auto r = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_TEST");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "/sap/bc/adt/oo/classes/ZCL_TEST");
    }
    SECTION("program URI") {
        auto r = ObjectUri::Create("/sap/bc/adt/programs/programs/ZTEST_PROG");
        REQUIRE(r.IsOk());
    }
    SECTION("discovery URI") {
        auto r = ObjectUri::Create("/sap/bc/adt/discovery");
        REQUIRE(r.IsOk());
    }
    SECTION("abapgit repos URI") {
        auto r = ObjectUri::Create("/sap/bc/adt/abapgit/repos");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("ObjectUri: invalid URIs", "[types][ObjectUri]") {
    SECTION("empty") {
        auto r = ObjectUri::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("wrong prefix") {
        auto r = ObjectUri::Create("/sap/opu/odata/something");
        REQUIRE(r.IsErr());
    }
    SECTION("just the prefix") {
        auto r = ObjectUri::Create("/sap/bc/adt/");
        REQUIRE(r.IsErr());
    }
    SECTION("no leading slash") {
        auto r = ObjectUri::Create("sap/bc/adt/oo/classes/ZCL_TEST");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("ObjectUri: value semantics", "[types][ObjectUri]") {
    auto r1 = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_A");
    auto r2 = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_A");
    auto r3 = ObjectUri::Create("/sap/bc/adt/oo/classes/ZCL_B");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    SECTION("hash") {
        std::unordered_set<ObjectUri> set;
        set.insert(r1.Value());
        CHECK(set.count(r1.Value()) == 1);
    }
}

// ===========================================================================
// ObjectType
// ===========================================================================

TEST_CASE("ObjectType: valid types", "[types][ObjectType]") {
    SECTION("class") {
        auto r = ObjectType::Create("CLAS/OC");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "CLAS/OC");
    }
    SECTION("program") {
        auto r = ObjectType::Create("PROG/P");
        REQUIRE(r.IsOk());
    }
    SECTION("function group") {
        auto r = ObjectType::Create("FUGR/F");
        REQUIRE(r.IsOk());
    }
    SECTION("with digits and underscores") {
        auto r = ObjectType::Create("TABL/DT01");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("ObjectType: invalid types", "[types][ObjectType]") {
    SECTION("empty") {
        auto r = ObjectType::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("no slash") {
        auto r = ObjectType::Create("CLAS");
        REQUIRE(r.IsErr());
    }
    SECTION("empty category") {
        auto r = ObjectType::Create("/OC");
        REQUIRE(r.IsErr());
    }
    SECTION("empty subcategory") {
        auto r = ObjectType::Create("CLAS/");
        REQUIRE(r.IsErr());
    }
    SECTION("lowercase") {
        auto r = ObjectType::Create("clas/oc");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("ObjectType: value semantics", "[types][ObjectType]") {
    auto r1 = ObjectType::Create("CLAS/OC");
    auto r2 = ObjectType::Create("CLAS/OC");
    auto r3 = ObjectType::Create("PROG/P");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    SECTION("hash") {
        std::unordered_set<ObjectType> set;
        set.insert(r1.Value());
        CHECK(set.count(r1.Value()) == 1);
    }
}

// ===========================================================================
// TransportId
// ===========================================================================

TEST_CASE("TransportId: valid IDs", "[types][TransportId]") {
    SECTION("standard") {
        auto r = TransportId::Create("NPLK900001");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "NPLK900001");
    }
    SECTION("another system") {
        auto r = TransportId::Create("DEVK000042");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("TransportId: invalid IDs", "[types][TransportId]") {
    SECTION("empty") {
        auto r = TransportId::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("too short") {
        auto r = TransportId::Create("NPLK9000");
        REQUIRE(r.IsErr());
    }
    SECTION("too long") {
        auto r = TransportId::Create("NPLK9000011");
        REQUIRE(r.IsErr());
    }
    SECTION("lowercase letters") {
        auto r = TransportId::Create("nplk900001");
        REQUIRE(r.IsErr());
    }
    SECTION("digits in letter part") {
        auto r = TransportId::Create("1PLK900001");
        REQUIRE(r.IsErr());
    }
    SECTION("letters in digit part") {
        auto r = TransportId::Create("NPLK90000A");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("TransportId: value semantics", "[types][TransportId]") {
    auto r1 = TransportId::Create("NPLK900001");
    auto r2 = TransportId::Create("NPLK900001");
    auto r3 = TransportId::Create("NPLK900002");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    SECTION("hash") {
        std::unordered_set<TransportId> set;
        set.insert(r1.Value());
        CHECK(set.count(r1.Value()) == 1);
    }
}

// ===========================================================================
// LockHandle
// ===========================================================================

TEST_CASE("LockHandle: valid handles", "[types][LockHandle]") {
    SECTION("opaque string") {
        auto r = LockHandle::Create("abc123-lock-handle-xyz");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "abc123-lock-handle-xyz");
    }
    SECTION("single char") {
        auto r = LockHandle::Create("x");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("LockHandle: invalid handles", "[types][LockHandle]") {
    SECTION("empty") {
        auto r = LockHandle::Create("");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("LockHandle: value semantics", "[types][LockHandle]") {
    auto r1 = LockHandle::Create("handle1");
    auto r2 = LockHandle::Create("handle1");
    auto r3 = LockHandle::Create("handle2");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    SECTION("hash") {
        std::unordered_set<LockHandle> set;
        set.insert(r1.Value());
        CHECK(set.count(r1.Value()) == 1);
    }
}

// ===========================================================================
// CheckVariant
// ===========================================================================

TEST_CASE("CheckVariant: valid variants", "[types][CheckVariant]") {
    SECTION("standard") {
        auto r = CheckVariant::Create("FUNCTIONAL_DB_ADDITION");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "FUNCTIONAL_DB_ADDITION");
    }
    SECTION("simple name") {
        auto r = CheckVariant::Create("DEFAULT");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("CheckVariant: invalid variants", "[types][CheckVariant]") {
    SECTION("empty") {
        auto r = CheckVariant::Create("");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("CheckVariant: value semantics", "[types][CheckVariant]") {
    auto r1 = CheckVariant::Create("VARIANT_A");
    auto r2 = CheckVariant::Create("VARIANT_A");
    auto r3 = CheckVariant::Create("VARIANT_B");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    SECTION("hash") {
        std::unordered_set<CheckVariant> set;
        set.insert(r1.Value());
        CHECK(set.count(r1.Value()) == 1);
    }
}

// ===========================================================================
// SapLanguage
// ===========================================================================

TEST_CASE("SapLanguage: valid languages", "[types][SapLanguage]") {
    SECTION("English") {
        auto r = SapLanguage::Create("EN");
        REQUIRE(r.IsOk());
        CHECK(r.Value().Value() == "EN");
    }
    SECTION("German") {
        auto r = SapLanguage::Create("DE");
        REQUIRE(r.IsOk());
    }
}

TEST_CASE("SapLanguage: invalid languages", "[types][SapLanguage]") {
    SECTION("empty") {
        auto r = SapLanguage::Create("");
        REQUIRE(r.IsErr());
    }
    SECTION("too short") {
        auto r = SapLanguage::Create("E");
        REQUIRE(r.IsErr());
    }
    SECTION("too long") {
        auto r = SapLanguage::Create("ENG");
        REQUIRE(r.IsErr());
    }
    SECTION("lowercase") {
        auto r = SapLanguage::Create("en");
        REQUIRE(r.IsErr());
    }
    SECTION("digits") {
        auto r = SapLanguage::Create("E1");
        REQUIRE(r.IsErr());
    }
}

TEST_CASE("SapLanguage: value semantics", "[types][SapLanguage]") {
    auto r1 = SapLanguage::Create("EN");
    auto r2 = SapLanguage::Create("EN");
    auto r3 = SapLanguage::Create("DE");
    REQUIRE(r1.IsOk());
    REQUIRE(r2.IsOk());
    REQUIRE(r3.IsOk());
    CHECK(r1.Value() == r2.Value());
    CHECK(r1.Value() != r3.Value());

    SECTION("hash") {
        std::unordered_set<SapLanguage> set;
        set.insert(r1.Value());
        CHECK(set.count(r1.Value()) == 1);
    }
}
