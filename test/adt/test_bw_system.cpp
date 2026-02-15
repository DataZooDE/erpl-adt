#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_system.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

// ===========================================================================
// BwGetDbInfo
// ===========================================================================

TEST_CASE("BwGetDbInfo: parses flat attribute response", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <dbinfo dbHost="hanahost" dbPort="30015"
                dbSchema="SAPABAP1" dbType="HDB"/>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsOk());

    const auto& info = result.Value();
    CHECK(info.host == "hanahost");
    CHECK(info.port == "30015");
    CHECK(info.schema == "SAPABAP1");
    CHECK(info.database_type == "HDB");
}

TEST_CASE("BwGetDbInfo: parses Atom entry format", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <content type="application/xml">
                    <properties host="dbhost.local" port="30013"
                                schema="BW4HANA" databaseType="HDB"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsOk());

    CHECK(result.Value().host == "dbhost.local");
    CHECK(result.Value().port == "30013");
    CHECK(result.Value().schema == "BW4HANA");
}

TEST_CASE("BwGetDbInfo: sends correct URL and Accept header", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<dbinfo/>"}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsOk());

    REQUIRE(mock.GetCallCount() == 1);
    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/dbinfo");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwGetDbInfo: HTTP error propagated", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsErr());
}

TEST_CASE("BwGetDbInfo: connection error propagated", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Err(Error{
        "Get", "/sap/bw/modeling/repo/is/dbinfo",
        std::nullopt, "Connection refused", std::nullopt}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetSystemInfo
// ===========================================================================

TEST_CASE("BwGetSystemInfo: parses Atom entries", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>System ID</title>
                <content type="application/xml">
                    <properties key="SID" value="BW4"/>
                </content>
            </entry>
            <entry>
                <title>System Type</title>
                <content type="application/xml">
                    <properties key="SYS_TYPE" value="BW4HANA"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetSystemInfo(mock);
    REQUIRE(result.IsOk());

    const auto& props = result.Value();
    REQUIRE(props.size() == 2);
    CHECK(props[0].key == "SID");
    CHECK(props[0].value == "BW4");
    CHECK(props[0].description == "System ID");
    CHECK(props[1].key == "SYS_TYPE");
    CHECK(props[1].value == "BW4HANA");
}

TEST_CASE("BwGetSystemInfo: parses flat property elements", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <systeminfo>
            <property key="SID" value="BW4" description="System ID"/>
            <property key="RELEASE" value="2022" description="Release"/>
        </systeminfo>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetSystemInfo(mock);
    REQUIRE(result.IsOk());

    const auto& props = result.Value();
    REQUIRE(props.size() == 2);
    CHECK(props[0].key == "SID");
    CHECK(props[1].key == "RELEASE");
    CHECK(props[1].value == "2022");
}

TEST_CASE("BwGetSystemInfo: sends correct URL", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<systeminfo/>"}));

    auto result = BwGetSystemInfo(mock);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/systeminfo");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwGetSystemInfo: empty response returns empty vector", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<systeminfo/>"}));

    auto result = BwGetSystemInfo(mock);
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwGetSystemInfo: HTTP error propagated", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "Not found"}));

    auto result = BwGetSystemInfo(mock);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetChangeability
// ===========================================================================

TEST_CASE("BwGetChangeability: parses Atom entries", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <title>Advanced DataStore Object</title>
                <content type="application/xml">
                    <properties objectType="ADSO" changeable="X"
                                transportable="X"/>
                </content>
            </entry>
            <entry>
                <title>InfoObject</title>
                <content type="application/xml">
                    <properties objectType="IOBJ" changeable="X"
                                transportable=""/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetChangeability(mock);
    REQUIRE(result.IsOk());

    const auto& entries = result.Value();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].object_type == "ADSO");
    CHECK(entries[0].changeable == "X");
    CHECK(entries[0].transportable == "X");
    CHECK(entries[0].description == "Advanced DataStore Object");
    CHECK(entries[1].object_type == "IOBJ");
    CHECK(entries[1].transportable == "");
}

TEST_CASE("BwGetChangeability: parses flat elements", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <changeabilities>
            <chginfo objectType="ADSO" changeable="X" transportable="X"
                     description="Advanced DataStore Object"/>
            <chginfo objectType="TRFN" changeable="" transportable="X"
                     description="Transformation"/>
        </changeabilities>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetChangeability(mock);
    REQUIRE(result.IsOk());

    const auto& entries = result.Value();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].object_type == "ADSO");
    CHECK(entries[1].object_type == "TRFN");
    CHECK(entries[1].changeable == "");
}

TEST_CASE("BwGetChangeability: sends correct URL", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<chginfo/>"}));

    auto result = BwGetChangeability(mock);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/chginfo");
}

TEST_CASE("BwGetChangeability: HTTP error propagated", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({403, {}, "Forbidden"}));

    auto result = BwGetChangeability(mock);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// BwGetAdtUriMappings
// ===========================================================================

TEST_CASE("BwGetAdtUriMappings: parses Atom entries", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom">
            <entry>
                <content type="application/xml">
                    <properties bwType="ADSO" adtType="DDLS"
                                bwUri="/sap/bw/modeling/adso/{name}"
                                adtUri="/sap/bc/adt/ddic/ddl/sources/{name}"/>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetAdtUriMappings(mock);
    REQUIRE(result.IsOk());

    const auto& mappings = result.Value();
    REQUIRE(mappings.size() == 1);
    CHECK(mappings[0].bw_type == "ADSO");
    CHECK(mappings[0].adt_type == "DDLS");
    CHECK(mappings[0].bw_uri_template.find("adso") != std::string::npos);
    CHECK(mappings[0].adt_uri_template.find("ddl") != std::string::npos);
}

TEST_CASE("BwGetAdtUriMappings: parses flat mapping elements", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <adturi>
            <mapping bwType="ADSO" adtType="DDLS"
                     bwUri="/sap/bw/modeling/adso/{name}"
                     adtUri="/sap/bc/adt/ddic/ddl/sources/{name}"/>
            <mapping bwType="IOBJ" adtType="DTEL"
                     bwUri="/sap/bw/modeling/iobj/{name}"
                     adtUri="/sap/bc/adt/ddic/dataelements/{name}"/>
        </adturi>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetAdtUriMappings(mock);
    REQUIRE(result.IsOk());

    const auto& mappings = result.Value();
    REQUIRE(mappings.size() == 2);
    CHECK(mappings[0].bw_type == "ADSO");
    CHECK(mappings[1].bw_type == "IOBJ");
}

TEST_CASE("BwGetAdtUriMappings: sends correct URL", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<adturi/>"}));

    auto result = BwGetAdtUriMappings(mock);
    REQUIRE(result.IsOk());

    CHECK(mock.GetCalls()[0].path == "/sap/bw/modeling/repo/is/adturi");
    CHECK(mock.GetCalls()[0].headers.at("Accept") == "application/atom+xml");
}

TEST_CASE("BwGetAdtUriMappings: empty response returns empty vector", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, "<adturi/>"}));

    auto result = BwGetAdtUriMappings(mock);
    REQUIRE(result.IsOk());
    CHECK(result.Value().empty());
}

TEST_CASE("BwGetAdtUriMappings: HTTP error propagated", "[adt][bw][system]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({500, {}, "Error"}));

    auto result = BwGetAdtUriMappings(mock);
    REQUIRE(result.IsErr());
}

// ===========================================================================
// OData child-element text format tests
// ===========================================================================

TEST_CASE("BwGetDbInfo: parses real SAP BW format with connect element", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <atom:feed xmlns:atom="http://www.w3.org/2005/Atom"
                   xmlns:dbInfo="http://www.sap.com/bw/modeling/DBInfo">
            <atom:entry>
                <atom:content type="application/xml">
                    <dbInfo:dbInfo>
                        <dbInfo:name>HDB</dbInfo:name>
                        <dbInfo:type>HDB</dbInfo:type>
                        <dbInfo:version server="2.00.075.00.1716717954"/>
                        <dbInfo:patchlevel>101</dbInfo:patchlevel>
                        <dbInfo:connect host="vhcala4hci" instance="02" port="30215" user="SAPA4H"/>
                        <dbInfo:schema>SAPA4H</dbInfo:schema>
                    </dbInfo:dbInfo>
                </atom:content>
            </atom:entry>
        </atom:feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsOk());

    const auto& info = result.Value();
    CHECK(info.host == "vhcala4hci");
    CHECK(info.port == "30215");
    CHECK(info.schema == "SAPA4H");
    CHECK(info.database_type == "HDB");
    CHECK(info.database_name == "HDB");
    CHECK(info.instance == "02");
    CHECK(info.user == "SAPA4H");
    CHECK(info.version == "2.00.075.00.1716717954");
    CHECK(info.patchlevel == "101");
}

TEST_CASE("BwGetDbInfo: parses OData child-element format", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom"
              xmlns:m="http://schemas.microsoft.com/ado/2007/08/dataservices/metadata"
              xmlns:d="http://schemas.microsoft.com/ado/2007/08/dataservices">
            <entry>
                <content type="application/xml">
                    <m:properties>
                        <d:dbHost>hanahost</d:dbHost>
                        <d:dbPort>30015</d:dbPort>
                        <d:dbSchema>SAPABAP1</d:dbSchema>
                        <d:dbType>HDB</d:dbType>
                    </m:properties>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsOk());

    const auto& info = result.Value();
    CHECK(info.host == "hanahost");
    CHECK(info.port == "30015");
    CHECK(info.schema == "SAPABAP1");
    CHECK(info.database_type == "HDB");
}

TEST_CASE("BwGetDbInfo: parses plain child-element format on root", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <dbinfo>
            <dbHost>myhost</dbHost>
            <dbPort>30013</dbPort>
            <dbSchema>BW4</dbSchema>
            <dbType>HDB</dbType>
        </dbinfo>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetDbInfo(mock);
    REQUIRE(result.IsOk());

    CHECK(result.Value().host == "myhost");
    CHECK(result.Value().port == "30013");
    CHECK(result.Value().schema == "BW4");
    CHECK(result.Value().database_type == "HDB");
}

TEST_CASE("BwGetSystemInfo: parses OData child-element format", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom"
              xmlns:m="http://schemas.microsoft.com/ado/2007/08/dataservices/metadata"
              xmlns:d="http://schemas.microsoft.com/ado/2007/08/dataservices">
            <entry>
                <title>System ID</title>
                <content type="application/xml">
                    <m:properties>
                        <d:key>SID</d:key>
                        <d:value>BW4</d:value>
                    </m:properties>
                </content>
            </entry>
            <entry>
                <title>Release</title>
                <content type="application/xml">
                    <m:properties>
                        <d:key>RELEASE</d:key>
                        <d:value>2022</d:value>
                        <d:description>SAP Release</d:description>
                    </m:properties>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetSystemInfo(mock);
    REQUIRE(result.IsOk());

    const auto& props = result.Value();
    REQUIRE(props.size() == 2);
    CHECK(props[0].key == "SID");
    CHECK(props[0].value == "BW4");
    CHECK(props[0].description == "System ID");
    CHECK(props[1].key == "RELEASE");
    CHECK(props[1].value == "2022");
    CHECK(props[1].description == "Release");  // <title> takes priority over <d:description>
}

TEST_CASE("BwGetChangeability: parses OData child-element format", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom"
              xmlns:m="http://schemas.microsoft.com/ado/2007/08/dataservices/metadata"
              xmlns:d="http://schemas.microsoft.com/ado/2007/08/dataservices">
            <entry>
                <title>Advanced DataStore Object</title>
                <content type="application/xml">
                    <m:properties>
                        <d:objectType>ADSO</d:objectType>
                        <d:changeable>X</d:changeable>
                        <d:transportable>X</d:transportable>
                    </m:properties>
                </content>
            </entry>
            <entry>
                <title>InfoObject</title>
                <content type="application/xml">
                    <m:properties>
                        <d:objectType>IOBJ</d:objectType>
                        <d:changeable>X</d:changeable>
                        <d:transportable></d:transportable>
                    </m:properties>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetChangeability(mock);
    REQUIRE(result.IsOk());

    const auto& entries = result.Value();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].object_type == "ADSO");
    CHECK(entries[0].changeable == "X");
    CHECK(entries[0].transportable == "X");
    CHECK(entries[0].description == "Advanced DataStore Object");
    CHECK(entries[1].object_type == "IOBJ");
    CHECK(entries[1].transportable == "");
}

TEST_CASE("BwGetAdtUriMappings: parses OData child-element format", "[adt][bw][system]") {
    MockAdtSession mock;
    std::string xml = R"(
        <feed xmlns="http://www.w3.org/2005/Atom"
              xmlns:m="http://schemas.microsoft.com/ado/2007/08/dataservices/metadata"
              xmlns:d="http://schemas.microsoft.com/ado/2007/08/dataservices">
            <entry>
                <content type="application/xml">
                    <m:properties>
                        <d:bwType>ADSO</d:bwType>
                        <d:adtType>DDLS</d:adtType>
                        <d:bwUri>/sap/bw/modeling/adso/{name}</d:bwUri>
                        <d:adtUri>/sap/bc/adt/ddic/ddl/sources/{name}</d:adtUri>
                    </m:properties>
                </content>
            </entry>
        </feed>
    )";
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto result = BwGetAdtUriMappings(mock);
    REQUIRE(result.IsOk());

    const auto& mappings = result.Value();
    REQUIRE(mappings.size() == 1);
    CHECK(mappings[0].bw_type == "ADSO");
    CHECK(mappings[0].adt_type == "DDLS");
    CHECK(mappings[0].bw_uri_template == "/sap/bw/modeling/adso/{name}");
    CHECK(mappings[0].adt_uri_template == "/sap/bc/adt/ddic/ddl/sources/{name}");
}
