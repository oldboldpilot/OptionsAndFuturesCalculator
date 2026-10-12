#!/usr/bin/env python3
"""check_docs_claims.py -- every code name a doc cites must exist in the code.

Reads the Markdown lines ADDED since a base revision (or whole files with --all), takes every
`backticked` span that names code -- an identifier, a qualified name, a flag, an environment
variable, a repository path, a quoted message, a `sen` command line -- and looks it up in the
tracked files outside docs/ and *.md. The tree is read ONCE and every lookup is answered in
memory, so a doc with a thousand citations costs one pass over the repository.

Each citation lands in one bucket:
  missing   -- not found in the code. Exit status 1 while any remain.
  design    -- not found, but inside a heading section or top-level list entry marked "Design (not built)"
               (the phrase in a heading, or the bold status marker **Design (not built)**):
               a design may name things that do not exist yet. Reported, never fatal.
  fragment  -- the span starts or ends with prose punctuation or spaces: two backticks paired
               across a sentence. A Markdown defect, reported, never fatal.
  revision  -- a git revision (7-40 hex digits, at least one of them a digit) this repository
               cannot resolve: a doc legitimately cites another repository's commit. Reported,
               never fatal. One it CAN resolve is found, so a mistyped local sha is reported.

This script is shared, and the copy of record is sensen's tools/check_docs_claims.py. Every other
repository that carries it (OptionsAndFuturesCalculator, ThinButQuickWebFramework,
mortgage-nest-egg, fastestjsoninthewest, cpp23-logger) carries a BYTE-IDENTICAL copy: change
sensen's and copy it out, never edit a copy in place. Nothing in a consuming repository can
compare the two today, so that rule is kept by hand; tools/test_check_docs_claims.sh is the only
local proof that the copy where it sits works. Nothing here is sensen-specific: the default base
is this repository's own default branch and the path prefixes are its own tracked directories.

Limits: the lookup is textual (a name that exists anywhere outside docs/ counts, whatever its
scope), so this proves a name EXISTS, not that the doc says the right thing about it -- a
semantic fact-check is still needed for that. Kernel and external-tool names (`TASK_RUNNING`,
`conmon`) and another repository's code names come back missing; spell them without backticks or
justify them in the lane report.

Usage: tools/check_docs_claims.py [--repo DIR] [--base REV | --all] [--json] [FILE.md ...]
       default base: merge-base of HEAD and this repository's default branch (origin/HEAD, else
       origin/master, else origin/main); default files: every changed *.md.
"""
import argparse
import json
import os
import re
import subprocess
import sys

DESIGN_MARK = "Design (not built)"


def marks_design(line):
    """A heading that says Design (not built), or the bold status marker -- not a sentence that mentions the phrase.

    Inline code spans are stripped first, so a line DOCUMENTING the marker does not APPLY it. Measured
    2026-10-12: the section of sensen's docs/agentic/ROADMAP_AND_TESTING.md that documents this checker
    has a table row quoting the marker, which marked that whole section as a design and made every
    citation in the section non-fatal -- the gate switched off exactly where it is explained.
    """
    bare = re.sub(r"`[^`]*`", "", line)
    return (re.match(r"^#{1,6}\s", bare) is not None and DESIGN_MARK in bare) or f"**{DESIGN_MARK}**" in bare


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True, check=True).stdout


def default_base(repo):
    """The merge-base with this repository's OWN default branch: master here, main elsewhere."""
    for ref in ("origin/HEAD", "origin/master", "origin/main"):
        if subprocess.run(["git", "-C", repo, "rev-parse", "--verify", "--quiet", ref],
                          capture_output=True).returncode == 0:
            return git(repo, "merge-base", "HEAD", ref).strip()
    sys.exit(f"check_docs_claims: {repo} has no origin/HEAD, origin/master or origin/main; "
             "pass --base REV or --all FILE.md")


def added_lines(repo, base, path):
    """Line numbers (1-based, in the current file) added since base."""
    out = git(repo, "diff", "-U0", base, "--", path)
    lines = set()
    for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", out, re.M):
        start, count = int(m.group(1)), int(m.group(2) or "1")
        lines.update(range(start, start + count))
    return lines


def design_lines(text_lines):
    """Lines whose heading section, or top-level list entry, is marked Design (not built)."""
    n = len(text_lines)
    design = [False] * n
    # Heading sections: a marked heading marks everything under it until a heading of the same or higher level.
    stack = []  # (level, marked)
    section_marked = [False] * n
    for i, line in enumerate(text_lines):
        h = re.match(r"^(#{1,6})\s", line)
        if h:
            level = len(h.group(1))
            while stack and stack[-1][0] >= level:
                stack.pop()
            stack.append((level, marks_design(line)))
        section_marked[i] = any(marked for _, marked in stack)
    # A heading section whose own body (before the next heading) carries the mark is marked too.
    start = 0
    for i in range(n + 1):
        if i == n or re.match(r"^#{1,6}\s", text_lines[i]):
            if any(marks_design(text_lines[j]) for j in range(start, i)):
                for j in range(start, i):
                    section_marked[j] = True
            start = i
    # Top-level list entries (INDEX-style): "- " at column 0 up to the next one or a heading.
    start = None
    for i in range(n + 1):
        boundary = i == n or re.match(r"^(- |#{1,6}\s)", text_lines[i])
        if boundary:
            if start is not None and any(marks_design(text_lines[j]) for j in range(start, i)):
                for j in range(start, i):
                    design[j] = True
            start = i if i < n and text_lines[i].startswith("- ") else None
    return [d or s for d, s in zip(design, section_marked)]


def looks_like_path(word, tops):
    """A path-shaped word whose first segment is a tracked top-level directory, or that names a file.

    Derived rather than listed: a hardcoded prefix list is right for one repository and silently
    skips every citation outside it -- an invented path in an unlisted directory then reads as a
    pass, which is the direction that hides. `and/or` is neither a tracked directory nor a
    filename and stays unchecked.
    """
    if "/" not in word or not re.fullmatch(r"[\w.@+-]+(/[\w.@+-]+)+/?(:\d+)?", word):
        return False
    bare = word.split(":")[0].rstrip("/")
    return word.split("/")[0] in tops or re.search(r"\.[A-Za-z][\w+-]{0,7}$", bare) is not None


def atoms(span, tops):
    """The checkable names inside one backticked span (empty: not a code citation)."""
    t = span.strip()
    if re.fullmatch(r"[A-Za-z_]\w*(::[\w~]+)+(\(\))?", t):
        return [t.split("::")[-1].rstrip("()")]
    if re.fullmatch(r"[0-9a-f]{7,40}", t) and re.search(r"\d", t):
        return [("rev", t)]
    if re.fullmatch(r"[A-Za-z_]\w*\(\)", t) or re.fullmatch(r"[A-Za-z_]\w{3,}", t):
        return [t.rstrip("()")]
    if looks_like_path(t, tops):
        return [("path", t.split(":")[0])]
    if re.fullmatch(r"--[\w-]+(=\S*)?", t):
        return [t.split("=")[0]]
    if t.startswith("sen ") or t.startswith("sensen_serve "):
        out = [w for w in t.split()[1:3] if re.fullmatch(r"[a-z][\w-]*", w)]
        out += [w.split("=")[0] for w in t.split() if w.startswith("--")]
        return out
    if re.match(r"(python3?|bash|sh|git|ninja|ctest|cmake)\s", t):
        return [("path", w.split(":")[0]) for w in t.split()[1:] if looks_like_path(w, tops)]
    if " " in t and len(t) > 18 and not re.search(r"[|<>$;&]", t):
        return [("text", t)]
    return []


def is_fragment(span):
    return bool(re.match(r"^(\s|[,;:)\]]|\.\s)", span) or re.search(r"[\s(\[]$", span))


class Tree:
    """Every tracked file outside docs/ and *.md, read once."""

    def __init__(self, repo):
        files = [f for f in git(repo, "ls-files", "-z").split("\0") if f]
        self.repo = repo
        self.paths = set(files)
        self.dirs = {os.path.dirname(f) for f in files}
        self.tops = {f.split("/")[0] for f in files if "/" in f}
        chunks = []
        for f in files:
            if f.startswith("docs/") or f.endswith(".md"):
                continue
            try:
                with open(os.path.join(repo, f), "rb") as fh:
                    b = fh.read()
            except OSError:
                continue
            if b"\0" in b[:8192]:
                continue
            chunks.append(b.decode("utf-8", "replace"))
        self.text = "\n".join(chunks)
        self.words = set(re.findall(r"\w+", self.text))

    def has(self, atom):
        if isinstance(atom, tuple):
            kind, value = atom
            if kind == "path":
                v = value.rstrip("/")
                return v in self.paths or v in self.dirs or any(p.startswith(v + "/") for p in self.paths)
            if kind == "rev":
                # A word of the code first: an identifier spelled only in hex letters is not a revision.
                return value in self.words or subprocess.run(
                    ["git", "-C", self.repo, "cat-file", "-e", value + "^{object}"],
                    capture_output=True).returncode == 0
            return value[:60] in self.text
        if re.fullmatch(r"\w+", atom):
            return atom in self.words
        # Non-identifier atoms (flags, dotted names): whole-token match.
        return re.search(r"(?<![\w-])" + re.escape(atom) + r"(?![\w-])", self.text) is not None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=".")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--base", help="check lines added since this revision")
    g.add_argument("--all", action="store_true", help="check every line of the given files")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    repo = a.repo
    base = None
    if not a.all:
        base = a.base or default_base(repo)
    files = a.files or [f for f in git(repo, "diff", "--name-only", base, "--", "*.md").split("\n") if f]
    if a.all and not a.files:
        sys.exit("check_docs_claims: --all needs the files to check")
    # A file NAMED on the command line and not there is an error: silently skipping it reports
    # "checked=0 missing=0" and exit 0, which reads as a pass over a doc nothing looked at. A
    # file the diff lists and that is gone was deleted on the branch, and is skipped below.
    gone = [f for f in a.files if not os.path.exists(os.path.join(repo, f))]
    if gone:
        sys.exit("check_docs_claims: no such file: " + ", ".join(gone))

    tree = Tree(repo)
    found = {"missing": [], "design": [], "fragment": [], "revision": []}
    checked = 0
    for f in files:
        path = os.path.join(repo, f)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.read().split("\n")
        want = set(range(1, len(lines) + 1)) if a.all else added_lines(repo, base, f)
        design = design_lines(lines)
        for ln in sorted(want):
            if ln > len(lines):
                continue
            for span in re.findall(r"`([^`\n]{2,160})`", lines[ln - 1]):
                if is_fragment(span):
                    found["fragment"].append((f, ln, span, span))
                    continue
                for atom in atoms(span, tree.tops):
                    checked += 1
                    if tree.has(atom):
                        continue
                    name = atom if isinstance(atom, str) else atom[1]
                    kind = atom[0] if isinstance(atom, tuple) else ""
                    bucket = "design" if design[ln - 1] else ("revision" if kind == "rev" else "missing")
                    found[bucket].append((f, ln, span, name))

    if a.json:
        print(json.dumps({"checked": checked,
                          **{k: [dict(file=f, line=l, span=s, atom=n) for f, l, s, n in v] for k, v in found.items()}},
                         indent=1))
    else:
        print(f"checked={checked} missing={len(found['missing'])} design={len(found['design'])}"
              f" fragment={len(found['fragment'])} revision={len(found['revision'])}")
        for kind in ("missing", "design", "fragment", "revision"):
            for f, l, s, n in found[kind]:
                print(f"{kind.upper()} {f}:{l}: `{s}`" + (f"  (atom: {n})" if n != s else ""))
    return 1 if found["missing"] else 0


if __name__ == "__main__":
    sys.exit(main())
