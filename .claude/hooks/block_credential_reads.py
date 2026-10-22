#!/usr/bin/env python3
"""Refuse any Bash command that could print the CONTENTS of credential material.

A ``permissions.deny`` ``Read(...)`` rule governs only the Read tool; ``cat`` on a
dotenv is a Bash call and sails straight past it. This hook closes that gap.

WHAT THIS IS, AND IS NOT. This is defence in depth, not a security boundary. A
determined command can always reach a file the process can open -- an interpreter,
a static-linked helper, a path assembled at runtime from pieces this hook never
sees. The boundary is the operating system: file permissions, or a sandbox that
hides the path (``sandbox.filesystem.denyRead``). What this hook buys is that an
ACCIDENTAL or careless read is refused loudly instead of landing in a transcript.

Two review findings shaped it, and both are kept as test controls:

1. Exemptions tested as a SUBSTRING of the command are bypasses: a compound line
   whose first half is exempt and whose second half prints the real file was
   waved through. Hence: once credential material is named, refuse ALL shell
   compounding first, and no exemption may rescue such a line.

2. Reasoning about the raw string at all is the deeper mistake. ``cat "config/.e""nv"``,
   ``cat conf*/.en*`` and ``cat config/.en\\v`` all evaded a regex over the raw text,
   while ``run_with_env.py -- cat config/.env`` turned the sanctioned-loader
   exemption into a laundering service, and ``file -f`` / ``wc --files0-from``
   made a "metadata-only" tool echo the contents. Hence: tokenize with shlex,
   normalise and glob-expand each token, and allow children and flags only from
   fixed allowlists.

Exit 2 tells Claude Code to block the call and feed stderr back as the reason.
"""

import fnmatch
import glob
import json
import os
import re
import shlex
import sys

HOME = os.path.expanduser("~")

# Canonical credential paths, used to decide whether a GLOB could reach one even
# when it currently matches nothing on disk.
CANONICAL = [
    "config/.env",
    ".env",
    ".env.local",
    ".env.production",
    "backend/config/.env",
    f"{HOME}/.ssh/id_rsa",
    f"{HOME}/.ssh/id_ed25519",
    f"{HOME}/.aws/credentials",
    f"{HOME}/.railway/config.json",
    f"{HOME}/.netrc",
    f"{HOME}/.npmrc",
    f"{HOME}/.config/gh/hosts.yml",
]

EXAMPLE_BASENAME = re.compile(r"^\.env\.(example|sample|template)$")
SECRET_EXT = {".pem", ".key", ".p12", ".pfx", ".jks"}
SECRET_BASENAME = {"id_rsa", "id_ed25519", ".netrc", ".npmrc"}
GLOB_CHARS = set("*?[")

# A child of the sanctioned loader must come from this list, with the argv shape
# its entry allows. Deny-by-default is the only defensible rule: the loader's
# whole PURPOSE is to hand the child the secrets, so a shell, an interpreter or a
# printer as the child is simply the read with extra steps.
#
# This list SHRINKS the surface; it does not close it. Any build tool runs
# project-defined work -- ninja runs what the build file says, npm what the
# package manifest says -- so a child that can build can in principle emit the
# environment. What is excluded here are the programs that exist ONLY to execute
# an arbitrary command (npx, uv, go run, cargo run, yarn, pnpm): pure surface
# with no use this repository needs. The boundary remains the operating system.
#
# A value of None means "no subcommand required"; a set means the first non-flag
# argument must be one of these.
LOADER_CHILD_OK = {
    "ninja": None,
    "cmake": None,
    "ctest": None,
    "make": None,
    "npm": {"run", "run-script", "test", "ci", "install", "build"},
    "pytest": None,
    "true": None,
}

# Flags that make an allowlisted child execute something of the caller's
# choosing, which is the whole point of excluding generic executors.
LOADER_CHILD_BAD_FLAGS = {
    "-f", "--file", "--makefile", "--eval", "-E", "-c", "--call", "--exec",
    "-e", "--command", "--entrypoint", "-i", "--interactive",
    "-P", "--script", "--prefix", "-p", "-o",
}

# Short options may carry their value ATTACHED, so `-f/tmp/evil.mk` is a single
# token that equals no entry above. Comparing whole tokens therefore misses it.
# These prefixes are normalised back to the bare flag before the check.
ATTACHED_SHORT = ("-f", "-E", "-P", "-p", "-e", "-c", "-o")
LOADER_CHILD_BAD_SUBCOMMANDS = {"exec", "run-script-shell", "explore"}

# A built binary, named by shape rather than by a blanket "./" acceptance: a
# direct child of a build directory of this repository.
BUILD_OUTPUT = re.compile(r"^(\./)?backend/build[^/]*/[A-Za-z0-9_.+-]+$")

# Metadata-only tools, and the ONLY flags each may carry. A flag that makes a
# tool treat its argument as a LIST OF NAMES (file -f, wc --files0-from,
# sha256sum -c) echoes the file's contents, so `file` is absent entirely and the
# checksum tools take no flags at all.
META_TOOLS = {
    "ls": {"-l", "-a", "-la", "-al", "-lh", "-h", "-d", "-1", "-ld", "-lA", "-A"},
    "stat": {"-c", "-L", "--printf"},
    "wc": {"-l", "-c", "-w", "-m"},
    "du": {"-h", "-s", "-sh", "-hs"},
    "basename": set(),
    "dirname": set(),
    "realpath": set(),
    "md5sum": set(),
    "sha1sum": set(),
    "sha256sum": set(),
}
GIT_META_SUBCOMMANDS = {"check-ignore", "ls-files", "check-attr"}
GIT_META_FLAGS = {"-v", "--error-unmatch", "-z"}

COMPOUNDING = re.compile(r"[;&|`<>#]|\$\(")


def refuse(reason: str) -> None:
    print(f"BLOCKED: {reason}", file=sys.stderr)
    print("", file=sys.stderr)
    print(
        "This command names credential material (a dotenv, key, token or credential",
        file=sys.stderr,
    )
    print("store). Permitted instead, as ONE simple command:", file=sys.stderr)
    print(
        "  - load it:  scripts/run_with_env.py -- <ninja|ctest|npm|cmake|make|a ./binary>",
        file=sys.stderr,
    )
    print(
        "  - inspect:  ls / stat / wc / du / sha256sum / git check-ignore",
        file=sys.stderr,
    )
    print("", file=sys.stderr)
    print(
        "If you need a VALUE from it, ask the user; do not read the file.",
        file=sys.stderr,
    )
    sys.exit(2)


def classify(token: str) -> str:
    """Return 'cred', 'example' or 'other' for one already-expanded token."""
    expanded = os.path.expanduser(token)
    norm = os.path.normpath(expanded)
    base = os.path.basename(norm)

    if EXAMPLE_BASENAME.match(base):
        return "example"
    if base == ".env" or base.startswith(".env."):
        return "cred"
    if base in SECRET_BASENAME:
        return "cred"
    if os.path.splitext(base)[1] in SECRET_EXT:
        return "cred"

    posix = norm.replace(os.sep, "/")
    if "/.ssh/" in posix or posix.endswith("/.ssh"):
        return "cred"
    if "/secrets/" in posix or "/.secrets/" in posix:
        return "cred"
    for tail in (
        "/.aws/credentials",
        "/.railway/config.json",
        "/.config/gh/hosts.yml",
    ):
        if posix.endswith(tail):
            return "cred"
    return "other"


def candidates(token: str):
    """One token to every path it could denote: itself, its glob expansion, and
    -- for a glob that currently matches nothing -- any canonical credential path
    the pattern would accept."""
    yield token
    if GLOB_CHARS & set(token):
        matched = False
        for hit in glob.glob(os.path.expanduser(token), recursive=True):
            matched = True
            yield hit
        pattern = os.path.expanduser(token)
        for canon in CANONICAL:
            if fnmatch.fnmatch(canon, pattern) or fnmatch.fnmatch(
                os.path.abspath(canon), pattern
            ):
                matched = True
                yield canon
        if not matched:
            return


def token_kinds(tokens):
    creds, examples = [], []
    for token in tokens:
        kinds = {classify(c) for c in candidates(token)}
        if "cred" in kinds:
            creds.append(token)
        elif "example" in kinds:
            examples.append(token)
    return creds, examples


def normalise_flag(token: str) -> str:
    """Reduce a flag token to the bare flag it is.

    `--eval=x` and `-f/tmp/evil.mk` both name a flag that a whole-token
    comparison would miss: the first carries its value after an equals sign, the
    second attached directly to a short option."""
    if not token.startswith("-"):
        return token
    head = token.split("=", 1)[0]
    if token.startswith("--"):
        return head
    for short in ATTACHED_SHORT:
        if token.startswith(short) and len(token) > len(short):
            return short
    return head


def program_name(tokens):
    return os.path.basename(tokens[0]) if tokens else ""


def all_tokens(cmd):
    """Tokenise TWICE and union the results.

    shlex alone is not enough: in ``echo $(cat config/.env)`` it yields the token
    ``config/.env)``, whose basename is ``.env)`` and which therefore matches no
    credential rule -- the shell metacharacter travels INTO the token and hides
    the path. So the command is also re-tokenised with every metacharacter
    replaced by a space, and both token sets are searched."""
    try:
        literal = shlex.split(cmd)
    except ValueError:
        literal = None

    split = re.sub(r"[;&|`<>#()$\\{}]", " ", cmd)
    try:
        loosened = shlex.split(split)
    except ValueError:
        loosened = split.split()

    if literal is None:
        return None, loosened
    return literal, sorted(set(literal) | set(loosened))


def invokes_loader(tokens) -> bool:
    return any(os.path.basename(t) == "run_with_env.py" for t in tokens)


def loader_exempt(tokens) -> bool:
    """scripts/run_with_env.py hands the file to a child process without echoing
    it. Permitted only when the child is on the fixed allowlist, or is an
    explicit ./path binary, and no child argument names credential material."""
    idx = 0
    if program_name(tokens) in {"python", "python3"}:
        idx = 1
    if idx >= len(tokens):
        return False
    if os.path.basename(tokens[idx]) != "run_with_env.py":
        return False
    if "--" not in tokens:
        return False

    child = tokens[tokens.index("--") + 1:]
    if not child:
        return False
    if token_kinds(child)[0]:
        return False  # a child argument names credential material

    head = child[0]
    if BUILD_OUTPUT.match(head):
        return True

    name = os.path.basename(head)
    if name not in LOADER_CHILD_OK:
        return False

    rest = child[1:]
    for token in rest:
        if normalise_flag(token) in LOADER_CHILD_BAD_FLAGS:
            return False
        if token in LOADER_CHILD_BAD_SUBCOMMANDS:
            return False

    required = LOADER_CHILD_OK[name]
    if required is not None:
        positional = [t for t in rest if not t.startswith("-")]
        if not positional or positional[0] not in required:
            return False
    return True


def metadata_exempt(tokens) -> bool:
    """Existence, size, ignore status, digest. None of these reveals a value --
    provided no flag turns the tool into a reader of a list of names."""
    name = program_name(tokens)

    if name == "git":
        if len(tokens) < 2 or tokens[1] not in GIT_META_SUBCOMMANDS:
            return False
        rest = tokens[2:]
        return all(not t.startswith("-") or t in GIT_META_FLAGS for t in rest)

    if name not in META_TOOLS:
        return False
    allowed = META_TOOLS[name]
    for token in tokens[1:]:
        if token.startswith("-"):
            if token.split("=", 1)[0] not in allowed:
                return False
    return True


def main() -> None:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        sys.exit(0)
    cmd = (payload.get("tool_input") or {}).get("command") or ""
    if not cmd.strip():
        sys.exit(0)

    tokens, searchable = all_tokens(cmd)
    if tokens is None:
        # Unbalanced quoting: the command would not run as written, and it cannot
        # be reasoned about. Refuse only if it so much as mentions a dotenv.
        if re.search(r"\.env|\.ssh|credentials|id_rsa|\.pem", cmd):
            refuse("it is not parseable and mentions credential material.")
        sys.exit(0)

    # The sanctioned loader INJECTS the secrets into its child's environment, so
    # it is judged whenever it is invoked -- not only when a credential path is
    # named. `run_with_env.py -- env` and `-- bash -lc ...` name no credential
    # file at all and print the entire set; they are the read with extra steps.
    if invokes_loader(searchable) and not loader_exempt(tokens):
        refuse(
            "it runs the credential loader with a child that is not on the allowlist "
            "(a shell, interpreter or printer there would dump the loaded secrets)."
        )

    creds, _examples = token_kinds(searchable)
    if not creds:
        sys.exit(0)

    if COMPOUNDING.search(cmd):
        refuse("it names credential material AND uses shell compounding or redirection.")
    if "\n" in cmd:
        refuse("it names credential material AND spans multiple lines.")

    if loader_exempt(tokens):
        sys.exit(0)
    if metadata_exempt(tokens):
        sys.exit(0)

    refuse("it could print the contents of credential material.")


if __name__ == "__main__":
    main()
