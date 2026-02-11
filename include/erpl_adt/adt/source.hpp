#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// ReadSource — read the source code of an ABAP object.
//
// Endpoint: GET {sourceUri}?version={version}
// Accept: text/plain
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string, Error> ReadSource(
    IAdtSession& session,
    const std::string& source_uri,
    const std::string& version = "active");

// ---------------------------------------------------------------------------
// WriteSource — write source code to an ABAP object.
//
// Endpoint: PUT {sourceUri}?lockHandle={handle}&corrNr={transport}
// Content-Type: text/plain; charset=utf-8
// Requires stateful session + lock.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> WriteSource(
    IAdtSession& session,
    const std::string& source_uri,
    const std::string& source,
    const LockHandle& lock_handle,
    const std::optional<std::string>& transport_number = std::nullopt);

// ---------------------------------------------------------------------------
// SyntaxMessage — a single message from a syntax check.
// ---------------------------------------------------------------------------
struct SyntaxMessage {
    std::string type;       // "E", "W", "I", "A", "X", "S"
    std::string text;
    std::string uri;
    int line = 0;
    int offset = 0;
};

// ---------------------------------------------------------------------------
// CheckSyntax — run a syntax check on an ABAP object.
//
// Endpoint: POST /sap/bc/adt/checkruns?reporters=abapCheckRun
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::vector<SyntaxMessage>, Error> CheckSyntax(
    IAdtSession& session,
    const std::string& source_uri);

} // namespace erpl_adt
