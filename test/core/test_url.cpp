#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/core/url.hpp>

using namespace erpl_adt;

TEST_CASE("UrlEncode: alphanumeric passthrough", "[core][url]") {
    CHECK(UrlEncode("abc123") == "abc123");
}

TEST_CASE("UrlEncode: unreserved chars passthrough", "[core][url]") {
    CHECK(UrlEncode("a-b_c.d~e") == "a-b_c.d~e");
}

TEST_CASE("UrlEncode: asterisk encoded", "[core][url]") {
    CHECK(UrlEncode("Z*") == "Z%2A");
    CHECK(UrlEncode("*TEST*") == "%2ATEST%2A");
}

TEST_CASE("UrlEncode: slash encoded", "[core][url]") {
    CHECK(UrlEncode("/DMO/FLIGHT") == "%2FDMO%2FFLIGHT");
}

TEST_CASE("UrlEncode: space encoded", "[core][url]") {
    CHECK(UrlEncode("hello world") == "hello%20world");
}

TEST_CASE("UrlEncode: empty string", "[core][url]") {
    CHECK(UrlEncode("") == "");
}

TEST_CASE("UrlEncode: special characters", "[core][url]") {
    CHECK(UrlEncode("a=b&c") == "a%3Db%26c");
}
