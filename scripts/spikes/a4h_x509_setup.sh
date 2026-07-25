#!/usr/bin/env bash
# Spike 2 — can the a4h container be made to accept an X.509 client
# certificate for ADT, with no password?
#
#   UNVERIFIED. This script has NOT yet been run against a live container.
#   It is the executable form of the plan in docs/passwordless-sso-plan.md
#   §4 Spike 2, and is the gate for the X.509 implementation track. Each step
#   verifies itself and stops on the first failure, so a run tells you exactly
#   which assumption breaks.
#
#   SAP_PASSWORD=... ./scripts/spikes/a4h_x509_setup.sh
#
# Env:
#   A4H_CONTAINER=a4h  SID=A4H  INSTANCE=00  SAP_CLIENT=001  SAP_USER=DEVELOPER
#   HANA_USER=SAPA4H  HANA_PASSWORD=ABAPtr2023#00  HTTPS_PORT=44300
#
# Steps (mirrors the SAP docs cited in the plan):
#   1. generate a CA + a client certificate for CN=<SAP_USER>
#   2. add the CA to the SSL Server Standard PSE   (sapgenpse maintain_pk)
#   3. set icm/HTTPS/verify_client = 1             (instance profile)
#   4. map the certificate DN to the ABAP user     (USREXTID)
#   5. restart the instance
#   6. assert: curl --cert/--key gets 200 from /sap/bc/adt/discovery, no -u
set -euo pipefail

A4H_CONTAINER="${A4H_CONTAINER:-a4h}"
SID="${SID:-A4H}"
INSTANCE="${INSTANCE:-00}"
SAP_CLIENT="${SAP_CLIENT:-001}"
SAP_USER="${SAP_USER:-DEVELOPER}"
HANA_USER="${HANA_USER:-SAPA4H}"
HANA_PASSWORD="${HANA_PASSWORD:-ABAPtr2023#00}"
HTTPS_PORT="${HTTPS_PORT:-44300}"

LOWER_SID="$(echo "$SID" | tr '[:upper:]' '[:lower:]')"
ADM="${LOWER_SID}adm"
SECDIR="/usr/sap/${SID}/D${INSTANCE}/sec"
PROFILE_DIR="/usr/sap/${SID}/SYS/profile"

WORK="${WORK:-$(pwd)/.spike2}"
mkdir -p "$WORK"

DN="CN=${SAP_USER}, O=ERPL, C=DE"

say()  { printf '\n== %s ==\n' "$1"; }
die()  { printf '  [FAIL] %s\n' "$1"; exit 1; }
ok()   { printf '  [PASS] %s\n' "$1"; }
inc()  { docker exec "$A4H_CONTAINER" bash -lc "$1"; }
asadm(){ docker exec "$A4H_CONTAINER" bash -lc "su - ${ADM} -c '$1'"; }
hdb()  { docker exec "$A4H_CONTAINER" /usr/sap/${SID}/hdbclient/hdbsql \
             -i 02 -d HDB -u "$HANA_USER" -p "$HANA_PASSWORD" "$1"; }

# -- 0: container reachable ---------------------------------------------------
say "0. container"
docker inspect "$A4H_CONTAINER" >/dev/null 2>&1 \
    || die "container '${A4H_CONTAINER}' not found (set A4H_CONTAINER)"
ok "container ${A4H_CONTAINER} present"

# -- 1: certificates ----------------------------------------------------------
say "1. generate CA + client certificate for ${DN}"
if [[ ! -f "$WORK/ca.crt" ]]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 365 \
        -keyout "$WORK/ca.key" -out "$WORK/ca.crt" \
        -subj "/CN=ERPL Test CA/O=ERPL/C=DE" 2>/dev/null
fi
if [[ ! -f "$WORK/client.crt" ]]; then
    openssl req -newkey rsa:2048 -nodes \
        -keyout "$WORK/client.key" -out "$WORK/client.csr" \
        -subj "/C=DE/O=ERPL/CN=${SAP_USER}" 2>/dev/null
    openssl x509 -req -in "$WORK/client.csr" -CA "$WORK/ca.crt" \
        -CAkey "$WORK/ca.key" -CAcreateserial -out "$WORK/client.crt" \
        -days 365 2>/dev/null
fi
ok "client cert: $(openssl x509 -in "$WORK/client.crt" -noout -subject)"

# -- 2: add the CA to the SSL Server Standard PSE ------------------------------
say "2. add CA to SSL Server Standard PSE (${SECDIR}/SAPSSLS.pse)"
inc "test -f ${SECDIR}/SAPSSLS.pse" \
    || die "SAPSSLS.pse not found — HTTPS is probably not configured on this image.
         Configure the SSL server PSE first (STRUST), or point SECDIR elsewhere."
docker cp "$WORK/ca.crt" "${A4H_CONTAINER}:/tmp/erpl_ca.crt"
asadm "cd ${SECDIR} && sapgenpse maintain_pk -a /tmp/erpl_ca.crt -p SAPSSLS.pse" \
    || die "sapgenpse maintain_pk failed (PSE may have a PIN — pass -x <PIN>)"
asadm "cd ${SECDIR} && sapgenpse maintain_pk -l -p SAPSSLS.pse" | grep -qi 'ERPL Test CA' \
    && ok "CA present in the PSE certificate list" \
    || die "CA not visible in the PSE certificate list after maintain_pk"

# -- 3: icm/HTTPS/verify_client ------------------------------------------------
say "3. set icm/HTTPS/verify_client = 1"
PROFILE="$(inc "ls ${PROFILE_DIR}/${SID}_D${INSTANCE}_* 2>/dev/null | head -1")"
[[ -n "$PROFILE" ]] || die "instance profile not found under ${PROFILE_DIR}"
inc "grep -q '^icm/HTTPS/verify_client' ${PROFILE} \
     && sed -i 's|^icm/HTTPS/verify_client.*|icm/HTTPS/verify_client = 1|' ${PROFILE} \
     || echo 'icm/HTTPS/verify_client = 1' >> ${PROFILE}"
inc "grep '^icm/HTTPS/verify_client' ${PROFILE}" | grep -q '= *1' \
    && ok "icm/HTTPS/verify_client = 1 in $(basename "$PROFILE")" \
    || die "failed to set icm/HTTPS/verify_client"

# -- 4: DN -> ABAP user mapping ------------------------------------------------
# Same direct-to-HANA technique CLAUDE.md documents for BW activation. The
# supported route is SM30 view VUSREXTID; this is the container shortcut.
say "4. map '${DN}' -> ${SAP_USER} in USREXTID"
hdb "UPSERT ${HANA_USER}.USREXTID (MANDT, IDTYPE, EXTID, BNAME) \
     VALUES ('${SAP_CLIENT}', 'DN', '${DN}', '${SAP_USER}') \
     WITH PRIMARY KEY" \
    || die "USREXTID upsert failed — check the table owner and column layout with:
         hdbsql \"SELECT * FROM ${HANA_USER}.USREXTID\""
hdb "SELECT BNAME FROM ${HANA_USER}.USREXTID WHERE IDTYPE='DN'" | grep -q "$SAP_USER" \
    && ok "USREXTID row present" \
    || die "USREXTID row not readable back"

# -- 5: restart ----------------------------------------------------------------
say "5. restart the instance (ICM must re-read the profile and the PSE)"
asadm "sapcontrol -nr ${INSTANCE} -function RestartInstance" || true
asadm "sapcontrol -nr ${INSTANCE} -function WaitforStarted 600 10" \
    || die "instance did not come back up"
ok "instance restarted"

# -- 6: the actual assertion ---------------------------------------------------
say "6. ASSERT: passwordless X.509 logon to ADT"
code="$(curl -sS -k -o /dev/null -w '%{http_code}' \
    --cert "$WORK/client.crt" --key "$WORK/client.key" \
    -H "sap-client: ${SAP_CLIENT}" \
    "https://localhost:${HTTPS_PORT}/sap/bc/adt/discovery" || echo 000)"
[[ "$code" == "200" ]] \
    && ok "client-certificate logon returned 200 — no password sent" \
    || die "client-certificate logon returned ${code}.
         Check: SMICM trace, the ICF logon procedure list for /sap/bc/adt
         (SICF -> 'Logon Data' must include the certificate procedure), and
         that the DN string in USREXTID matches the ICM's rendering exactly."

echo
echo "Spike 2: VERIFIED — X.509 client-certificate SSO works on a4h."
echo "Artifacts in ${WORK} (client.crt / client.key) drive the Phase 2"
echo "integration test test/integration_py/test_23_auth_x509.py."
