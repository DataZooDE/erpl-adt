# Passwordless SAP connections

Select the transport explicitly with `--auth x509` or `--auth snc`. Both modes
use SAP's mapped user identity; `--user` optionally asserts the expected user.
They never fall back to a Basic password, including an ambient `SAP_PASSWORD`.
A browser downloading discovery without a prompt does not identify its
mechanism: a cached certificate selection, HTTP Negotiate, or an existing
browser session can all produce that observation. HTTP Negotiate and browser
cookie import are not implemented.

## HTTPS with a client certificate

The SAP ICM must trust the client certificate and map it to a user in the chosen
client. Use an HTTPS endpoint, even when discovery advertises an HTTP URL.
The client rejects cross-origin endpoints and TLS downgrades.

```yaml
connection:
  host: sap.example.com
  port: 443
  https: true
  client: '040'
  auth: x509
  client_certificate: /secure/client-chain.pem
  client_key: /secure/client.key
  ca_certificate: /secure/company-ca.pem
```

```sh
erpl-adt discover services --config connection.yaml
erpl-adt mcp --config connection.yaml
```

The certificate file may contain its intermediate chain. The PEM key must be
unencrypted; protect its filesystem permissions. Encrypted PEM keys are
rejected without prompting. The CA bundle is optional when the server is
already trusted. Avoid `--insecure` outside disposable test systems.

On Windows, a certificate with a protected/non-exportable private key can be
selected from the current user's `MY` store:

```sh
erpl-adt discover services --host sap.example.com --port 443 --https \
  --client 040 --auth x509 --client-certificate-thumbprint SHA1_THUMBPRINT
```

This uses WinHTTP and the Windows key provider. Do not combine a thumbprint
with PEM key/certificate/CA options; Windows uses its own server trust store.
The certificate must be available to the account running the process. Native
Windows test coverage uses a non-exportable key; a particular Secure Login
Client installation still needs its own acceptance test.

## RFC tunnel with SNC SSO

RFC support is an optional build feature. It uses erpl-proto, not the SAP NW RFC
SDK. The operator supplies an SNC mechanism library and an initiating
credential, and SAP must trust/map that identity. No password is required.

```yaml
connection:
  host: sap.example.com
  client: '040'
  auth: snc
  sysnr: '00'
  snc_library: /opt/sapcrypto/libsapcrypto.so
  snc_partner: 'p:CN=SAPSERVER'
  snc_qop: '9'
```

Use the same CLI/MCP commands with this profile. Configure the credential
location as required by the mechanism (for CommonCryptoLib, typically
`SECUDIR`). `snc_name` is optional: omission selects the mechanism's default
initiating credential. QoP 1, 2, 3 and 9 are supported; 9 requires privacy, and
policy-dependent QoP 8 is rejected. Negotiated protection must meet the
requested level. HTTPS certificate options are invalid for this transport.

Each RFC connection owns the ADT stateful context. Keep lock/write/unlock
operations in one MCP process. HTTP cookie session files are not supported
for RFC. A new independent MCP tool call can recover after a failed operation;
mutations are never replayed automatically.

## Saving settings

`erpl-adt login` accepts the same explicit authentication flags and saves
certificate/library references in `.adt.creds`, without copying private keys
or saving an SSO password. Command flags override environment variables,
which override a YAML connection profile, saved credentials, and defaults.
For Basic authentication, YAML `password_env` remains supported.

An X.509 cookie session file is bound to the origin, SAP client and certificate
identity. Legacy/unbound or mismatched files are rejected. Use separate
session files for separate identities.

## Building and validating

The default build includes HTTPS certificate support. To build RFC support,
obtain authorized access to the pinned private erpl-proto revision and install
the Rust toolchain specified in `cmake/ErplProto.cmake`:

```sh
cmake -S . -B build -DERPL_ADT_WITH_RFC=ON \
  -DFETCHCONTENT_SOURCE_DIR_ERPL_PROTO=/path/to/pinned/erpl-proto
cmake --build build
```

The dependency retains its BSL 1.1 license; RFC-enabled binaries are subject to
those terms. Neither SAP crypto libraries nor SAP SDK binaries are bundled.

Offline usage tests run the actual CLI and MCP binaries against a local mTLS
server. Live acceptance runs the actual MCP binary against SAP, verifies the
mapped identity, discovery/search, Unicode class creation/write/activation,
recovery after rejected writes, deletion, and lock cleanup on process exit.

```sh
ERPL_ADT_BINARY=/path/to/erpl-adt uv run --project test/integration_py pytest -q \
  test/integration_py/test_54_sso_protocol.py
SAP_X509_TEST_CONFIG=/secure/x509.yaml SAP_SNC_TEST_CONFIG=/secure/snc.yaml \
  ERPL_ADT_BINARY=/path/to/erpl-adt uv run --project test/integration_py pytest -q \
  test/integration_py/test_55_sso_live.py
```

Live tests skip a mode whose profile is absent; a skip is not acceptance
coverage. The controlled Linux A4H tests do not establish the authentication
mechanism or compatibility of an unobserved customer installation.
