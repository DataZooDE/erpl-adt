# Passwordless SSO for erpl-adt — Research & Implementation Plan

Status: **proposal** (no code written yet)
Scope: extend `erpl-adt` beyond HTTP Basic auth so it can log on to AS ABAP
without a password in the config/CLI.

---

## 1. Research: what "SNC SSO" actually means for an ADT client

### 1.1 The core correction

The request that triggered this ("implement an SNC SSO — passwordless login to
SAP GUI and Eclipse") conflates two different transport stacks:

| | SAP GUI / RFC | Eclipse ADT / erpl-adt |
|---|---|---|
| Wire protocol | DIAG / RFC (proprietary TCP) | HTTP(S) via ICM |
| Security layer | **SNC** (GSS-API v2 over DIAG/RFC) | **TLS** + an HTTP auth scheme |
| Library needed | CommonCryptoLib / `sapcrypto`, NW RFC SDK | none beyond OpenSSL |

**SNC is not reachable from an ADT client.** SNC is a GSS-API binding at the
DIAG/RFC transport layer. ADT is a plain REST API served by the ICM under
`/sap/bc/adt/*`; there is no SNC on that path. Implementing literal SNC would
require the SAP NW RFC SDK plus CommonCryptoLib — both explicitly out of scope
for this project (see README: "No Eclipse, no SAP NW RFC SDK, no JVM").

What Eclipse ADT *actually* does for passwordless logon, and what we can do
identically, is one of the HTTP-level SSO schemes below. The user-visible
outcome is the same — no password typed, the corporate identity is reused —
which is what the request is really after.

### 1.2 The four viable HTTP-level mechanisms

#### (a) X.509 client certificate (mutual TLS)

The closest functional equivalent to "SNC SSO", and what SAP Secure Login
Client actually hands to Eclipse in most enterprise landscapes: the SLC
converts the user's Kerberos TGT into a short-lived X.509 certificate, and the
client authenticates with mutual TLS.

Server side (AS ABAP):
- profile parameter `icm/HTTPS/verify_client = 1` (accept) or `2` (require);
  `1` falls back to other logon procedures, `2` does not
  ([SAP Help](https://help.sap.com/doc/saphelp_nw74/7.4.16/en-US/4e/1260981e3d2287e10000000a15822b/content.htm?no_cache=true))
- issuing CA root imported into the **SSL Server Standard PSE** certificate
  list via STRUST, or from the OS with
  `sapgenpse maintain_pk -a ca.crt -p SAPSSLS.pse`
  ([SAP Help](https://help.sap.com/docs/SAP_NETWEAVER_750/e73bba71770e4c0ca5fb2a3c17e8e229/0a2aa63a2719f539e10000000a11402f.html))
- certificate subject DN mapped to an ABAP user in table `USREXTID`
  (`MANDT`, `EXTID`, `IDTYPE='DN'`, `BNAME`), maintained via SM30 view
  `VUSREXTID`
  ([SAP Help](https://help.sap.com/doc/saphelp_nw73ehp1/7.31.19/en-US/a8/f11960daa149958bd73c9b1b20095a/content.htm?no_cache=true))
- the ICF node's logon procedure list must include the certificate procedure
  ([SAP KBA 2573379](https://userapps.support.sap.com/sap/support/knowledge/en/2573379))

Client side: this is *purely* a TLS handshake concern. No new HTTP logic at
all. `cpp-httplib` already supports it — `httplib::Client` has a constructor
taking `client_cert_path` / `client_key_path` and routes to `SSLClient` when
the scheme is `https` (verified in `httplib.h`, `Client::Client` at the
universal-client constructor). **This is the cheapest mechanism to implement
and the only one fully verifiable end-to-end against the a4h container.**

#### (b) SPNEGO / Kerberos (`Authorization: Negotiate`)

The literal "Kerberos SSO" ask. AS ABAP supports it via transaction `SPNEGO`
(NW 7.40+), requires the Secure Login Library / CommonCryptoLib on the server
and a licensed SAP SSO product
([SAP Community](https://community.sap.com/t5/technology-blog-posts-by-sap/sap-single-sign-on-authenticate-with-kerberos-spnego/ba-p/13321445),
[itsiti](https://itsiti.com/spnego-configuring-kerberos-services-with-spnego-in-as-abap/)).

Protocol (RFC 4559): server replies `401` with `WWW-Authenticate: Negotiate`;
client builds a SPNEGO `NegTokenInit` (mech OID `1.3.6.1.5.5.2`, wrapping
Kerberos 5 `1.2.840.113554.1.2.2`) via `gss_init_sec_context`, base64-encodes
it, retries with `Authorization: Negotiate <b64>`; server may reply with a
`WWW-Authenticate: Negotiate <b64>` continuation until `GSS_S_COMPLETE`
([MS docs](https://msdn.microsoft.com/en-us/library/ms995330.aspx),
[curl](https://curl.se/video/curlup-2017/2017-03-19_09_Isaac_Boukris_SPNEGO_KERBEROS_GSS-API_and_Negotiate_support.pdf)).

Client side needs a GSS provider:
- Linux: `libgssapi_krb5.so.2` (MIT krb5)
- macOS: `GSS.framework`
- Windows: SSPI (`secur32.dll`, ships with the OS — no dependency)

To keep the "single static binary, no heavy deps" property, bind these at
**runtime via `dlopen`/`LoadLibrary`**, not at build time. If the library is
absent, `--auth negotiate` fails with a clear diagnostic; every other auth mode
still works. This keeps `vcpkg.json` untouched.

#### (c) Bearer token / OAuth 2.0

Required for SAP BTP ABAP Environment and S/4HANA Cloud, where ADT logon is an
OAuth/SAML-bearer flow, not Basic. Trivial on the client: `Authorization:
Bearer <token>`; `cpp-httplib` has `set_bearer_token_auth`. The interesting
part is *sourcing* the token, which is landscape-specific — best solved with an
external credential-helper hook (`--auth-command`) rather than baking in a
BTP-specific OAuth client.

#### (d) SAP Logon Ticket / session cookie reuse (`MYSAPSSO2`)

After any SSO logon (SAML, SPNEGO, cert), AS ABAP issues an `MYSAPSSO2` cookie
valid ~8h by default. Accepting an externally-obtained cookie makes erpl-adt
work behind SAML/IdP-fronted landscapes and in "browser logs in once, CLI
reuses it" workflows. Cheapest possible implementation (one header), and the
one path that is **verifiable on a stock a4h container with zero server-side
configuration** — obtain the ticket once with a password, then prove that
subsequent calls succeed with the cookie alone and no `Authorization` header.

### 1.3 Recommendation

Implement (a), (c), (d) and the credential-helper hook first — they are small,
orthogonal, and cover BTP + SAML + Secure-Login-Client landscapes. Implement
(b) SPNEGO after, behind a runtime-loaded GSS shim, because it is the largest
piece and the hardest to test.

---

## 2. Current state of the code

| Concern | Where | Note |
|---|---|---|
| Session construction | `src/adt/adt_session.cpp:174` | `client->set_basic_auth(user, password)` — the only auth |
| Options struct | `include/erpl_adt/adt/adt_session.hpp:16` | `AdtSessionOptions` |
| Config | `include/erpl_adt/config/app_config.hpp:12` | `ConnectionConfig{user, password, password_env}` |
| Construction sites | `src/main.cpp:495` (MCP), `src/main.cpp:796` (deploy), `src/cli/command_executor.cpp:414` (CLI) | three |
| Credential file | `src/cli/command_executor.cpp:201` `.adt.creds` | + `src/main.cpp:409` |
| Login wizard | `src/cli/login_wizard.cpp` | ftxui form, password field |
| 403/CSRF retry loop | `src/adt/adt_session.cpp:475` etc. | the natural place to add a symmetric 401 retry |

The password is threaded as a bare `std::string` through the constructor. It
must become a value type so new methods can be added without touching every
call site again.

---

## 3. Target design

### 3.1 New types

`include/erpl_adt/core/auth.hpp`:

```cpp
enum class AuthMethod { Basic, Bearer, Certificate, Negotiate, Cookie };

// Strong type, private ctor + Create() returning Result<AuthConfig, string>,
// per the project's strong-type convention. Validates that the fields
// required by `method` are present and that mutually exclusive fields are not
// both set.
class AuthConfig {
 public:
  static Result<AuthConfig, std::string> Basic(std::string user, std::string password);
  static Result<AuthConfig, std::string> Bearer(std::string token);
  static Result<AuthConfig, std::string> Certificate(std::string cert_pem_path,
                                                     std::string key_pem_path,
                                                     std::optional<std::string> key_password);
  static Result<AuthConfig, std::string> Negotiate(std::optional<std::string> spn);
  static Result<AuthConfig, std::string> Cookie(std::string mysapsso2);
  AuthMethod Method() const;
  // ... accessors
};
```

### 3.2 Two extension points on the session

Auth splits cleanly into a transport concern and a header concern:

```cpp
// include/erpl_adt/adt/i_auth_provider.hpp
class IAuthProvider {
 public:
  virtual ~IAuthProvider() = default;
  // Headers applied to every outbound request (Bearer, Cookie).
  virtual HttpHeaders InitialHeaders() const = 0;
  // Handle a 401. Returns the headers to retry with, or nullopt if this
  // provider cannot answer the challenge (-> propagate the 401).
  virtual Result<std::optional<HttpHeaders>, Error> OnUnauthorized(
      const HttpHeaders& www_authenticate) = 0;
  virtual std::string Name() const = 0;
};
```

- `BasicAuthProvider`, `BearerAuthProvider`, `CookieAuthProvider`,
  `CertificateAuthProvider` (no-op provider; work happens in the transport),
  `NegotiateAuthProvider` (drives the GSS loop).
- TLS client-cert material is passed into the `httplib::Client` constructor —
  a `TransportConfig` field on `AdtSessionOptions`.
- A 401 retry loop sits in `AdtSession::Impl::Do{Get,Post,Put,Delete}`,
  mirroring the existing 403/CSRF retry. Bounded to a small number of
  round-trips (SPNEGO can need >1 leg).

### 3.3 GSS shim (isolates the untestable part)

```cpp
// include/erpl_adt/adt/i_gss_context.hpp
class IGssContext {
 public:
  virtual ~IGssContext() = default;
  // Feed the server's token (empty on the first leg), get ours back.
  virtual Result<GssStep, Error> Step(const std::string& input_token) = 0;
};
struct GssStep { std::string output_token; bool complete; };
```

`GssApiContext` (`dlopen` MIT krb5 / GSS.framework), `SspiContext` (Windows),
and `MockGssContext` in `test/mocks/`. **The entire Negotiate state machine
becomes unit-testable offline** — this is what makes SPNEGO tractable under
TDD.

### 3.4 Surface

```
--auth basic|bearer|cert|negotiate|cookie   (default: basic; inferred if omitted)
--client-cert <pem>  --client-key <pem>  --client-key-password-env <VAR>
--bearer-token <t>   --bearer-token-env <VAR>
--sso-cookie <v>     --sso-cookie-env <VAR>
--spn <HTTP@host>                        (default HTTP@<host>)
--auth-command <cmd>                     external helper; stdout = token
```

Mirrored in YAML under `connection.auth.*` and in `.adt.creds`. **No secret ever
becomes a CLI default or is logged** — `Authorization` is already redacted in
`AdtSession::Impl::IsSensitiveHeader`; extend the redaction list to cover the
new fields, and never persist private keys (store paths only).

Backward compatibility: absent `--auth`, behaviour is exactly today's Basic.

---

## 4. Spikes (each independently verifiable before committing to the design)

Each spike is a throwaway under `scripts/spikes/` with a single pass/fail
assertion. Spikes are run in order; a failure re-opens the design.

Status of each spike is recorded below. Runner:
`./scripts/spikes/run_offline_spikes.sh` (spikes 1 and 4).

### Spike 1 — cpp-httplib does mutual TLS — **✅ VERIFIED**
`scripts/spikes/spike1_mtls.cpp`. Generates a CA + server cert + client cert,
stands up an `httplib::SSLServer` requiring a client cert, and asserts:

```
  [PASS] mTLS request completed
  [PASS] server returned 200
  [PASS] server sees client subject CN = DEVELOPER
  [PASS] no Authorization header sent
  [PASS] client without a certificate is rejected
```

**Verified:** `httplib::Client(url, cert_path, key_path)` completes mutual TLS;
the server can read the client's subject CN (the input SAP's ICM feeds into its
`USREXTID` lookup); the logon carries no `Authorization` header at all; and a
certificate-less client cannot silently degrade to anonymous. The X.509 track
has no transport-layer blocker.

### Spike 2 — a4h accepts an X.509 client certificate — **⏳ NOT RUN** (needs the container)
Script `scripts/spikes/a4h_x509_setup.sh` (written, unverified — each step
self-checks and stops on the first broken assumption):
1. `openssl` — CA + client cert `CN=DEVELOPER, O=ERPL, C=DE`
2. `docker exec` → `sapgenpse maintain_pk -a ca.crt -p SAPSSLS.pse` (add CA)
3. set `icm/HTTPS/verify_client = 1` in the instance profile
4. insert the DN→`DEVELOPER` row into `USREXTID` (same `hdbsql` technique
   already documented in CLAUDE.md for BW activation), or SM30 `VUSREXTID`
5. `sapcontrol RestartInstance`
6. **Assert:** `curl --cert dev.crt --key dev.key -k
   https://localhost:44300/sap/bc/adt/discovery` returns `200` with **no**
   `-u user:pass`.
**Verifies:** the server side is achievable at all — *before* any C++ is
written. This is the highest-risk unknown in the plan.

### Spike 3 — MYSAPSSO2 replay on stock a4h — **⏳ NOT RUN** (needs the container, no server config)
Script `scripts/spikes/a4h_sso_cookie_probe.sh`. Logs on once with a password
and keeps the cookie jar, then repeats the call with the cookie only and **no**
`Authorization` header, plus a control leg asserting that a bare anonymous
request is rejected (otherwise the ICF node is unprotected and the spike proves
nothing).
**Verifies:** the cookie track end-to-end against a real system with zero
server-side setup — the fastest possible "passwordless works" proof point.

### Spike 4 — GSS-API is loadable and produces a NegTokenInit — **✅ VERIFIED (binding half)**, ⏳ KDC half not run
`scripts/spikes/spike4_gss.c`. Compiles with **no krb5 headers and no
`-lgssapi` at link time**:

```
  [PASS] dlopen(libgssapi_krb5.so.2)
  [PASS] resolve gss_import_name / gss_init_sec_context / gss_release_{buffer,name}
  [PASS] resolve GSS_C_NT_HOSTBASED_SERVICE
  [PASS] gss_import_name("HTTP@sap-trial.example.com")
      gss_init_sec_context -> major=0x70000 minor=100001 token_len=0
  [PASS] reached the Kerberos layer and failed for lack of a TGT
```

**Verified:** the runtime-binding strategy works — the exported *variable*
`GSS_C_NT_HOSTBASED_SERVICE` resolves via `dlsym`, name import succeeds, and
`gss_init_sec_context` returns `GSS_S_NO_CRED` (`0x70000`), i.e. it reached the
Kerberos layer and failed only because there is no TGT. `vcpkg.json` needs no
new port.
**Still to prove:** on a Kerberized host with a TGT, the emitted token carries
the SPNEGO `NegTokenInit` DER prefix (`60 … 06 06 2b 06 01 05 05 02`). The
assertion is already in the spike; it just needs a KDC to reach.

### Spike 5 — 401 Negotiate challenge loop — **⏳ NOT RUN** *(offline, cheap)*
Local `httplib::Server` that answers `401 WWW-Authenticate: Negotiate`, then
`200` once a well-formed `Authorization: Negotiate <b64>` arrives. Drive it
with `MockGssContext`. **Assert:** the client completes in ≤3 legs and does not
loop.
**Verifies:** the state machine, mechanically, with no Kerberos at all.

Spikes 1, 4 (binding half) and 5 need nothing but this repo. Spike 2 is the
gate for the X.509 track; Spike 4's KDC half is the gate for the SPNEGO track.

**Net result so far:** both offline gates pass. The X.509 track has no
client-side blocker, and SPNEGO can be added without touching the dependency
set or the static-linking story. The one genuinely open question is Spike 2 —
whether a4h can be configured to accept client certificates.

---

## 5. Implementation phases (red → green → refactor)

Each bullet is one red/green cycle: write the failing Catch2 test first, then
the minimum code to pass, then tidy. Integration tests run per the cadence in
CLAUDE.md (smoke after each task, full suite at task DoD).

### Phase 0 — auth abstraction, no behaviour change
- RED: `test/core/test_auth.hpp` — `AuthConfig::Basic("", "")` must be an error;
  `Create` validates mutually exclusive fields.
- RED: `test/adt/test_adt_session.cpp` — session built from `AuthConfig::Basic`
  still sends `Authorization: Basic ...`.
- GREEN: introduce `AuthConfig`, `IAuthProvider`, `BasicAuthProvider`; add an
  `AdtSession` constructor taking `AuthConfig`; keep the old one delegating.
- Refactor the three construction sites. **Full existing suite must stay green
  — this phase ships zero new user-visible behaviour.**

### Phase 1 — cookie + bearer (unblocks SAML & BTP)
- RED: `CookieAuthProvider` emits `Cookie: MYSAPSSO2=...` and **no**
  `Authorization`.
- RED: `BearerAuthProvider` emits `Authorization: Bearer <t>`; the token is
  redacted in debug logs.
- RED: `--auth cookie` without `--sso-cookie` exits with `ErrorCategory::Auth`.
- GREEN + wire CLI/YAML/`.adt.creds`.
- **a4h test:** new `test/integration_py/test_22_auth_cookie.py` — obtain
  `MYSAPSSO2` with a password, then run `erpl-adt search ... --auth cookie
  --sso-cookie-env ADT_SSO_COOKIE` and assert results with no password in the
  environment.

### Phase 2 — X.509 client certificate (the "SNC-equivalent")
- RED: `AuthConfig::Certificate` rejects a missing/unreadable cert or key file.
- RED: `AdtSessionOptions` carries cert paths into the transport; a fake
  `SSLServer` requiring client certs accepts the session (Spike 1, promoted to
  a real test).
- RED: `--auth cert` over plain `http` is an error (mTLS needs TLS).
- GREEN + CLI/YAML/`.adt.creds` (paths only, never key material).
- **a4h test:** `test_23_auth_x509.py` — provision via the Spike-2 script, then
  `erpl-adt object read ... --auth cert --client-cert ... --client-key ...`
  against `https://localhost:44300` with **no** `--password` and no
  `SAP_PASSWORD` in the environment. Assert exit 0 and that the object is
  returned. This is the acceptance criterion for "passwordless SSO works".

### Phase 3 — SPNEGO / Kerberos
- RED: `NegotiateAuthProvider` with `MockGssContext` — first request has no
  `Authorization`; on `401 WWW-Authenticate: Negotiate` it retries with
  `Negotiate <b64>`; on a continuation token it does one more leg; it gives up
  after N legs with `ErrorCategory::Auth`.
- RED: base64 round-trip of a binary GSS token (no padding/newline bugs).
- RED: a `WWW-Authenticate` header advertising only `Basic` returns "cannot
  answer challenge" rather than looping.
- GREEN: state machine + `GssApiContext` behind `dlopen`; clear error when the
  GSS library is absent.
- **Test:** Spike 5 promoted to a unit test; a Kerberized integration test
  against an MIT KDC container. a4h SPNEGO configuration is *not* assumed —
  it needs a licensed SAP SSO library, so this phase's real-system proof is the
  KDC, with a4h SPNEGO as best-effort.

### Phase 4 — credential helper
- RED: `--auth-command` runs a command, trims stdout, uses it as a bearer
  token; non-zero exit → `ErrorCategory::Auth`; stdout never logged.
- GREEN. Covers `cf oauth-token`, vault lookups, Secure Login Client wrappers.

### Phase 5 — docs, wizard, MCP
- `login_wizard` gains an auth-method selector.
- `docs/cli-usage.md` + README auth section.
- MCP server honours the same config (all three construction sites already
  share `AuthConfig` after Phase 0).

---

## 6. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| a4h cannot be configured for client certs (Spike 2 fails) | Phase 2 loses its real-system test | Fall back to Phase 1's cookie test as the passwordless acceptance proof; keep an `httplib::SSLServer` mTLS test for the transport |
| `sapgenpse`/PSE layout differs in the trial image | Spike 2 blocked | Alternative: STRUST via SAP GUI, or the `hdbsql` route already used for BW |
| SPNEGO needs a licensed SAP SSO library on the server | Phase 3 untestable on a4h | Test against MIT KDC; treat a4h SPNEGO as optional |
| Static-binary/portability regression from linking krb5 | breaks release matrix | `dlopen` at runtime — no build-time dependency, `vcpkg.json` unchanged |
| Secrets leaking into logs or `.adt.creds` | security | Extend `IsSensitiveHeader`; store cert *paths* only; tests assert redaction |

---

## 7. Environment note

The a4h container is **not reachable from the current session** (no SAP system
on `localhost:50000`, no `SAP_PASSWORD`, and the
`sapse/abap-cloud-developer-trial` image is far larger than this container's
disk allowance). Spikes 1, 4 and 5 and all unit tests can run here; Spikes 2
and 3 and the `test_2x_auth_*.py` integration tests need a host with the
container running.

---

## Sources

- [Configuring the AS ABAP to Use X.509 Client Certificates](https://help.sap.com/doc/saphelp_nw74/7.4.16/en-US/4e/1260981e3d2287e10000000a15822b/content.htm?no_cache=true)
- [SAP KBA 2960822 — How to enable SSO using X.509 client certificates](https://userapps.support.sap.com/sap/support/knowledge/en/2960822)
- [Mapping Users in Table USREXTID](https://help.sap.com/doc/saphelp_nw73ehp1/7.31.19/en-US/a8/f11960daa149958bd73c9b1b20095a/content.htm?no_cache=true)
- [Maintaining the Server's Certificate List Using SAPGENPSE](https://help.sap.com/docs/SAP_NETWEAVER_750/e73bba71770e4c0ca5fb2a3c17e8e229/0a2aa63a2719f539e10000000a11402f.html)
- [SAP KBA 2573379 — Adjusting the logon procedure list of an ICF service](https://userapps.support.sap.com/sap/support/knowledge/en/2573379)
- [SAP Single Sign-On: Authenticate with Kerberos/SPNEGO](https://community.sap.com/t5/technology-blog-posts-by-sap/sap-single-sign-on-authenticate-with-kerberos-spnego/ba-p/13321445)
- [SPNEGO: Configuring Kerberos Services with SPNego in AS ABAP](https://itsiti.com/spnego-configuring-kerberos-services-with-spnego-in-as-abap/)
- [HTTP-Based Cross-Platform Authentication via the Negotiate Protocol](https://msdn.microsoft.com/en-us/library/ms995330.aspx)
- [Developing with GSSAPI — MIT Kerberos](https://web.mit.edu/kerberos/krb5-devel/doc/appdev/gssapi.html)
- [curl: SPNEGO, Kerberos, GSS-API and Negotiate support](https://curl.se/video/curlup-2017/2017-03-19_09_Isaac_Boukris_SPNEGO_KERBEROS_GSS-API_and_Negotiate_support.pdf)
- [Logon Tickets and Assertion Tickets](https://help.sap.com/doc/saphelp_nw74/7.4.16/en-US/4c/5bd4fe97817512e10000000a42189b/content.htm?no_cache=true)
- [OAuth 2.0 with SAML Bearer Token — SAP Help](https://help.sap.com/docs/ABAP_PLATFORM_NEW/e815bb97839a4d83be6c4fca48ee5777/9526b0118cb24d129ddbea47bb061606.html)
