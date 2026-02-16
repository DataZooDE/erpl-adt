#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/adt/bw_activation.hpp>
#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

std::string TestDataPath(const std::string& filename) {
    std::string this_file = __FILE__;
    auto last_slash = this_file.find_last_of("/\\");
    auto test_dir = this_file.substr(0, last_slash);
    auto test_root = test_dir.substr(0, test_dir.find_last_of("/\\"));
    return test_root + "/testdata/" + filename;
}

std::string LoadFixture(const std::string& filename) {
    std::ifstream in(TestDataPath(filename));
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

BwActivateOptions MakeActivateOptions(const std::string& type,
                                       const std::string& name) {
    BwActivateOptions opts;
    BwActivationObject obj;
    obj.name = name;
    obj.type = type;
    obj.uri = "/sap/bw/modeling/" + type + "/" + name + "/m";
    opts.objects.push_back(std::move(obj));
    return opts;
}

} // anonymous namespace

// ===========================================================================
// BwActivateObjects — success cases
// ===========================================================================

TEST_CASE("BwActivateObjects: sync activation success", "[adt][bw][activation]") {
    MockAdtSession mock;
    auto xml = LoadFixture("bw/bw_activation.xml");
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, xml}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES_DATA");
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());
    CHECK(result.Value().success);
}

TEST_CASE("BwActivateObjects: sends correct URL for activate mode", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    auto& path = mock.PostCalls()[0].path;
    CHECK(path.find("mode=activate") != std::string::npos);
    CHECK(path.find("simu=false") != std::string::npos);
}

TEST_CASE("BwActivateObjects: validate mode sends mode=validate", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Validate;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("mode=validate") != std::string::npos);
}

TEST_CASE("BwActivateObjects: validate mode sends sort/onlyina flags", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Validate;
    opts.sort = true;
    opts.only_inactive = true;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("sort=true") != std::string::npos);
    CHECK(mock.PostCalls()[0].path.find("onlyina=true") != std::string::npos);
}

TEST_CASE("BwActivateObjects: simulate mode sends simu=true", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Simulate;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("simu=true") != std::string::npos);
}

TEST_CASE("BwActivateObjects: background mode sends asjob=true", "[adt][bw][activation]") {
    MockAdtSession mock;
    HttpHeaders headers;
    headers["Location"] = "/sap/bw/modeling/jobs/ABC12345678901234567890";
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({202, headers, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.mode = BwActivationMode::Background;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("asjob=true") != std::string::npos);
    CHECK(result.Value().job_guid == "ABC12345678901234567890");
}

TEST_CASE("BwActivateObjects: transport appended to URL", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.transport = "K900001";
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].path.find("corrnum=K900001") != std::string::npos);
}

TEST_CASE("BwActivateObjects: sends massact content type", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    CHECK(mock.PostCalls()[0].content_type == "application/vnd.sap-bw-modeling.massact+xml");
}

TEST_CASE("BwActivateObjects: body contains object XML", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("objectName=\"ZSALES\"") != std::string::npos);
    CHECK(body.find("objectType=\"ADSO\"") != std::string::npos);
    CHECK(body.find("bwActivation:objects") != std::string::npos);
}

TEST_CASE("BwActivateObjects: body includes execChk and withCTO root attributes",
          "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    opts.exec_checks = true;
    opts.with_cto = true;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("execChk=\"true\"") != std::string::npos);
    CHECK(body.find("withCTO=\"true\"") != std::string::npos);
}

TEST_CASE("BwActivateObjects: endpoint override is used", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, "<result/>"}));

    BwActivateOptions opts;
    opts.endpoint_override = "/sap/bw/modeling/activation/custom";
    BwActivationObject object;
    object.name = "ZADSO001";
    object.type = "ADSO";
    opts.objects.push_back(std::move(object));

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    REQUIRE(mock.PostCallCount() == 1);
    CHECK(mock.PostCalls()[0].path.find("/sap/bw/modeling/activation/custom") == 0);
}

TEST_CASE("BwActivateObjects: escapes XML attribute values", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwActivateOptions opts;
    BwActivationObject obj;
    obj.name = "Z&A\"<B>";
    obj.type = "AD&SO";
    obj.description = "desc <bad> & \"quote\"";
    obj.package_name = "ZP&KG";
    obj.transport = "K9&001";
    obj.uri = "/sap/bw/modeling/adso/Z&A";
    opts.objects.push_back(std::move(obj));

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    const auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("objectName=\"Z&amp;A&quot;&lt;B&gt;\"") != std::string::npos);
    CHECK(body.find("objectType=\"AD&amp;SO\"") != std::string::npos);
    CHECK(body.find("objectDesc=\"desc &lt;bad&gt; &amp; &quot;quote&quot;\"") != std::string::npos);
    CHECK(body.find("package=\"ZP&amp;KG\"") != std::string::npos);
}

TEST_CASE("BwActivateObjects: empty objects returns error", "[adt][bw][activation]") {
    MockAdtSession mock;
    BwActivateOptions opts;
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsErr());
    CHECK(result.Error().message.find("No objects") != std::string::npos);
}

TEST_CASE("BwActivateObjects: HTTP error propagated", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({500, {}, "Internal Error"}));

    auto opts = MakeActivateOptions("ADSO", "ZSALES");
    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsErr());
}

TEST_CASE("BwActivateObjects: multi-object activation", "[adt][bw][activation]") {
    MockAdtSession mock;
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, ""}));

    BwActivateOptions opts;
    for (auto& name : {"ZSALES1", "ZSALES2", "ZSALES3"}) {
        BwActivationObject obj;
        obj.name = name;
        obj.type = "ADSO";
        opts.objects.push_back(std::move(obj));
    }

    auto result = BwActivateObjects(mock, opts);
    REQUIRE(result.IsOk());

    auto& body = mock.PostCalls()[0].body;
    CHECK(body.find("ZSALES1") != std::string::npos);
    CHECK(body.find("ZSALES2") != std::string::npos);
    CHECK(body.find("ZSALES3") != std::string::npos);
}
