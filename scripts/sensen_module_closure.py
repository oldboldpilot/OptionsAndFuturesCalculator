#!/usr/bin/env python3
"""Check this engine's sensen_slim against the modules backend/src imports.

    python3 scripts/sensen_module_closure.py            # report the closure and every file built
    python3 scripts/sensen_module_closure.py --check    # same checks, exit status for a gate

The module list is no longer in this repository. It is sensen's, as named groups
(backend/sensen/cmake/SensenSlim.cmake), checked by backend/sensen/tools/sensen_slim_closure.py,
so that a second service builds the same slim library from the same list instead of copying
~350 lines of it. This script is the thin call that points that checker at THIS engine's
sources: the roots are the `import sensen.*;` lines under backend/src, and the engine takes
every group.

Run it after every sensen bump. A module a listed one starts importing is a build break that
names the module and never the list ("module 'sensen.x' not found"); this names it first, with
the group it belongs in.
"""
import os
import subprocess
import sys

REPO = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"],
    capture_output=True, text=True, check=True,
).stdout.strip()

TOOL = os.path.join(REPO, "backend", "sensen", "tools", "sensen_slim_closure.py")
OUR_SRC = os.path.join(REPO, "backend", "src")


def main() -> int:
    if not os.path.isfile(TOOL):
        print(f"{TOOL} is missing: the backend/sensen submodule is not checked out at a revision "
              "that carries the slim groups.", file=sys.stderr)
        return 2
    mode = ["--check"] if "--check" in sys.argv[1:] else ["--report"]
    rest = [a for a in sys.argv[1:] if a != "--check"]
    return subprocess.run(
        [sys.executable, TOOL, *mode, "--consumer-src", OUR_SRC, "--groups", "ALL", *rest]
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
