#!/usr/bin/env bash
# Run the passwordless-SSO spikes that need nothing but this repo.
#
#   ./scripts/spikes/run_offline_spikes.sh
#
# Spike 1 — cpp-httplib mutual TLS (gates the X.509 track)
# Spike 4 — GSS-API via dlopen, no build-time krb5 dependency (gates SPNEGO)
#
# Spikes 2 and 3 need a running a4h container; see a4h_x509_setup.sh and
# a4h_sso_cookie_probe.sh.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== Spike 1: cpp-httplib mutual TLS =="

HTTPLIB="$(find "$HERE/../.." -name httplib.h -path '*vcpkg*' 2>/dev/null | head -1)"
if [[ -z "$HTTPLIB" ]]; then
    echo "  httplib.h not found in vcpkg_installed; downloading a copy"
    curl -fsSL -o "$WORK/httplib.h" \
        https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
    HTTPLIB_DIR="$WORK"
else
    HTTPLIB_DIR="$(dirname "$HTTPLIB")"
fi

cd "$WORK"
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.crt -days 2 \
    -subj "/CN=ERPL Test CA/O=ERPL/C=DE" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -keyout server.key -out server.csr \
    -subj "/CN=localhost" 2>/dev/null
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out server.crt -days 2 \
    -extfile <(printf "subjectAltName=DNS:localhost,IP:127.0.0.1") 2>/dev/null
openssl req -newkey rsa:2048 -nodes -keyout client.key -out client.csr \
    -subj "/CN=DEVELOPER/O=ERPL/C=DE" 2>/dev/null
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out client.crt -days 2 2>/dev/null

g++ -std=c++17 -O1 -I"$HTTPLIB_DIR" -DCPPHTTPLIB_OPENSSL_SUPPORT \
    "$HERE/spike1_mtls.cpp" -o spike1 -lssl -lcrypto -lpthread
./spike1

echo
echo "== Spike 4 (binding half): GSS-API via dlopen =="
gcc -std=c11 -O1 "$HERE/spike4_gss.c" -o spike4 -ldl
./spike4
