#include <catch2/catch_test_macros.hpp>

#include <erpl_adt/cli/command_executor.hpp>
#include <erpl_adt/adt/activation.hpp>

#include "../../test/mocks/mock_adt_session.hpp"

#include <fstream>
#include <sstream>
#include <string>

using namespace erpl_adt;
using namespace erpl_adt::testing;

namespace {

// Fixture XML for a successful lock (matches the SAP ABAP XML format used in other tests).
const char* kLockXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<asx:abap xmlns:asx="http://www.sap.com/abapxml">
  <asx:values>
    <DATA>
      <LOCK_HANDLE>lock_handle_abc123</LOCK_HANDLE>
      <CORRNR>NPLK900001</CORRNR>
      <CORRUSER>DEVELOPER</CORRUSER>
      <CORRTEXT>Test transport</CORRTEXT>
      <IS_LOCAL>X</IS_LOCAL>
    </DATA>
  </asx:values>
</asx:abap>)";

// Activation success response XML (chkl namespace, no errors).
const char* kActivationXml = R"(<?xml version="1.0" encoding="utf-8"?>
<chkl:activationResultList xmlns:chkl="http://www.sap.com/adt/checklistresult">
</chkl:activationResultList>)";

const std::string kSourceUri = "/sap/bc/adt/oo/classes/zcl_test/source/main";
const std::string kObjectUri  = "/sap/bc/adt/oo/classes/zcl_test";
const std::string kOriginal   = "CLASS zcl_test DEFINITION PUBLIC.\nENDCLASS.\n";
const std::string kModified   = "CLASS zcl_test DEFINITION PUBLIC.\n* changed\nENDCLASS.\n";

// An editor fn that does nothing (simulates closing without saving).
SourceEditorFn NoOpEditor() {
    return [](const std::string&) -> int { return 0; };
}

// An editor fn that overwrites the temp file with new content.
SourceEditorFn ReplacingEditor(const std::string& new_content) {
    return [new_content](const std::string& path) -> int {
        std::ofstream out(path);
        out << new_content;
        return 0;
    };
}

} // anonymous namespace

// ===========================================================================
// No-change path
// ===========================================================================

TEST_CASE("RunSourceEdit: no-change exits 0 without writing", "[source][edit]") {
    MockAdtSession mock;
    // Only a GET for ReadSource — no PUT or lock POST.
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kOriginal}));

    std::ostringstream out, err;
    int rc = RunSourceEdit(mock, kSourceUri, std::nullopt, false, false,
                           NoOpEditor(), out, err);

    CHECK(rc == 0);
    CHECK(mock.GetCallCount() == 1);
    CHECK(mock.PostCallCount() == 0);  // No lock POST.
    CHECK(mock.PutCallCount() == 0);   // No source PUT.
    CHECK(out.str().find("No changes") != std::string::npos);
}

// ===========================================================================
// Changed path
// ===========================================================================

TEST_CASE("RunSourceEdit: changed content triggers write", "[source][edit]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kOriginal}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockXml}));  // Lock.
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));         // Write.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));        // Unlock.

    std::ostringstream out, err;
    int rc = RunSourceEdit(mock, kSourceUri, std::nullopt, false, false,
                           ReplacingEditor(kModified), out, err);

    CHECK(rc == 0);
    CHECK(mock.GetCallCount() == 1);
    CHECK(mock.PutCallCount() == 1);
    // PUT body contains the modified source.
    REQUIRE(mock.PutCalls().size() == 1);
    CHECK(mock.PutCalls()[0].body == kModified);
}

// ===========================================================================
// Write error path
// ===========================================================================

TEST_CASE("RunSourceEdit: write error returns non-zero exit code", "[source][edit]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kOriginal}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockXml}));  // Lock OK.
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({409, {}, "Locked by other user"}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));        // Unlock.

    std::ostringstream out, err;
    int rc = RunSourceEdit(mock, kSourceUri, std::nullopt, false, false,
                           ReplacingEditor(kModified), out, err);

    CHECK(rc != 0);
    CHECK(err.str().find("409") != std::string::npos);
    CHECK(mock.PutCallCount() == 1);
}

// ===========================================================================
// --no-write flag
// ===========================================================================

TEST_CASE("RunSourceEdit: --no-write skips write even when content changed", "[source][edit]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kOriginal}));
    // No lock/write enqueued — if write is attempted the mock will return an error.

    std::ostringstream out, err;
    int rc = RunSourceEdit(mock, kSourceUri, std::nullopt, false,
                           /*no_write=*/true,
                           ReplacingEditor(kModified), out, err);

    CHECK(rc == 0);
    CHECK(mock.PutCallCount() == 0);
    CHECK(mock.PostCallCount() == 0);
}

// ===========================================================================
// --activate flag
// ===========================================================================

TEST_CASE("RunSourceEdit: --activate calls ActivateObject after write", "[source][edit]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({200, {}, kOriginal}));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kLockXml}));        // Lock.
    mock.EnqueuePut(Result<HttpResponse, Error>::Ok({200, {}, ""}));               // Write.
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({204, {}, ""}));              // Unlock.
    // ActivateObject fetches a CSRF token, then POSTs.
    mock.EnqueueCsrfToken(Result<std::string, Error>::Ok(std::string("csrf-tok")));
    mock.EnqueuePost(Result<HttpResponse, Error>::Ok({200, {}, kActivationXml}));  // Activate.

    std::ostringstream out, err;
    int rc = RunSourceEdit(mock, kSourceUri, std::nullopt, /*activate=*/true, false,
                           ReplacingEditor(kModified), out, err);

    CHECK(rc == 0);
    // POST count: lock + unlock + activate = 3.
    CHECK(mock.PostCallCount() == 3);
    CHECK(out.str().find("Activated") != std::string::npos);
}

// ===========================================================================
// ReadSource error path
// ===========================================================================

TEST_CASE("RunSourceEdit: read error returns non-zero exit code", "[source][edit]") {
    MockAdtSession mock;
    mock.EnqueueGet(Result<HttpResponse, Error>::Ok({404, {}, "not found"}));

    std::ostringstream out, err;
    int rc = RunSourceEdit(mock, kSourceUri, std::nullopt, false, false,
                           NoOpEditor(), out, err);

    CHECK(rc != 0);
    CHECK(mock.PostCallCount() == 0);
    CHECK(mock.PutCallCount() == 0);
    CHECK(err.str().find("404") != std::string::npos);
}
