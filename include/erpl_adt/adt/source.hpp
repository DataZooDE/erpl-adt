#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>
#include <erpl_adt/core/types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// DeriveObjectUriFromSourceUri — derive the owning object URI from a source
// (section) URI by stripping the trailing section segment.
//
// Recognizes both section forms used by ADT:
//   .../source/main          (most objects: classes, programs, function modules)
//   .../includes/testclasses (class includes: definitions, implementations,
//                             macros, testclasses)
//
// `/source/` is preferred over `/includes/` so that program-include source
// URIs (e.g. /sap/bc/adt/programs/includes/zincl/source/main), where
// `/includes/` is part of the object path rather than the section, still strip
// at the correct `/source/` boundary.
//
// Returns std::nullopt when no recognizable section segment is present.
// ---------------------------------------------------------------------------
[[nodiscard]] std::optional<std::string> DeriveObjectUriFromSourceUri(
    std::string_view source_uri);

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
// WriteSourceOptimistic — attempt a lockless write (no lockHandle).
//
// Endpoint: PUT {sourceUri}[?corrNr={transport}]
// Returns Ok if the server accepts the write without a lock handle.
// Returns Err (any non-2xx) if the server rejects it — caller should fall
// back to the standard lock+write flow.
//
// Safe to use when the caller is the only writer (agent workflows, single-
// user dev). No lock is created, so there is no risk of a stuck lock handle.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<void, Error> WriteSourceOptimistic(
    IAdtSession& session,
    const std::string& source_uri,
    const std::string& source,
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
