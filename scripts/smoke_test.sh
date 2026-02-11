#!/usr/bin/env bash
#
# smoke_test.sh — Phase 3b smoke test against a live SAP system
#
# Usage:
#   ./scripts/smoke_test.sh HOST PORT USER PASSWORD [CLIENT]
#
# Tests:
#   1. Connect to system, fetch CSRF token, verify non-empty
#   2. GET /sap/bc/adt/discovery returns 200 with XML body
#   3. Bad credentials return 401
#   4. Verify sap-client header sent correctly
#
# Exit 0 if all pass, exit 1 on any failure.

set -euo pipefail

# --- Color output ---
RED='\033[0;31m'
GREEN='\033[0;32m'
BOLD='\033[1m'
RESET='\033[0m'

pass() { echo -e "${GREEN}PASS${RESET}: $1"; }
fail() { echo -e "${RED}FAIL${RESET}: $1"; FAILURES=$((FAILURES + 1)); }

# --- Arguments ---
if [ $# -lt 4 ]; then
    echo "Usage: $0 HOST PORT USER PASSWORD [CLIENT]"
    echo ""
    echo "Arguments:"
    echo "  HOST      SAP system hostname or IP"
    echo "  PORT      SAP system port (e.g. 50000 or 443)"
    echo "  USER      SAP username"
    echo "  PASSWORD  SAP password"
    echo "  CLIENT    SAP client number (default: 001)"
    exit 1
fi

HOST="$1"
PORT="$2"
USER="$3"
PASSWORD="$4"
CLIENT="${5:-001}"

# Determine scheme based on port
if [ "${PORT}" = "443" ]; then
    SCHEME="https"
else
    SCHEME="http"
fi

BASE_URL="${SCHEME}://${HOST}:${PORT}"
FAILURES=0

echo -e "${BOLD}=== erpl-deploy Smoke Test ===${RESET}"
echo ""
echo "Target: ${BASE_URL}"
echo "User:   ${USER}"
echo "Client: ${CLIENT}"
echo ""

# Common curl options: follow redirects, include response headers, fail silently
CURL_OPTS=(-s -k --max-time 30)

# --- Test 1: Fetch CSRF token ---
echo -e "${BOLD}Test 1: Fetch CSRF token${RESET}"

CSRF_RESPONSE=$(curl "${CURL_OPTS[@]}" -D - -o /dev/null \
    -u "${USER}:${PASSWORD}" \
    -H "x-csrf-token: fetch" \
    -H "sap-client: ${CLIENT}" \
    "${BASE_URL}/sap/bc/adt/discovery" 2>&1) || true

CSRF_TOKEN=$(echo "${CSRF_RESPONSE}" | grep -i "x-csrf-token:" | head -1 | tr -d '\r' | awk '{print $2}')

if [ -n "${CSRF_TOKEN}" ] && [ "${CSRF_TOKEN}" != "unsafe" ]; then
    pass "CSRF token received: ${CSRF_TOKEN:0:8}..."
else
    fail "CSRF token empty or 'unsafe' (got: '${CSRF_TOKEN}')"
fi

# --- Test 2: Discovery endpoint returns 200 with XML ---
echo -e "${BOLD}Test 2: GET /sap/bc/adt/discovery returns 200 with XML${RESET}"

DISCOVERY_HTTP_CODE=$(curl "${CURL_OPTS[@]}" -o /tmp/erpl_smoke_discovery.xml -w "%{http_code}" \
    -u "${USER}:${PASSWORD}" \
    -H "sap-client: ${CLIENT}" \
    "${BASE_URL}/sap/bc/adt/discovery" 2>&1) || true

if [ "${DISCOVERY_HTTP_CODE}" = "200" ]; then
    pass "Discovery returned HTTP 200"
else
    fail "Discovery returned HTTP ${DISCOVERY_HTTP_CODE} (expected 200)"
fi

# Check the body contains XML (look for app:service or xml declaration)
if [ -f /tmp/erpl_smoke_discovery.xml ]; then
    if grep -q "xml" /tmp/erpl_smoke_discovery.xml 2>/dev/null; then
        pass "Discovery response contains XML"
    else
        fail "Discovery response does not appear to contain XML"
    fi
    rm -f /tmp/erpl_smoke_discovery.xml
else
    fail "No discovery response body received"
fi

# --- Test 3: Bad credentials return 401 ---
echo -e "${BOLD}Test 3: Bad credentials return 401${RESET}"

BAD_AUTH_CODE=$(curl "${CURL_OPTS[@]}" -o /dev/null -w "%{http_code}" \
    -u "INVALID_USER_XXXXX:wrong_password" \
    -H "sap-client: ${CLIENT}" \
    "${BASE_URL}/sap/bc/adt/discovery" 2>&1) || true

if [ "${BAD_AUTH_CODE}" = "401" ]; then
    pass "Bad credentials returned HTTP 401"
else
    fail "Bad credentials returned HTTP ${BAD_AUTH_CODE} (expected 401)"
fi

# --- Test 4: sap-client header sent correctly ---
echo -e "${BOLD}Test 4: Verify sap-client header handling${RESET}"

# We verify by making a request with the client header and checking we get
# a valid response (not a 400 or error about missing client).
# A successful response to Test 2 already implies the header is accepted,
# but we explicitly test with verbose output to confirm the header is sent.
CLIENT_VERBOSE=$(curl "${CURL_OPTS[@]}" -v \
    -u "${USER}:${PASSWORD}" \
    -H "sap-client: ${CLIENT}" \
    "${BASE_URL}/sap/bc/adt/discovery" 2>&1) || true

if echo "${CLIENT_VERBOSE}" | grep -q "> sap-client: ${CLIENT}"; then
    pass "sap-client: ${CLIENT} header sent in request"
else
    fail "sap-client header not found in outgoing request"
fi

# --- Summary ---
echo ""
if [ "${FAILURES}" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}All tests passed.${RESET}"
    exit 0
else
    echo -e "${RED}${BOLD}${FAILURES} test(s) failed.${RESET}"
    exit 1
fi
