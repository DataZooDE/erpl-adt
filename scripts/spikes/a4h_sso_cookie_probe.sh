#!/usr/bin/env bash
# Spike 3 — MYSAPSSO2 logon-ticket replay against a STOCK a4h container.
#
# Proves the cheapest passwordless path end-to-end with ZERO server-side
# configuration: log on once with a password, keep the SAP logon ticket, then
# call ADT again with the cookie only and no Authorization header.
#
#   SAP_PASSWORD=... ./scripts/spikes/a4h_sso_cookie_probe.sh
#
# Env (all optional except SAP_PASSWORD):
#   SAP_HOST=localhost  SAP_PORT=50000  SAP_USER=DEVELOPER  SAP_CLIENT=001
set -euo pipefail

: "${SAP_PASSWORD:?SAP_PASSWORD must be set}"
SAP_HOST="${SAP_HOST:-localhost}"
SAP_PORT="${SAP_PORT:-50000}"
SAP_USER="${SAP_USER:-DEVELOPER}"
SAP_CLIENT="${SAP_CLIENT:-001}"

BASE="http://${SAP_HOST}:${SAP_PORT}"
DISCOVERY="${BASE}/sap/bc/adt/discovery"
JAR="$(mktemp)"
trap 'rm -f "$JAR"' EXIT

fail=0
check() {
    if [[ "$1" == "0" ]]; then echo "  [PASS] $2"; else echo "  [FAIL] $2"; fail=1; fi
}

echo "== Leg 1: password logon, capture the logon ticket =="
code="$(curl -sS -o /dev/null -w '%{http_code}' -c "$JAR" \
    -u "${SAP_USER}:${SAP_PASSWORD}" \
    -H "sap-client: ${SAP_CLIENT}" "$DISCOVERY")"
[[ "$code" == "200" ]] && check 0 "password logon returned 200" \
                       || check 1 "password logon returned $code (expected 200)"

if grep -q 'MYSAPSSO2' "$JAR"; then
    check 0 "MYSAPSSO2 logon ticket issued"
    TICKET_NAME=MYSAPSSO2
elif grep -qi 'SAP_SESSIONID' "$JAR"; then
    # Some trial images issue only a security session cookie. That is still a
    # passwordless replay credential, just a shorter-lived one — worth knowing.
    echo "  [NOTE] no MYSAPSSO2; falling back to SAP_SESSIONID_* security session"
    TICKET_NAME=SAP_SESSIONID
else
    check 1 "no replayable cookie in the jar"
    echo "--- cookie jar ---"; cat "$JAR"; exit 1
fi

echo
echo "== Leg 2: replay the cookie, NO password, NO Authorization header =="
code="$(curl -sS -o /dev/null -w '%{http_code}' -b "$JAR" \
    -H "sap-client: ${SAP_CLIENT}" "$DISCOVERY")"
[[ "$code" == "200" ]] && check 0 "cookie-only request returned 200 (${TICKET_NAME})" \
                       || check 1 "cookie-only request returned $code (expected 200)"

echo
echo "== Control: no cookie, no password must NOT succeed =="
code="$(curl -sS -o /dev/null -w '%{http_code}' \
    -H "sap-client: ${SAP_CLIENT}" "$DISCOVERY")"
[[ "$code" != "200" ]] && check 0 "anonymous request rejected ($code)" \
                       || check 1 "anonymous request returned 200 — the ICF node is unprotected, this spike proves nothing"

echo
if [[ "$fail" == "0" ]]; then echo "Spike 3: VERIFIED"; else echo "Spike 3: FAILED"; fi
exit "$fail"
