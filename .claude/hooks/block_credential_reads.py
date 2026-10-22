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
import shutil
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

# Flags are ALLOWLISTED per tool, not denylisted. A denylist of flag spellings
# cannot be completed: a short option may be clustered with another (-kf names a
# makefile while equalling neither -k nor -f), and GNU long options accept any
# unambiguous abbreviation (--ev reaches --eval), so for every entry there is a
# spelling that misses it. The allowlist asks the decidable question instead --
# is this flag one of the few this repository's workflow actually uses?
#
# "exact" matches the flag as written; "prefix" additionally permits a value
# attached directly to it, as in -C<dir> or -j8. Note -C is permitted for the
# build tools, where it selects a directory, and REFUSED for the configuration
# tool, where it loads and executes a script.
TOOL_FLAGS = {
    "ninja": {"exact": {"-v", "-k", "-n"}, "prefix": ("-C", "-j")},
    "make": {"exact": {"-k", "-n", "-s", "-B"}, "prefix": ("-C", "-j")},
    "cmake": {
        "exact": {"--build", "--install", "--fresh", "-S", "-B", "-G"},
        "prefix": ("-D", "-S", "-B", "-G"),
    },
    "ctest": {
        "exact": {"--output-on-failure", "--test-dir", "-V", "--verbose", "-N"},
        "prefix": ("-R", "-j", "--test-dir"),
    },
    "npm": {"exact": {"--silent"}, "prefix": ()},
    "pytest": {"exact": {"-q", "-x", "-v", "-s"}, "prefix": ("-k", "--maxfail", "--tb")},
    "true": {"exact": set(), "prefix": ()},
}

# Naming a permitted FLAG is not the same as permitting its VALUE. A cache entry
# can name a script the configuration tool will execute, or a launcher it will
# run every compiler invocation through; a directory flag can point at a tree
# outside this repository whose build files are someone else's. So the few flags
# whose value is a path are constrained to resolve inside the repository, and
# cache entries are allowlisted BY KEY -- the keys this repository's own
# instructions use, and no others.
PATH_VALUE_FLAGS = ("-C", "-S", "-B", "--test-dir", "--build")

# Allowlisting the KEY is still not enough: the compiler and launcher keys that
# this repository's instructions require both name a program that will be
# executed, so an arbitrary path there is an arbitrary program. Each permitted
# key therefore also pins the shape of its value.
# A BASENAME is text, not identity: a path ending in the right program name is
# trivially arranged anywhere. So a value naming a program to execute must also
# RESOLVE into a trusted system location. Measured on this host: the mandated
# compiler resolves within /usr/local/bin and the compiler cache within /usr/bin,
# so confining to these directories admits the documented toolchain while
# refusing a lookalike under the repository, the home directory or a temp dir.
TRUSTED_TOOL_DIRS = ("/usr/bin", "/usr/local/bin", "/bin", "/opt")
UNTRUSTED_PREFIXES = ("/tmp", "/var/tmp", "/dev/shm")

CACHE_VALUE_OK = {
    "CMAKE_BUILD_TYPE": lambda v: v in {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"},
    "CMAKE_CXX_COMPILER": lambda v: trusted_program(v, {"clang++", "g++", "c++"}),
    "CMAKE_C_COMPILER": lambda v: trusted_program(v, {"clang", "gcc", "cc"}),
    "CMAKE_CXX_COMPILER_LAUNCHER": lambda v: trusted_program(v, {"ccache", "sccache"}),
    "CMAKE_C_COMPILER_LAUNCHER": lambda v: trusted_program(v, {"ccache", "sccache"}),
    "CMAKE_EXPORT_COMPILE_COMMANDS": lambda v: v in {"ON", "OFF", "1", "0", "TRUE", "FALSE"},
    "CMAKE_CXX_STANDARD": lambda v: v.isdigit(),
}
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


def repo_root() -> str:
    return os.path.realpath(os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd())


def inside_repo(value: str) -> bool:
    root = repo_root()
    resolved = os.path.realpath(os.path.join(root, os.path.expanduser(value)))
    return resolved == root or resolved.startswith(root + os.sep)


def trusted_program(value: str, names: set) -> bool:
    """A value that names a program to EXECUTE.

    The written name must be one of `names`, and the path it resolves to must sit
    in a trusted system directory -- never under this repository, the home
    directory or a temp dir, each of which is writable by whatever wrote the
    command. Note the resolved basename may legitimately differ from the written
    one (a versioned compiler behind an unversioned symlink), which is why the
    name is checked as WRITTEN and the location as RESOLVED."""
    if os.path.basename(value) not in names:
        return False

    if os.sep not in value:
        found = shutil.which(value)
        if not found:
            return False
        resolved = os.path.realpath(found)
    else:
        resolved = os.path.realpath(os.path.expanduser(value))

    if inside_repo(resolved):
        return False
    home = os.path.realpath(os.path.expanduser("~"))
    if resolved == home or resolved.startswith(home + os.sep):
        return False
    for bad in UNTRUSTED_PREFIXES:
        if resolved == bad or resolved.startswith(bad + os.sep):
            return False
    return any(
        resolved == d or resolved.startswith(d + os.sep) for d in TRUSTED_TOOL_DIRS
    )


def cache_entry_permitted(token: str) -> bool:
    """A -D cache entry: the key must be one this repository uses, and the value
    must have the shape that key is allowed to carry."""
    body = token[2:]
    if "=" not in body:
        return False
    key, value = body.split("=", 1)
    key = key.split(":", 1)[0]  # -DKEY:TYPE=value
    check = CACHE_VALUE_OK.get(key)
    return bool(check) and check(value)


def flag_permitted(token: str, rules, following: str | None) -> bool:
    """Is this flag one the tool may carry, with a value it may carry?

    Deciding membership of a small allowlist is possible; deciding non-membership
    of every dangerous spelling is not. And a permitted flag name does not imply
    a permitted value -- see CACHE_VALUE_OK and PATH_VALUE_FLAGS."""
    head = token.split("=", 1)[0]

    if token.startswith("-D"):
        return "-D" in rules["prefix"] and cache_entry_permitted(token)

    permitted = head in rules["exact"] or any(
        token.startswith(p) for p in rules["prefix"]
    )
    if not permitted:
        return False

    for pf in PATH_VALUE_FLAGS:
        if token == pf:
            # value is the next token; absent means nothing to check here
            return following is None or inside_repo(following)
        if token.startswith(pf) and len(token) > len(pf):
            attached = token[len(pf):].lstrip("=")
            return inside_repo(attached)
    return True


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

    rules = TOOL_FLAGS[name]
    rest = child[1:]
    for i, token in enumerate(rest):
        following = rest[i + 1] if i + 1 < len(rest) else None
        if token.startswith("-"):
            if not flag_permitted(token, rules, following):
                return False
            continue
        if token in LOADER_CHILD_BAD_SUBCOMMANDS:
            return False
        # A positional NAME=value is a variable override, and the build tools
        # read variables that name a program to run (SHELL, CC, LD). Refuse.
        if "=" in token and not token.startswith("-"):
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
