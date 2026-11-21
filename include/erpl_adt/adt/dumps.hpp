#pragma once

#include <erpl_adt/adt/i_adt_session.hpp>
#include <erpl_adt/core/result.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace erpl_adt {

// ---------------------------------------------------------------------------
// Runtime dumps (ST22) over ADT (#89).
//
//   GET /sap/bc/adt/runtime/dumps            Atom feed, newest first
//   GET /sap/bc/adt/runtime/dump/<id>        header (error, exception, ...)
//   GET .../<id>/unformatted                 keyed rows: AP program, AI include,
//                                            AL line, EV event (language-free)
//   GET .../<id>/formatted                   the dump as ST22 shows it
//   GET .../<id>/summary                     HTML; its source extract links the
//                                            termination point as ADT source
// ---------------------------------------------------------------------------

// One dump in the list. id is the URL-encoded id from the ADT link (the raw
// one has spaces); time is the UTC time it was written, ISO 8601.
struct DumpEntry {
    std::string id;
    std::string error;       // runtime error, e.g. COMPUTE_INT_ZERODIVIDE
    std::string short_text;  // e.g. "Division by 0 (type I or INT8)"
    std::string program;     // terminated program, e.g. ZCL_X=====CP
    std::string user;
    std::string time;
};

struct DumpListOptions {
    std::optional<std::string> owner;  // user name; any user when empty
    std::optional<std::string> since;  // ParseDumpSince form, UTC
    int max = 50;
};

// A dump with where it terminated: include/include_line as the runtime saw
// them (e.g. ZCL_X=====CM001 line 3), uri/line as ADT source an agent can
// open (e.g. .../classes/zcl_x/source/main line 9) - absent when the program
// no longer exists. text is the formatted dump, only when asked for.
struct DumpDetail {
    std::string id;
    std::string error;
    std::string exception;
    std::string short_text;
    std::string program;
    std::string include;
    std::optional<int> include_line;
    std::string event;
    std::string main_program;
    std::string user;
    std::string time;
    std::optional<std::string> uri;
    std::optional<int> line;
    std::string text;
};

// "2026-11-21T16:12:00Z", "2026-11-21T16:12", "2026-11-21" or
// "20261121161200" (all UTC) -> "2026-11-21T16:12:00Z"; anything else is a
// validation error.
[[nodiscard]] Result<std::string, Error> ParseDumpSince(std::string_view since);

// The dump id from an id, "/sap/bc/adt/runtime/dump/<id>" or
// "adt://<SID>/sap/bc/adt/runtime/dump/<id>"; a validation error for
// anything that is not an id (slashes, spaces, empty).
[[nodiscard]] Result<std::string, Error> NormalizeDumpId(std::string_view id);

[[nodiscard]] Result<std::vector<DumpEntry>, Error> ListDumps(IAdtSession& session,
                                                              const DumpListOptions& options);

[[nodiscard]] Result<DumpDetail, Error> GetDump(IAdtSession& session, std::string_view id,
                                                bool full_text);

// The newest dump of `user` written at or after `since` (ParseDumpSince
// form) whose terminated program is the class pool of `class_name` (a name
// or an ADT class URI): "ZCL_X" matches "ZCL_X=========...CP". A dump of
// any other program - another session, a GUI step - is not this run's.
// Best effort: nullopt on any failure.
[[nodiscard]] std::optional<DumpEntry> FindDumpSince(IAdtSession& session, const std::string& user,
                                                     const std::string& since,
                                                     const std::string& class_name);

// A time as ParseDumpSince's canonical form, e.g. "2026-11-21T16:12:00Z".
[[nodiscard]] std::string DumpTime(std::chrono::system_clock::time_point when);

// When a run of `class_name` (object run, adt_run_class) failed: if the
// logon user has a dump of that class written since `since`, turn the error
// into a runtime_dump error whose hint names it ("COMPUTE_INT_ZERODIVIDE
// (Division by 0 ...) in ZCL_X=====CP - see: erpl-adt dump show <id>").
// Otherwise the error is left as it was.
void AttachRunDump(IAdtSession& session, const std::string& since, const std::string& class_name,
                   Error& error);

// The JSON of `dump list` / `dump show` and of adt_list_dumps /
// adt_show_dump.
[[nodiscard]] nlohmann::json DumpEntryToJson(const DumpEntry& entry);
[[nodiscard]] nlohmann::json DumpDetailToJson(const DumpDetail& detail);

} // namespace erpl_adt
