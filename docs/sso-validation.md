# SSO implementation validation

Issue #107 has two independent transport paths. The following evidence comes
from a controlled local A4H trial, not the reporting company's installation.

## Behavioral red/green evidence

| Slice | Reproduced failure | Green coverage |
|---|---|---|
| PEM HTTPS | Client-certificate-required TLS handshake failed; plaintext certificate configuration was accepted | Successful mTLS; missing, mismatched and untrusted credentials fail; no Basic header |
| Windows certificate selection | Certificate-store options were not handled | Protected-key TLS/session fixture implemented; Windows execution remains a CI gate |
| Session persistence | A certificate session accepted unrelated saved cookies | Origin/client/authentication/certificate binding rejects legacy and mismatched SSO sessions |
| HTTP session lifecycle | A failed operation prevented later independent work | Cleanup precedes recovery; failed mutations are not replayed |
| HTTPS resource URLs | Valid absolute same-origin URLs failed or unsafe origins were accepted | Same-origin normalization, cross-origin and downgrade rejection |
| SNC SSO | The RFC logon still supplied a password field; default-name acquisition failed | Password omitted, default credential acquired, real passwordless A4H logon |
| SNC protection | Negotiated flags and per-message privacy were not enforced | Weaker negotiation and missing confidentiality rejected; QoP validated |
| RFC transport | No ADT tunnel mapping or recovery contract existed | Real SAP request/response mapping; Unicode/binary bodies and stateful lifecycle |
| RFC reconnect | A changed mapped user could dispatch the next request | Reconnected identity checked before dispatch; no mutation under changed identity |
| Identity discovery | Generic JSON Accept produced real SAP 406 | SAP vendor JSON media type; non-200 identity responses rejected |
| CLI/MCP configuration | SSO flags rejected; YAML password_env ignored | Real-process flag/profile/saved-settings tests; Basic regression |
| Factory failures | Helper error lost; CLI returned 99 despite authentication exit_code 1 | Typed error retained across helper and CLI callsites |
| Option validation | Login saved incompatible SNC/HTTPS options; SSO flags entered password wizard | Invalid combinations rejected before save/connect; explicit SSO bypasses Basic wizard |

Compilation failures, invalid test fixtures and unavailable infrastructure were
not counted as behavioral red evidence. Tests were corrected and rerun where
initial fixtures did not reach the intended behavior.

## Real SAP acceptance

`test/integration_py/test_55_sso_live.py` passed for both X.509 HTTPS and SNC RFC,
with SAP password variables removed from each child MCP process. Each path:

1. Authenticates and checks the mapped user/client.
2. Reads ADT and BW discovery and searches BW.
3. Creates a disposable class, writes Unicode source, reads it, activates it and
   verifies the active source.
4. Repeats three rejected-write followed by successful-write/activation cycles.
5. Exits while holding an explicit lock; a new process successfully relocks and
   writes the same object.
6. Deletes the object and verifies its absence from a fresh process.

The optional independent HANA security-context oracle observed zero additional
sessions after both runs. For HTTPS it also observed the test's live session
before cleanup, proving the oracle could see the session being tested.

The local HTTPS server certificate required disabling server verification for
that live test. Offline mTLS fixtures separately exercise trusted and rejected
server certificates. The live SNC test used Linux CommonCryptoLib/PSE, with
QoP 9 and neither a username nor password as a logon credential.

## Independent review

Codex agents performed separate authentication/protocol, architecture/factory,
and lifecycle/test reviews. HTTPS was reviewed by the RFC owner; RFC C++ was
reviewed by the HTTPS owner; a third reviewer examined both paths' tests and
factory integration. Findings above were fixed with regression coverage.
Claude Code, Qwen and Muse were not used.

## Remaining acceptance gates

- Complete the full existing Basic-auth SAP regression suite and investigate
  failures; targeted SSO success does not replace this suite.
- Run public HTTPS and private RFC-enabled CI on Linux, both macOS targets and
  Windows. Local Linux success does not establish Windows compilation/runtime.
- Verify the actual Windows Secure Login provider against a controlled SAP
  system, including credential renewal/expiry and reconnect behavior. A local
  non-exportable-key fixture is useful coverage, but is not this acceptance test.
- Resolve distribution terms before shipping an RFC-enabled release. The
  optional dependency retains its own license; no RFC binaries are published
  by the private validation workflow.

Keep the PR draft and issue open while these gates remain outstanding. No
claim is made about HTTP Negotiate or the reporter's unknown browser mechanism.
