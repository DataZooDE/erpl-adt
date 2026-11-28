"""Real CLI E2E against an ICF session ledger with fault injection.

Models 98 existing security sessions out of 100, two independent HTTP clients,
CSRF cookies, stateful locks, helper creation/write/activation/classrun and
logoff. This is deterministic protocol coverage, not proof of SAP's SM05 count.
"""

import json
import os
import re
import subprocess
import threading
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
ERROR = ('<html><head><title>Service cannot be reached</title></head>'
         '<body><p>400 Session not found</p></body></html>')
LOCK = (ROOT / 'testdata/object/lock_response.xml').read_text()
ACTIVATED = '<chkl:activationResultList xmlns:chkl="http://www.sap.com/adt/checklistresult"/>'


class SessionLedger:
    def __init__(self, fault):
        self.fault = fault
        self.sessions = {f'preexisting-{i}' for i in range(98)}
        self.before = self.sessions.copy()
        self.requests = []
        self.created = 0
        self.failed = False
        self.helper = None
        self.peak = 98
        self.after_failure = []
        self.op_count = 0

    def dispatch(self, req):
        body = req.rfile.read(int(req.headers.get('Content-Length', 0))).decode()
        cookies = SimpleCookie(req.headers.get('Cookie', ''))
        sid = cookies.get('SAP_SESSIONID_A4H_001')
        sid = sid.value if sid else None
        self.requests.append((req.command, req.path, sid))
        if self.failed and req.path != '/sap/public/bc/icf/logoff':
            self.after_failure.append((req.command, req.path, sid))
        if req.path == '/sap/public/bc/icf/logoff':
            # Reject a stale stateful context, as an ICF failure can do.
            if req.headers.get('X-sap-adt-sessiontype') == 'stateful':
                return 400, {}, ERROR
            if self.fault == 'logoff' and not self.failed:
                self.failed = True
                return 503, {}, ERROR
            self.sessions.discard(sid)
            return 200, {}, ''
        headers = {}
        if sid not in self.sessions:
            if len(self.sessions) >= 100:
                self.failed = True
                return 429, {}, '<html><title>Too Many Logons</title></html>'
            self.created += 1
            sid = f'new-{self.created}'
            self.sessions.add(sid)
            self.peak = max(self.peak, len(self.sessions))
            headers['Set-Cookie'] = f'SAP_SESSIONID_A4H_001={sid}; Path=/'
        path = req.path
        point = ('csrf' if path.endswith('/discovery') else
                 'lock' if '_action=LOCK' in path else
                 'classrun' if '/classrun/' in path else None)
        if point == self.fault or (self.fault == '429' and point == 'classrun'):
            self.failed = True
            return (429 if self.fault == '429' else 400), headers, ERROR
        if path.endswith('/discovery'):
            headers['x-csrf-token'] = 'token'
            return 200, headers, '<discovery/>'
        if 'informationsystem/search' in path:
            return 200, headers, '<adtcore:objectReferences xmlns:adtcore="http://www.sap.com/adt/core"/>'
        if req.command == 'POST' and path == '/sap/bc/adt/oo/classes':
            self.helper = True
            return 201, headers, ''
        if '_action=LOCK' in path:
            headers['Set-Cookie'] = f'sap-contextid={sid}; Path=/'
            return 200, headers, LOCK
        if req.command == 'PUT':
            self.op_count = len(re.findall(r"op = '\d+'", body))
            return 204, headers, ''
        if req.command == 'DELETE':
            self.helper = False
            return 204, headers, ''
        if '_action=UNLOCK' in path:
            return 204, headers, ''
        if '/activation' in path:
            return 200, headers, ACTIVATED
        if '/classrun/' in path:
            output = '@1 META TEST 20261128 120000\n'
            output += ''.join(f'@{i} EXISTS 0\n@{i} RC 0\n'
                              for i in range(1, self.op_count + 1))
            return 200, headers, output + '@END\n'
        if '/oo/classes/' in path:
            return (200 if self.helper else 404), headers, ''
        raise AssertionError(f'Unexpected request: {req.command} {path}')


@pytest.mark.parametrize('fault', ['csrf', 'lock', 'logoff', 'classrun', '429', 'none'])
@pytest.mark.parametrize('command', ['deploy', 'export'])
def test_abapgit_stops_without_leaking_near_session_limit(tmp_path, fault, command):
    ledger = SessionLedger(fault)

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def handle_request(self):
            status, headers, body = ledger.dispatch(self)
            self.send_response(status)
            for key, value in headers.items():
                self.send_header(key, value)
            payload = body.encode()
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        do_GET = do_POST = do_PUT = do_DELETE = handle_request

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    binary = os.environ.get('ERPL_ADT_BINARY', str(ROOT.parent / 'build/erpl-adt'))
    args = [binary, '--host', '127.0.0.1', '--port', str(server.server_port),
            '--user', 'TEST', '--password', 'TEST', '--client', '001', '--json',
            'abapgit', command]
    if command == 'deploy':
        args += [str(ROOT / 'testdata/abapgit/PROG/src/zag_unit_test.prog.xml')]
    else:
        args += [str(tmp_path / 'export'), '--object', 'PROG:ZAG_UNIT_TEST']
    args += ['--package', '$TMP']
    try:
        result = subprocess.run(args, capture_output=True, text=True, cwd=tmp_path, timeout=30)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    if fault == 'none':
        assert not ledger.failed
        # Exporting an absent object fails normally; deploying it succeeds.
        assert result.returncode == (0 if command == 'deploy' else 2), result.stdout + result.stderr
        assert ledger.helper is False
    else:
        assert ledger.failed, result.stdout + result.stderr
        assert result.returncode != 0
    assert ledger.sessions == ledger.before, ledger.requests
    assert not ledger.after_failure, ledger.after_failure
    # A failed operation must not launch another helper lock or another logon.
    failures = [r for r in ledger.requests if
                ('_action=LOCK' in r[1] if fault == 'lock' else
                 '/classrun/' in r[1] if fault in ('classrun', '429') else
                 r[1].endswith('/discovery') if fault == 'csrf' else False)]
    if fault not in ('logoff', 'none'):
        assert len(failures) == 1, ledger.requests
    if fault in ('csrf', 'lock', 'classrun'):
        report = json.loads(result.stdout)
        assert 'Session not found' in report['objects'][0]['message']
        assert 'SM05' in report['objects'][0]['hint']
    assert ledger.peak <= 100
