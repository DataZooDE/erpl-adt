"""Subprocess wrapper for erpl-adt CLI binary."""
import json
import shlex
import subprocess
import sys


def _mask_password(cmd):
    """Replace --password value with *** for safe logging."""
    masked = list(cmd)
    for i, arg in enumerate(masked):
        if arg == "--password" and i + 1 < len(masked):
            masked[i + 1] = "***"
        elif arg.startswith("--password="):
            masked[i] = "--password=***"
    return masked


class CliRunner:
    """Invoke the erpl-adt binary and parse JSON output."""

    def __init__(self, binary_path, host, port, user, password, client):
        self.binary = str(binary_path)
        self.base_args = [
            "--host", host,
            "--port", str(port),
            "--user", user,
            "--password", password,
            "--client", client,
            "--json=true",
        ]

    def run(self, *args, session_file=None, timeout=120, extra_flags=None):
        """Run a CLI command and return CompletedProcess."""
        cmd = [self.binary] + self.base_args
        if session_file:
            cmd += ["--session-file", str(session_file)]
        if extra_flags:
            cmd += [str(f) for f in extra_flags]
        cmd += [str(a) for a in args]
        # Log command for documentation / debugging
        masked = _mask_password(cmd)
        print(f"\n$ {' '.join(shlex.quote(a) for a in masked)}",
              file=sys.stderr)
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
        )
        if result.returncode != 0:
            print(f"  -> exit {result.returncode}", file=sys.stderr)
        return result

    def run_raw(self, *args, timeout=120, env=None):
        """Run the binary without --json=true and without base connection args.

        Useful for testing --version, --help, and --password-env.
        """
        cmd = [self.binary] + list(str(a) for a in args)
        masked = _mask_password(cmd)
        print(f"\n$ {' '.join(shlex.quote(a) for a in masked)}",
              file=sys.stderr)
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout, env=env,
        )
        if result.returncode != 0:
            print(f"  -> exit {result.returncode}", file=sys.stderr)
        return result

    def run_no_json(self, *args, session_file=None, timeout=120):
        """Run a CLI command without --json=true (human-readable output)."""
        cmd = [self.binary]
        # Add base args but skip --json=true
        for a in self.base_args:
            if a == "--json=true":
                continue
            cmd.append(a)
        if session_file:
            cmd += ["--session-file", str(session_file)]
        cmd += [str(a) for a in args]
        masked = _mask_password(cmd)
        print(f"\n$ {' '.join(shlex.quote(a) for a in masked)}",
              file=sys.stderr)
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
        )
        if result.returncode != 0:
            print(f"  -> exit {result.returncode}", file=sys.stderr)
        return result

    def run_ok(self, *args, **kwargs):
        """Run a CLI command, assert exit code 0, return parsed JSON."""
        result = self.run(*args, **kwargs)
        assert result.returncode == 0, (
            f"CLI failed (exit {result.returncode}):\n"
            f"  cmd: {' '.join(shlex.quote(a) for a in _mask_password(result.args))}\n"
            f"  stderr: {result.stderr}"
        )
        stdout = result.stdout.strip()
        if not stdout:
            return {}
        return json.loads(stdout)

    def run_fail(self, *args, **kwargs):
        """Run a CLI command, assert exit code != 0, return CompletedProcess."""
        result = self.run(*args, **kwargs)
        assert result.returncode != 0, (
            f"CLI unexpectedly succeeded:\n  stdout: {result.stdout}"
        )
        return result
