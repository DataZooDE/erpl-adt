#pragma once
//
// Compatibility shim for the shared DataZooDE/posthog-telemetry library.
//
// The library was extracted from the DuckDB extension world, where cpp-httplib
// is vendored inside a `duckdb_httplib_openssl` namespace. Its telemetry.cpp
// does `#include "httplib.hpp"` and calls `duckdb_httplib_openssl::Client`.
//
// erpl-adt is not a DuckDB product; it consumes the *upstream* cpp-httplib from
// vcpkg (`httplib::httplib`, header <httplib.h>, namespace `httplib`). Upstream
// and the DuckDB fork share an identical public API, so a namespace alias is all
// that's needed — no source vendoring, no DuckDB header fetch.
//
// This file is placed on the posthog_telemetry target's include path so its
// quoted `#include "httplib.hpp"` resolves here.
//
#include <httplib.h>

namespace duckdb_httplib_openssl = httplib;
