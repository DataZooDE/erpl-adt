#!/usr/bin/env python3
"""scripts/check_plugin_docs.py -- keep the Claude Code plugin's command lines spelled right (offline, no SAP).

For every `erpl-adt …` command line in a ```bash fence or backtick span of plugins/erpl-adt/**.md:
  * the command (and, for a command group, its action) must exist: `erpl-adt <command> [<action>] --help` must
    print that command's own usage, and
  * every `--flag` must appear as a whole token in that help or in the GLOBAL FLAGS section of the top-level help.
Every backticked `erpl-adt-*` skill and `/erpl-adt:*` command must exist in the plugin.

Scope: spelling only. It does not check workflows, sessions, output fields or exit-code semantics.

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
FLAG_RE = re.compile(r"(?<![\w-])(--[a-z][a-z0-9-]*)")
# Global options that take a value: their value is not a command word.
LEGACY = {"deploy", "status", "pull"}
GLOBAL_WITH_VALUE = {"--host", "--port", "--user", "--password", "--password-env", "--client", "--language",
                     "--timeout", "--session-file", "-c", "--config"}


def run_help(binary, args, cache):
    key = tuple(args)
    if key not in cache:
        try:
            r = subprocess.run([binary] + args + ["--help"], capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as e:
            print(f"ERROR: cannot run {binary}: {e}", file=sys.stderr)
            sys.exit(2)
        cache[key] = (r.returncode, r.stdout + r.stderr)
    return cache[key]


def section(text, title):
    """Lines of an upper-case section (e.g. GLOBAL FLAGS) of the top-level help."""
    out, on = [], False
    for line in text.splitlines():
        if line.strip() == title:
            on = True
            continue
        if on and line and not line.startswith(" "):
            break
        if on:
            out.append(line)
    return "\n".join(out)


def command_lines(text):
    """(line number, command) for every erpl-adt invocation; joins backslash continuations."""
    in_fence, lang, pending, start = False, "", "", 0
    for i, raw in enumerate(text.splitlines()):
        m = FENCE_RE.match(raw.strip())
        if m and not in_fence:
            in_fence, lang = True, m.group(1).lower()
            continue
        if in_fence and raw.strip() == "```":
            in_fence = False
            continue
        if in_fence and lang in ("bash", "sh", ""):
            line = (pending + " " + raw.strip()) if pending else raw.strip()
            if not pending:
                start = i + 1
            if line.endswith("\\"):
                pending = line[:-1].strip()
                continue
            pending = ""
            candidates = [(start, line)]
        elif not in_fence:
            candidates = [(i + 1, s) for s in SPAN_RE.findall(raw)]
        else:
            continue
        for n, c in candidates:
            c = re.split(r"\s#|\s\|\s|\s>", " " + c)[0].strip()
            c = re.sub(r"<[^>]*>", "X", c)                      # placeholders like <handle>
            c = c.replace("uvx erpl-adt", "erpl-adt", 1)
            if c.startswith("erpl-adt ") and not c.startswith("erpl-adt-"):
                yield n, c


def command_words(tokens):
    words, skip = [], False
    for t in tokens:
        if skip:
            skip = False
            continue
        if t.startswith("-"):
            if "=" not in t and t in GLOBAL_WITH_VALUE:
                skip = True
            continue
        words.append(t)
    return words


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(REPO, "build", "erpl-adt"))
    args = ap.parse_args()
    cache = {}
    rc, top = run_help(args.binary, [], cache)
    if rc != 0:
        print(f"ERROR: {args.binary} --help failed", file=sys.stderr)
        return 2
    global_flags = section(top, "GLOBAL FLAGS")
    # Command groups are the commands that the top-level help lists with an action ("  bw lock …").
    groups = {m.group(1) for m in re.finditer(r"^  ([a-z]+) [a-z][a-z-]* ", top, re.M)}
    skills = set(os.listdir(os.path.join(PLUGIN, "skills")))
    cmd_dir = os.path.join(PLUGIN, "commands")
    commands = {f[:-3] for f in os.listdir(cmd_dir)} if os.path.isdir(cmd_dir) else set()
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
                words = command_words(tokens)
                if not words:
                    continue
                target = words[:2] if words[0] in groups and len(words) > 1 else words[:1]
                if any(w == "X" or "…" in w for w in target):
                    continue                                    # a placeholder, e.g. `erpl-adt bw <action> --help`
                rc, help_out = run_help(args.binary, target, cache)
                checked += 1
                usage = f"erpl-adt {' '.join(target)}"
                # The legacy deploy workflow (deploy/status/pull) prints a generic argparse usage line.
                legacy = target[0] in LEGACY and "Usage: erpl-adt [" in help_out
                if rc != 0 or (usage not in help_out and not legacy):
                    problems.append(f"{rel}:{line_no}\t`{usage}` is not a command: {cmd}")
                    continue
                known = set(FLAG_RE.findall(help_out)) | set(FLAG_RE.findall(global_flags))
                for flag in FLAG_RE.findall(cmd):
                    if flag not in known:
                        problems.append(f"{rel}:{line_no}\tunknown flag {flag} for {usage}")
    print("\n".join(problems))
    print(f"check_plugin_docs: {checked} command lines checked, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
