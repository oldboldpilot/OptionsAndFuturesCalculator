#!/usr/bin/env python3
"""Run a command with `config/.env` loaded EXACTLY as written.

`set -a; . config/.env` DOES NOT WORK FOR THIS FILE, and the way it fails is
silent. Several values are bare JSON:

    FINANCE_API_KEYS={"<key>":{"id":...,"scopes":["finance","assistant"]}}
    QUOTA_POLICY={"default_tier":"anonymous","tiers":{...}}

The value is unquoted, so the shell treats every `"` as a quoting
metacharacter and REMOVES it during word expansion. Measured 2026-09-22:
`QUOTA_POLICY` arrives 317 bytes in the file and 286 bytes in the environment,
the difference being exactly its 30 double quotes, and the engine then logs

    QUOTA_POLICY is not valid JSON

because `{default_tier:anonymous,...}` is not JSON. Nothing warns; the engine
starts fine, the key registry is EMPTY and quota is DISABLED, and a local run
then measures an engine with both controls switched off while looking healthy.
That is why the API-key and quota paths went ungated locally for so long -- not
because they are hard to exercise, but because the loader quietly removed them.

This parses the file itself and `execve`s, so no shell ever sees the values.

Usage:
    scripts/run_with_env.py -- backend/build/calculator_engine
    scripts/run_with_env.py --set PRO_GATE_MODE=off -- ./some_binary --flag
    scripts/run_with_env.py --env-file config/.env --print-keys   # names only

`--set` is applied AFTER the file, so it overrides. `--print-keys` lists the
variable names and value LENGTHS and never a value, so it is safe to paste.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def parse_env_file(path: Path) -> dict[str, str]:
    """Parse a dotenv file without a shell.

    Deliberately minimal and deliberately NOT shell-compatible: it performs no
    expansion, no interpolation and no brace handling. A value is the rest of
    the line, with one optional layer of matching surrounding quotes stripped.
    Anything cleverer would reintroduce the class of bug this file exists to
    avoid -- the loader must not be able to change the value it is loading.
    """
    env: dict[str, str] = {}
    for lineno, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export ") :].lstrip()
        if "=" not in line:
            print(f"{path}:{lineno}: no '=', skipped", file=sys.stderr)
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if not key:
            continue
        # Strip ONE matching pair of surrounding quotes; an inner quote (which
        # is what the JSON values are made of) is left exactly as written.
        if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
            value = value[1:-1]
        env[key] = value
    return env


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--env-file", default=str(REPO_ROOT / "config" / ".env"))
    ap.add_argument(
        "--set",
        action="append",
        default=[],
        metavar="K=V",
        help="override after the file is loaded; repeatable",
    )
    ap.add_argument(
        "--print-keys",
        action="store_true",
        help="list variable names and value lengths (never values) and exit",
    )
    ap.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    env_path = Path(args.env_file)
    if not env_path.is_file():
        print(f"error: no such env file: {env_path}", file=sys.stderr)
        return 2

    merged = dict(os.environ)
    from_file = parse_env_file(env_path)
    merged.update(from_file)

    for item in args.set:
        if "=" not in item:
            print(f"error: --set needs K=V, got {item!r}", file=sys.stderr)
            return 2
        k, v = item.split("=", 1)
        merged[k] = v

    if args.print_keys:
        for k in sorted(from_file):
            print(f"{k}\tlen={len(from_file[k])}")
        return 0

    cmd = args.command
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        print("error: no command given (use `-- <command> [args...]`)", file=sys.stderr)
        return 2

    try:
        os.execvpe(cmd[0], cmd, merged)
    except FileNotFoundError:
        print(f"error: command not found: {cmd[0]}", file=sys.stderr)
        return 127
    return 0  # unreachable on success


if __name__ == "__main__":
    raise SystemExit(main())
