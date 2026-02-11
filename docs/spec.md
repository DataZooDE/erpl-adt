# `erpl-deploy` — Headless abapGit Package Deployer

## Spec v0.4 — February 2026

---

## 1. Problem

Deploying abapGit repositories into an ABAP Cloud Developer Trial requires Eclipse + ADT + abapGit plugin — interactive, GUI-bound, not scriptable. No headless path exists.

`erpl-deploy` fills that gap: a single C++ binary that talks the ADT REST protocol (same HTTP endpoints Eclipse uses) to clone, pull, and activate abapGit repos. No Eclipse, no SAP NW RFC SDK, no JVM.

Part of the Datazoo ERPL family. Shares build conventions with flapi and library choices with erpl-web.

---

## 2. Design Principles

### Build & Ecosystem

- **CMake + Ninja + vcpkg submodule**, `make release` builds everything — identical to flapi
- **Single self-contained binary** per platform (Linux, macOS, Windows) — no shared libs, no runtime deps
- **GitHub Actions CI** as the sole build/release platform — matrix builds for all three OS targets
- **Library alignment with erpl-web** where possible (tinyxml2 for XML, cpp-httplib for HTTP)

### Software Design

The codebase follows modern C++17 practices throughout. These are not aspirational — they are constraints the implementation must satisfy.

**Ownership & lifetime:**
- No raw `new`/`delete`. All heap objects owned via `std::unique_ptr` or `std::shared_ptr` with clear ownership semantics.
- Value types preferred over pointer types wherever possible. Move semantics used for efficient transfer.
- No global mutable state. All state flows through explicit constructor injection.

**Error handling:**
- No exceptions for expected failure paths. Use `std::expected<T, Error>` (C++23) or a project-local `Result<T, E>` type if C++23 is not available across all target compilers.
- Exceptions reserved only for programming errors (logic bugs) and truly exceptional conditions (out of memory).
- Every error case carries structured context: what operation failed, which endpoint, HTTP status, SAP error text.

**Type safety:**
- Strong types for domain concepts — don't pass bare strings where a `PackageName`, `RepoUrl`, or `BranchRef` is meant. Thin wrapper types or `enum class` to prevent mixing up arguments.
- `std::optional` for genuinely optional values. No sentinel values (empty strings meaning "absent").
- `std::string_view` for non-owning read access to strings.

**Interface design:**
- Each component defines a pure abstract interface (abstract base class). Concrete implementations are injected via constructor.
- This enables mock-based unit testing: every component can be tested in isolation by injecting mock collaborators.
- Public interfaces are minimal. Internal helpers are private or in anonymous namespaces — never in headers.

**RAII everywhere:**
- HTTP sessions, CSRF tokens, file handles — all managed by RAII wrappers.
- The `AdtSession` class destructor handles cleanup. No manual resource management.

**Const-correctness:**
- All methods that don't mutate state are `const`. All parameters that aren't modified are `const&` or `std::string_view`.
- Data structures returned to callers are value types (returned by value, moved).

**No implicit conversions, no C-style casts, no `void*`.**

---

## 3. Dependencies

Five vcpkg ports. Same count as erpl-web's external deps.

| Port | Role | Why |
|------|------|-----|
| **cpp-httplib[openssl]** | HTTP/HTTPS client | Header-only, cookie jar, Basic Auth. Same lib used by DuckDB internals. No Boost. |
| **tinyxml2** | XML parse + build | Same XML library as erpl-web. Lightweight, no dependencies. Sufficient for ADT's XML payloads (no XPath needed — ADT responses have predictable structure). |
| **yaml-cpp** | Config file parsing | Same as flapi. |
| **argparse** | CLI argument parsing | Same as flapi. |
| **openssl** | TLS | Required by cpp-httplib for HTTPS. |

Test dependency: **Catch2**.

Not included: pugixml (erpl-web uses tinyxml2 — stay consistent), Crow, jwt-cpp, DuckDB, Boost, SAP NW RFC SDK.

---

## 4. Project Layout

```
erpl-deploy/
├── CMakeLists.txt
├── Makefile
├── vcpkg.json
├── vcpkg/                           # git submodule
├── .github/
│   └── workflows/
│       ├── build.yaml               # CI: build + unit test on all 3 OS
│       └── release.yaml             # tagged release: build + upload artifacts
├── include/erpl_deploy/
│   ├── core/
│   │   ├── types.hpp                # strong types: PackageName, RepoUrl, etc.
│   │   ├── result.hpp               # Result<T,E> error handling
│   │   └── log.hpp                  # structured logging interface
│   ├── adt/
│   │   ├── i_adt_session.hpp        # abstract: HTTP session interface
│   │   ├── adt_session.hpp          # concrete: cpp-httplib implementation
│   │   ├── i_xml_codec.hpp          # abstract: XML build/parse interface
│   │   ├── xml_codec.hpp            # concrete: tinyxml2 implementation
│   │   ├── discovery.hpp            # discovery document parser
│   │   ├── packages.hpp             # package operations
│   │   ├── abapgit.hpp              # repo clone/pull/status
│   │   └── activation.hpp           # mass activation
│   ├── config/
│   │   ├── app_config.hpp           # configuration data types
│   │   └── config_loader.hpp        # CLI + YAML loading
│   └── workflow/
│       └── deploy_workflow.hpp      # orchestration state machine
├── src/
│   ├── main.cpp
│   ├── core/
│   ├── adt/
│   ├── config/
│   └── workflow/
├── test/
│   ├── CMakeLists.txt
│   ├── core/
│   │   └── test_types.cpp
│   ├── adt/
│   │   ├── test_xml_codec.cpp
│   │   ├── test_adt_session.cpp
│   │   ├── test_packages.cpp
│   │   ├── test_abapgit.cpp
│   │   └── test_activation.cpp
│   ├── config/
│   │   └── test_config_loader.cpp
│   ├── workflow/
│   │   └── test_deploy_workflow.cpp
│   ├── mocks/
│   │   ├── mock_adt_session.hpp
│   │   └── mock_xml_codec.hpp
│   └── testdata/                    # captured ADT XML traffic
├── scripts/
│   ├── capture_adt.sh
│   └── smoke_test.sh
├── docker/
│   ├── Dockerfile
│   └── docker-compose.yaml
├── examples/
│   ├── flight.yaml
│   └── multi-repo.yaml
├── LICENSE
└── README.md
```

### Layout Rationale

The `include/` tree mirrors `src/` with a `core/adt/config/workflow` decomposition. This isn't just organizational — it reflects compilation firewall boundaries. Changes to `adt/` internals don't force recompilation of `config/`. The `i_*.hpp` abstract interfaces live alongside their concrete implementations but depend on nothing from sibling directories.

The `test/mocks/` directory contains hand-written mocks implementing the abstract interfaces. No mocking framework — the interfaces are small enough that manual mocks are clearer and have zero dependency cost.

---

## 5. Architecture

### Component Dependency Graph

```
main.cpp → config_loader → deploy_workflow
                               │
               ┌───────────────┼───────────────┐
               ▼               ▼               ▼
           packages        abapgit        activation
               │               │               │
               └───────────────┼───────────────┘
                               ▼
                          i_adt_session ◄── adt_session (cpp-httplib)
                               │
                          i_xml_codec  ◄── xml_codec   (tinyxml2)
```

All arrows point downward. No cycles. Every horizontal boundary is an abstract interface.

### Component Responsibilities

| Component | Responsibility | Interface | Injected via |
|-----------|---------------|-----------|-------------|
| **types** | Strong types (`PackageName`, `RepoUrl`, `BranchRef`, `RepoKey`, `SapClient`), `Result<T,E>`. No logic, just type definitions. | — | — |
| **log** | Structured logging with levels (error, warn, info, debug). Console output for humans, JSON for machines. Thread-safe. | Abstract sink interface | Constructor |
| **xml_codec** | Build XML request payloads, parse XML response payloads. Knows ADT namespaces and content types. **Pure functions on data — no I/O.** | `IXmlCodec` | Constructor of adt components |
| **adt_session** | HTTP session: Basic Auth, CSRF token lifecycle (fetch → attach → refresh on 403), SAP headers (`sap-client`, `Accept-Language`), cookie jar, async operation polling (202 → poll Location). | `IAdtSession` | Constructor of adt components |
| **discovery** | Parse the Atom Service Document from `/sap/bc/adt/discovery`. Detect available services, verify abapGit backend presence. | Free functions taking `IAdtSession&` | — |
| **packages** | Check existence, create. Handles `/DMO/` namespace. | Free functions taking `IAdtSession&` | — |
| **abapgit** | List linked repos, clone, trigger pull, check status. | Free functions taking `IAdtSession&` | — |
| **activation** | Enumerate inactive objects, submit mass activation, report errors. | Free functions taking `IAdtSession&` | — |
| **config_loader** | Parse CLI args (argparse) and YAML files (yaml-cpp). Merge. Resolve `password_env`. Validate. Returns `AppConfig` value type. | Free function | — |
| **deploy_workflow** | Idempotent state machine: discover → package → clone → pull → activate. Queries state before each step. Respects `depends_on` ordering. | Class taking `IAdtSession&` | Constructor |

### Design Decisions

**Free functions for ADT operations** (`packages`, `abapgit`, `activation`): These are stateless operations that take a session reference and return results. Wrapping them in classes would add ceremony without benefit — there's no per-operation state to encapsulate. The testability comes from the injected `IAdtSession` mock, not from class structure.

**`IXmlCodec` as interface** rather than free functions: XML building/parsing could accumulate configuration (default namespace prefixes, content-type version preferences). An interface allows testing `adt_session` and the operation modules independently of the XML layer.

**`deploy_workflow` as a class**: This is the one component with meaningful state — the sequence of steps, their results, progress tracking. Constructor injection of `IAdtSession` makes it fully unit-testable with mocks.

---

## 6. ADT REST Protocol

### Endpoint Map

| Operation | Method | Path |
|-----------|--------|------|
| Discovery | GET | `/sap/bc/adt/discovery` |
| Package exists | GET | `/sap/bc/adt/packages/{name}` |
| Package create | POST | `/sap/bc/adt/packages` |
| List repos | GET | `/sap/bc/adt/abapgit/repos` |
| Clone repo | POST | `/sap/bc/adt/abapgit/repos` |
| Repo status | GET | `/sap/bc/adt/abapgit/repos/{key}` |
| Pull repo | POST | `/sap/bc/adt/abapgit/repos/{key}/pull` |
| Unlink repo | DELETE | `/sap/bc/adt/abapgit/repos/{key}` |
| Inactive objects | GET | `/sap/bc/adt/activation/inactive` |
| Mass activate | POST | `/sap/bc/adt/activation` |

### Protocol Rules

- **CSRF**: Every mutating request requires a CSRF token. Fetch via `GET` with `x-csrf-token: fetch` header. On `403` → re-fetch and retry once.
- **Async ops**: Pull and activation return `202 Accepted` + `Location` header. Poll until `completed` or `failed`.
- **Content types**: ADT uses vendor MIME types. Exact types validated via traffic capture.
- **Session**: Cookie-based (`sap-contextid`, `SAP_SESSIONID_*`).

### Protocol Discovery (Dev Phase 0)

Exact XML payloads are undocumented. Capture once from Eclipse ADT traffic (mitmproxy). Captured payloads become the canonical test fixtures.

**References:** `abapGit/ADT_Backend` (GitHub), `marcellourbani/abap-adt-api` (GitHub, MIT).

---

## 7. CLI Interface

### Subcommands

| Command | Purpose |
|---------|---------|
| `deploy` | Full workflow (default) |
| `status` | Show state |
| `pull` | Pull only |
| `activate` | Activate only |
| `discover` | Probe endpoints |

### Flags

Connection: `--host`, `--port`, `--https`, `--client`, `--user`, `--password`, `--password-env`

Repository: `--repo`, `--branch`, `--package`

Options: `-c/--config`, `--no-activate`, `--timeout`, `--json`, `--log-file`, `-v`, `-q`, `--version`

### YAML Config

Multi-repo deployment with `depends_on` ordering. Connection settings, defaults, repo list.

### Exit Codes

0=success, 1=connection, 2=package, 3=clone, 4=pull, 5=activation, 10=timeout, 99=internal.

---

## 8. Workflow Idempotency

| Step | Pre-check | Skip condition |
|------|-----------|----------------|
| Create package | `GET /packages/{name}` | 200 → exists |
| Clone repo | `GET /abapgit/repos` + match URL | Already linked |
| Pull | Check repo status | ACTIVE, no changes |
| Activate | Count inactive objects | Zero inactive |

Multi-repo: topological sort on `depends_on`.

---

## 9. Cross-Platform Build & GitHub Actions

### Build Matrix

| Target | Runner | Toolchain | Static linking | Notes |
|--------|--------|-----------|---------------|-------|
| Linux x86_64 | `ubuntu-24.04` | GCC 13+ | `-static-libgcc -static-libstdc++`, OpenSSL static | Primary target. Docker trial runs here. |
| macOS arm64 | `macos-14` | Apple Clang 15+ | Default static for vcpkg libs, system OpenSSL/LibreSSL | Universal binary not required — arm64 only (all modern Macs). |
| macOS x86_64 | `macos-13` | Apple Clang 15+ | Same as arm64 | For Intel Macs still in use. |
| Windows x64 | `windows-2022` | MSVC 17+ | `/MT` (static CRT) | vcpkg triplet `x64-windows-static`. |

### GitHub Actions — Build Workflow

**Trigger:** Every push to `main` and every PR.

**Steps per matrix entry:**
1. Checkout with submodules (`submodules: recursive`)
2. Cache vcpkg binary cache (`actions/cache` on `~/.cache/vcpkg` / platform equivalent) — critical for build time, vcpkg from-source builds are slow
3. Bootstrap vcpkg
4. `make release`
5. `make test` (unit tests only — no SAP system in CI)
6. Upload binary as artifact

**Robustness measures:**
- Pin vcpkg baseline in `vcpkg.json` to a specific commit — prevents upstream breakage
- Pin runner images to specific versions in the matrix
- vcpkg binary cache keyed on `vcpkg.json` hash + OS + compiler — cache invalidates only when deps change
- Separate `build.yaml` (CI) and `release.yaml` (tagged release) workflows
- Release workflow builds all platforms, then creates GitHub Release with all binaries attached

### GitHub Actions — Release Workflow

**Trigger:** Push of tag `v*`.

**Steps:**
1. Build matrix (same as CI)
2. Name binaries: `erpl-deploy-{os}-{arch}` (`.exe` on Windows)
3. Create GitHub Release
4. Attach all binaries + SHA256 checksums

### Static Linking Considerations

**Linux:** The primary concern is glibc. Fully static linking with musl (Alpine-based build) is the cleanest path but may conflict with vcpkg's default triplet. Alternative: build on `ubuntu-24.04` with static libstdc++ and accept a glibc 2.39 floor — acceptable since the Docker trial itself runs on a recent distro. Decision: **start with default `x64-linux` triplet + static libstdc++**; switch to musl only if portability issues arise.

**macOS:** vcpkg builds static libs by default on macOS. OpenSSL links statically. System frameworks (Security, CoreFoundation) link dynamically but are always present. No portability concern.

**Windows:** vcpkg triplet `x64-windows-static` builds everything statically including the CRT (`/MT`). Produces a single `.exe` with no DLL dependencies. Well-tested path.

### Build Time Budget

Target: CI build completes in under 10 minutes per platform (with warm vcpkg cache). First build (cold cache) may take 15-20 minutes due to OpenSSL compilation.

---

## 10. Incremental Development & Test Plan

The critical risk is the undocumented ADT protocol. The plan validates each layer independently, bottom-up, so nothing builds on unverified assumptions.

### Phase 0 — Capture Ground Truth

**Goal:** Obtain real ADT XML payloads from Eclipse.

**Work:**
1. mitmproxy as reverse proxy in front of Docker trial
2. In Eclipse ADT: discover → create package → clone repo → pull → activate
3. Save each request/response pair to `test/testdata/`

**Acceptance:**
- `test/testdata/` contains: discovery response, package GET 200/404, package POST request/response, repo clone request/response, pull 202, poll running/completed, activation request/response
- Files are real unmodified Eclipse traffic captures

No code written. Phase produces only test fixtures.

---

### Phase 1 — `core/types` + `core/result`

**Goal:** Establish the type foundation.

**Test-first:**
- Strong types construct from valid inputs, reject invalid (e.g. `PackageName` rejects empty string, enforces uppercase, validates `/` namespace prefix rules)
- `Result<T, E>` behaves correctly: value access on success, error access on failure, monadic chaining (`and_then`, `map`)
- Types are value types: copyable, movable, equality-comparable

**Acceptance:** All offline. Pure unit tests. Validates the vocabulary before anything else exists.

---

### Phase 2 — `adt/xml_codec` (Pure, No I/O)

**Goal:** Prove we can build and parse every ADT XML payload.

**Test-first:**
- For each captured request XML: test that the corresponding build function produces structurally equivalent output (correct root element, namespace, required attributes/elements). **Not whitespace-identical** — structural equivalence via tinyxml2 DOM comparison.
- For each captured response XML: test that the parse function extracts all fields correctly into the typed result structs.
- Round-trip: build → parse → assert fields match input.

**Acceptance:**
- Package XML: namespace, name, software component present
- Clone XML: URL, branch ref, package present
- Discovery parse: extracts service list, `HasAbapGitSupport()` returns true
- Repo response parse: extracts key, status, URL, package, branch
- Activation response parse: extracts totals and error messages

All tests offline against `test/testdata/`.

---

### Phase 3 — `adt/adt_session` (HTTP + CSRF)

**Goal:** Prove the HTTP session layer works.

**3a. Unit tests (mock, offline):**
- CSRF token extraction from response headers
- Cookie forwarding on subsequent requests
- 403 retry: mock 403 → verify token re-fetch → verify retry with new token → verify gives up after one retry
- `PollUntilComplete`: feed mock responses (running, running, completed) → returns success; (running, failed) → returns failure; timeout exceeded → returns timeout error
- SAP-specific headers (`sap-client`, `Accept-Language`) always present
- TLS verification disabled when configured (self-signed trial certs)

**3b. Smoke tests (live, Docker trial):**
- Connect to `localhost:50000`, fetch CSRF token, verify non-empty
- `GET /sap/bc/adt/discovery` returns 200 with XML body
- Bad credentials return 401
- Verify `sap-client: 001` sent

**Acceptance:** Unit tests pass offline. Smoke script passes against running trial.

---

### Phase 4 — `adt/discovery`

**Goal:** Parse live discovery document, detect capabilities.

**Test-first:**
- Parse live discovery response, assert abapGit endpoints present
- Compare against Phase 0 captured data — flag structural differences
- Missing abapGit endpoints → clear error with remediation hint

**Acceptance:** `discover` subcommand prints endpoint list. Exits cleanly on missing capabilities.

---

### Phase 5 — `config`

**Goal:** CLI and YAML parsing works.

**Test-first (all offline):**
- Minimal CLI args → `AppConfig` with correct strong-typed fields
- YAML file → `AppConfig` with repo list
- CLI overrides YAML (merge semantics)
- `password_env` resolves from environment
- Missing required fields → `Result` with descriptive error
- Multi-repo `depends_on` → correct topological order
- Cycle in `depends_on` → error

**Acceptance:** All offline unit tests.

---

### Phase 6 — `adt/packages` (Live)

**Goal:** Package check and creation works.

**Test-first:**
- `PackageExists` on `$TMP` → true (always exists)
- `PackageExists` on non-existent package → false
- `CreatePackage` for `ZTEST_*` → success, then `PackageExists` → true
- Create already-existing package → idempotent (no error)

**Acceptance:** Requires running trial + Phases 1-4 passing.

---

### Phase 7 — `adt/abapgit` (Live)

**Goal:** Repo clone and pull works.

**7a. Small test repo** (fast iteration, < 60 seconds):
- `FindRepo` on fresh system → empty
- `CloneRepo` → returns repo key
- `FindRepo` → returns info
- `PullRepo` → completes, returns object count
- Re-clone same URL → idempotent

**7b. Flight Reference Scenario** (full integration):
- Clone + pull into `/DMO/FLIGHT`
- Verify 300+ objects
- Timeout and progress polling work

**Acceptance:** Small repo < 60s. Flight repo within timeout.

---

### Phase 8 — `adt/activation` (Live)

**Goal:** Mass activation works.

**Test-first:**
- After pull: inactive objects > 0
- `ActivateAll` → returns count
- Re-activate → 0 activated (idempotent)
- Activation errors → captured with object name + text

**Acceptance:** Flight scenario: ~340 objects activate. Second run skips.

---

### Phase 9 — `workflow/deploy_workflow` (Orchestration)

**9a. Unit tests (mock, offline):**
- Step ordering verified: discover → package → clone → pull → activate
- Mock "package exists" → Skipped
- Mock "pull failed" → subsequent steps don't run
- Multi-repo `depends_on` → correct order
- All results carry timing, counts, error details

**9b. Integration (live):**
- `erpl-deploy deploy -c examples/flight.yaml` on fresh trial → success
- Same command again → all Skipped
- `--json` → parseable output
- `--no-activate` → activation skipped

**Acceptance:** Fresh deploy → exit 0. Repeat → exit 0, all Skipped.

---

### Phase 10 — Docker & CI

- `docker build` → working image < 15 MB
- `docker-compose.yaml` → provisions Flight scenario end-to-end
- GitHub Actions build matrix → compiles + unit tests pass on all 3 OS
- Release workflow → binaries attached to GitHub Release

---

### Test Summary

| Phase | Component | Type | SAP needed? | Runs in CI? |
|-------|-----------|------|-------------|-------------|
| 0 | — | Manual capture | Yes | No |
| 1 | core/types, result | Unit | No | **Yes** |
| 2 | adt/xml_codec | Unit | No | **Yes** |
| 3a | adt/adt_session | Unit (mock) | No | **Yes** |
| 3b | adt/adt_session | Smoke | Yes | No |
| 4 | adt/discovery | Integration | Yes | No |
| 5 | config | Unit | No | **Yes** |
| 6 | adt/packages | Integration | Yes | No |
| 7 | adt/abapgit | Integration | Yes | No |
| 8 | adt/activation | Integration | Yes | No |
| 9a | workflow | Unit (mock) | No | **Yes** |
| 9b | workflow | E2E | Yes | No |
| 10 | Docker/CI | E2E + build | Yes (E2E) | **Build only** |

**CI runs Phases 1, 2, 3a, 5, 9a on every commit.** No SAP infrastructure needed.

Live phases run on-demand against Docker trial.

---

## 11. Docker

Multi-stage build: Ubuntu build stage, distroless runtime. Binary target ~5-8 MB.

Docker Compose: SAP trial with healthcheck on `/sap/bc/adt/discovery`, provisioner depends on `service_healthy`, mounts YAML, reads password from env.

---

## 12. Scope

### In scope
- Deploy public GitHub repos via abapGit ADT REST
- Packages: `/DMO/`, `Z*`, `Y*`, `$TMP`
- Clone, pull, activate
- YAML multi-repo with `depends_on`
- Idempotent re-runs
- JSON + human output
- Cross-platform binaries (Linux x86_64, macOS arm64/x86_64, Windows x64)
- GitHub Actions CI/CD

### Out of scope
- General ADT client
- Replacing abapGit
- Non-abapGit formats (transports, gCTS)
- GUI
- Private repo auth (v1.1)
- Transport request management (v1.1)
- DuckDB extension variant (future — `adt_session`/`xml_codec` are cleanly separable)

---

## 13. License

Apache 2.0.
