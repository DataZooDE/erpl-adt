#pragma once

#include <erpl_adt/core/result.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Classic-object engine: source generation + output parsing.
//
// ADT has no REST endpoint for classic GUI objects (dynpros, GUI status,
// transactions, text pools, domains via the classic DDIC API). erpl-adt
// reaches them through SAP's own workbench function modules, called from a
// temporary IF_OO_ADT_CLASSRUN class. The class body is a fixed ABAP file
// (src/abap/zcl_erpl_classic_engine.abap, embedded in the binary); a run only
// adds the operation calls. Payloads (abapGit asXML) travel base64-encoded,
// so no user data ever has to be quoted into ABAP source.
//
// This module is pure: it builds source text and parses console output. The
// helper-class lifecycle (create in $TMP, activate, run, delete) lives in
// workflow/classrun_helper.hpp.
// ---------------------------------------------------------------------------

// One operation in an engine run.
struct EngineOp {
    std::string call;     // ABAP method call statement incl. trailing '.'
    std::string payload;  // raw bytes handed to the operation (empty: none)
};

// Builders for the operations the engine implements. Arguments are quoted as
// ABAP literals; names must already have passed the customer-object check.
EngineOp DomaGetOp(const std::string& name, const std::string& language);
EngineOp DomaPutOp(const std::string& name, const std::string& language,
                   const std::string& devclass, const std::string& trkorr,
                   const std::string& asx_xml);
EngineOp DomaDeleteOp(const std::string& name, const std::string& trkorr);
// Logon user (META changed_by field) and changeable namespaces (values["NS"]).
EngineOp SystemInfoOp();

// Escapes `value` as an ABAP character literal ('...', quotes doubled).
std::string AbapQuote(const std::string& value);

// Full class source for one run: the embedded engine with `class_name`
// substituted and `ops` inserted into RUN_OPS, numbered 1..n.
std::string BuildEngineSource(const std::string& class_name, const std::vector<EngineOp>& ops);

struct EngineMessage {
    std::string type;    // E, W, I, S, A
    std::string id;      // message class
    std::string number;  // message number
    std::string text;    // rendered in the logon language
};

struct EngineOpResult {
    int rc = -1;
    std::optional<bool> exists;
    std::string changed_by;
    std::string changed_on;
    std::string changed_at;
    std::map<std::string, std::string> data;  // tag -> decoded bytes
    std::vector<EngineMessage> messages;
    std::vector<std::string> errors;           // activation / check log lines
    std::map<std::string, std::vector<std::string>> values;  // other kinds, e.g. "NS"
};

// Parses the engine's line protocol (see the ABAP file's header). Fails if
// the run did not reach @END, raised an exception, or an operation reported
// no return code — an incomplete run is never read as success.
[[nodiscard]] Result<std::vector<EngineOpResult>, Error> ParseEngineOutput(
    const std::string& output, size_t op_count);

// Human-readable reason for a failed operation: SAP messages and log lines
// first, the bare return code only as a last resort.
std::string SummarizeFailure(const EngineOpResult& result);

std::string EncodeBase64(const std::string& bytes);
std::string DecodeBase64(const std::string& text);

}  // namespace erpl_adt
