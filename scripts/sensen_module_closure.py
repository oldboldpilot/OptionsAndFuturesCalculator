#!/usr/bin/env python3
"""Compute the sensen modules this engine actually needs, and check CMake agrees.

    python3 scripts/sensen_module_closure.py            # report the closure
    python3 scripts/sensen_module_closure.py --check    # verify sensen_slim matches

Why this exists: `add_subdirectory(sensen)` plus linking sensen's own target
compiles every module interface unit in its FILE_SET, imported or not — 271
translation units producing a 93 MB libsensen.so, for the two modules this repo
imports. Headers do not behave that way, so the cost was invisible.

backend/CMakeLists.txt therefore builds `sensen_slim` from the transitive
closure of what we import. This script is how that list is derived and kept
honest; --check is a build-gate, so the list cannot drift from the source
silently.

Parsing is deliberately shallow — module and import declarations only. It does
not preprocess, so an import inside `#if` is counted whether or not that branch
is live. That errs toward compiling too much, which fails safe: a module we do
not need costs build time, whereas one we miss fails the build outright with
"module 'sensen.x' not found".
"""
import os
import re
import subprocess
import sys
from collections import deque

REPO = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"],
    capture_output=True, text=True, check=True,
).stdout.strip()

SENSEN_SRC = os.path.join(REPO, "backend", "sensen", "src")
CMAKE = os.path.join(REPO, "backend", "CMakeLists.txt")
OUR_SRC = os.path.join(REPO, "backend", "src")

DECL = re.compile(r"^\s*(?:export\s+)?module\s+([A-Za-z_][\w.]*)\s*;")
IMPORT = re.compile(r"^\s*(?:export\s+)?import\s+([A-Za-z_][\w.]*)\s*;")


def scan(root, exts=(".cppm", ".cpp", ".ixx")):
    """-> (module name -> files providing it, module name -> modules it imports)"""
    provides, imports = {}, {}
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if not fn.endswith(exts):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            mod, deps = None, set()
            for line in text.splitlines():
                m = DECL.match(line)
                if m and mod is None:
                    mod = m.group(1)
                m = IMPORT.match(line)
                if m:
                    deps.add(m.group(1))
            if mod:
                provides.setdefault(mod, []).append(path)
                imports.setdefault(mod, set()).update(deps)
    return provides, imports


def roots_from_our_sources():
    """Which sensen modules this repo imports directly."""
    found = set()
    for dirpath, _, files in os.walk(OUR_SRC):
        for fn in files:
            if not fn.endswith((".cpp", ".cppm")):
                continue
            with open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    m = IMPORT.match(line)
                    if m and m.group(1).startswith("sensen."):
                        found.add(m.group(1))
    return found


def main():
    provides, imports = scan(SENSEN_SRC)
    roots = roots_from_our_sources()
    if not roots:
        print("No `import sensen.*;` found in backend/src — nothing to compute.")
        return 1

    seen, missing = set(), set()
    q = deque(roots)
    while q:
        mod = q.popleft()
        if mod in seen:
            continue
        seen.add(mod)
        if mod not in provides:
            if mod.startswith("sensen"):
                missing.add(mod)
            continue
        for dep in imports.get(mod, ()):
            if dep.startswith("sensen") and dep not in seen:
                q.append(dep)

    closure = sorted(seen & set(provides))
    files = sorted({f for m in closure for f in provides[m]})
    rel = [os.path.relpath(f, REPO) for f in files]
    total = len({f for fs in provides.values() for f in fs})

    print(f"imported directly : {', '.join(sorted(roots))}")
    print(f"closure           : {len(closure)} modules, {len(files)} files")
    print(f"sensen total      : {len(provides)} modules, {total} files")
    print(f"avoided           : {total - len(files)} files "
          f"({100 * (total - len(files)) / total:.0f}%)")
    if missing:
        print(f"UNRESOLVED        : {sorted(missing)}")
    print()
    for r in rel:
        print(f"  {r}")

    if "--check" not in sys.argv:
        return 0

    with open(CMAKE, encoding="utf-8") as fh:
        cmake_text = fh.read()
    listed = set(re.findall(r"\$\{SENSEN_SRC\}/([\w.]+\.(?:cppm|cpp))", cmake_text))
    expected = {os.path.basename(f) for f in files}

    print()
    absent = expected - listed
    extra = listed - expected
    if absent:
        print(f"MISSING from sensen_slim : {sorted(absent)}")
    if extra:
        print(f"EXTRA in sensen_slim     : {sorted(extra)}")
    if absent or extra or missing:
        print("\nFAIL: backend/CMakeLists.txt does not match the computed closure.")
        return 1
    print("OK: sensen_slim matches the computed closure exactly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
