#!/usr/bin/env bash
#
# capture_adt.sh — Capture ADT traffic via mitmproxy for Phase 0 ground truth
#
# Usage:
#   ./scripts/capture_adt.sh [SAP_HOST] [SAP_PORT] [PROXY_PORT]
#
# Starts mitmdump as a reverse proxy in front of the SAP system and captures
# ADT-relevant traffic to test/testdata/captured/.
#
# Prerequisites:
#   - mitmproxy installed (pip install mitmproxy, or brew install mitmproxy)
#   - SAP ABAP Cloud Developer Trial running at SAP_HOST:SAP_PORT
#
# How to use with Eclipse ADT:
#   1. Run this script — it starts a reverse proxy on localhost:PROXY_PORT
#   2. In Eclipse, create an ABAP Cloud Project pointing at localhost:PROXY_PORT
#      instead of the real SAP host (use HTTP, not HTTPS, for the proxy hop)
#   3. Perform the operations you want to capture:
#      - Discovery (automatic on project open)
#      - Create package
#      - Clone abapGit repo
#      - Pull repo
#      - Activate objects
#   4. Stop the script with Ctrl+C
#   5. Captured flows are saved to test/testdata/captured/
#
# The proxy intercepts only ADT-relevant paths (/sap/bc/adt/*) and saves
# full request/response pairs in mitmproxy flow format.

set -euo pipefail

SAP_HOST="${1:-localhost}"
SAP_PORT="${2:-50000}"
PROXY_PORT="${3:-8080}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CAPTURE_DIR="${PROJECT_ROOT}/test/testdata/captured"

# Ensure capture directory exists
mkdir -p "${CAPTURE_DIR}"

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
FLOW_FILE="${CAPTURE_DIR}/adt_capture_${TIMESTAMP}.flow"

echo "=== ADT Traffic Capture ==="
echo ""
echo "SAP system:    ${SAP_HOST}:${SAP_PORT}"
echo "Proxy listen:  localhost:${PROXY_PORT}"
echo "Flow file:     ${FLOW_FILE}"
echo ""
echo "Configure Eclipse ADT to connect to localhost:${PROXY_PORT}"
echo "Press Ctrl+C to stop capturing."
echo ""

# Check that mitmdump is available
if ! command -v mitmdump &>/dev/null; then
    echo "ERROR: mitmdump not found. Install mitmproxy:"
    echo "  pip install mitmproxy"
    echo "  # or"
    echo "  brew install mitmproxy"
    exit 1
fi

# Run mitmdump as reverse proxy:
#   --mode reverse:  proxy forwards to the real SAP system
#   --listen-port:   port Eclipse connects to
#   -w:              write flows to file
#   --flow-detail 2: log request/response summaries
#   Filter:          only capture /sap/bc/adt/ paths
mitmdump \
    --mode "reverse:http://${SAP_HOST}:${SAP_PORT}/" \
    --listen-port "${PROXY_PORT}" \
    -w "${FLOW_FILE}" \
    --flow-detail 2 \
    --set "view_filter=~u /sap/bc/adt/"

echo ""
echo "Capture saved to: ${FLOW_FILE}"
echo ""
echo "To extract individual request/response pairs, use:"
echo "  mitmdump -r ${FLOW_FILE} -n --set flow_detail=3"
