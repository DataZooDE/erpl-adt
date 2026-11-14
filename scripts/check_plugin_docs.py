#!/usr/bin/env python3
"""scripts/check_plugin_docs.py -- keep the Claude Code plugin honest (offline, no SAP).

For every `erpl-adt …` command line in a ```bash fence or backtick span of plugins/erpl-adt/**.md:
  * `erpl-adt <command> [<action>] --help` must succeed (the command exists), and
  * every `--flag` on the line must appear in that help text or in the global flags.
Every backticked `erpl-adt-*` skill and `/erpl-adt:*` command must exist in the plugin.

Usage: python3 scripts/check_plugin_docs.py [--binary build/erpl-adt]
Exit 0 clean, 1 on problems, 2 when the binary cannot run.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN = os.path.join(REPO, "plugins", "erpl-adt")
FENCE_RE = re.compile(r"^```[ \t]*([A-Za-z0-9_+-]*)[ \t]*$")
SPAN_RE = re.compile(r"`([^`\n]*)`")
SKILL_RE = re.compile(r"^erpl-adt-[a-z0-9-]+$")
COMMAND_RE = re.compile(r"^/erpl-adt:([a-z0-9-]+)")
# Commands that take an action word as their second token.
GROUPS = {"search", "object", "source", "transport", "ddic", "package", "discover", "bw", "catalog", "screen",
          "abapgit"}


def help_text(binary, args, cache):
    key = tuple(args)
    if key not in cache:
        try:
            r = subprocess.run([binary] + args + ["--help"], capture_output=True, text=True, timeout=30)
        except OSError as e:
            print(f"ERROR: cannot run {binary}: {e}", file=sys.stderr)
            sys.exit(2)
        cache[key] = (r.returncode, r.stdout + r.stderr)
    return cache[key]


def command_lines(text):
    in_fence, lang = False, ""
    for i, line in enumerate(text.splitlines()):
        m = FENCE_RE.match(line.strip())
        if m and not in_fence:
            in_fence, lang = True, m.group(1).lower()
            continue
        if in_fence and line.strip() == "```":
            in_fence = False
            continue
        candidates = [line] if in_fence and lang in ("bash", "sh", "") else SPAN_RE.findall(line)
        for c in candidates:
            c = c.split("#", 1)[0].split("|", 1)[0].split(">", 1)[0].strip().rstrip("\\").strip()
            if c.startswith("erpl-adt ") or c.startswith("uvx erpl-adt "):
                yield i + 1, c.replace("uvx erpl-adt", "erpl-adt", 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(REPO, "build", "erpl-adt"))
    args = ap.parse_args()
    cache = {}
    rc, global_help = help_text(args.binary, [], cache)
    if rc != 0:
        print(f"ERROR: {args.binary} --help failed", file=sys.stderr)
        return 2
    skills = set(os.listdir(os.path.join(PLUGIN, "skills")))
    commands = {f[:-3] for f in os.listdir(os.path.join(PLUGIN, "commands"))} if os.path.isdir(
        os.path.join(PLUGIN, "commands")) else set()
    problems, checked = [], 0
    for base, _, names in os.walk(PLUGIN):
        for name in sorted(names):
            if not name.endswith(".md"):
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, REPO).replace(os.sep, "/")
            with open(path, encoding="utf-8") as f:
                text = f.read()
            for i, line in enumerate(text.splitlines()):
                for span in SPAN_RE.findall(line):
                    s = span.strip()
                    if SKILL_RE.match(s) and s not in skills:
                        problems.append(f"{rel}:{i + 1}\tskill {s} does not exist")
                    m = COMMAND_RE.match(s)
                    if m and m.group(1) not in commands:
                        problems.append(f"{rel}:{i + 1}\tcommand /erpl-adt:{m.group(1)} does not exist")
            for line_no, cmd in command_lines(text):
                try:
                    tokens = shlex.split(cmd)[1:]
                except ValueError:
                    tokens = cmd.split()[1:]
                words = [t for t in tokens if not t.startswith("-")]
                if not words:
                    continue
                path_args = words[:2] if words[0] in GROUPS and len(words) > 1 else words[:1]
                rc, text_help = help_text(args.binary, path_args, cache)
                checked += 1
                if rc != 0 or "Unknown" in text_help.splitlines()[0:1]:
                    problems.append(f"{rel}:{line_no}\t`erpl-adt {' '.join(path_args)}` has no help: {cmd}")
                    continue
                for flag in re.findall(r"(?<!\S)(--[a-z][a-z0-9-]*)", cmd):
                    if flag not in text_help and flag not in global_help:
                        problems.append(f"{rel}:{line_no}\tunknown flag {flag} for erpl-adt {' '.join(path_args)}")
    print("\n".join(problems))
    print(f"check_plugin_docs: {checked} command lines checked, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
