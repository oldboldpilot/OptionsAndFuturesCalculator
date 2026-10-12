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

What is NOT checked, listed because an unchecked citation is indistinguishable from a verified one:
  * the qualifier of a qualified name -- `nosuch::realFn` checks `realFn` only;
  * a flag inside a command-line span -- `python3 x.py --nosuchflag` checks the paths only;
  * a glob or a path outside the work tree -- `src/*.cpp`, `/abs/nosuch.h`;
  * a lowercase-hex invented identifier of 7 to 40 characters carrying both a digit and a letter
    (`bad1dea`): indistinguishable from a short sha, so it is a non-fatal REVISION, not MISSING;
  * anything in a file the diff does not list -- a *.md deleted on the branch (correctly), or one
    whose added lines carry no backticked span at all.
A citation that matches no rule is not an error and is not counted in `checked`.

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


LIST_PREFIX = re.compile(r"^\s*(?:[-*+]\s+|>\s*|\d+[.)]\s+)*")
STATUS_PREFIX = re.compile(r"^\*\*[^*]{1,40}\*\*\s*:\s*")


def marks_design(line):
    """A heading that says Design (not built), or a line whose CONTENT STARTS with the bold marker.

    Two measured defects, both 2026-10-12, both of which switched the gate off:
      * inline code spans are stripped first, so a line DOCUMENTING the marker does not APPLY it --
        the section of sensen's docs/agentic/ROADMAP_AND_TESTING.md that documents this checker has a
        table row quoting the marker, and every citation in that section was reported DESIGN;
      * the marker must OPEN the line's content, after list bullets and an optional `**Status**:`
        prefix, because `Never write **Design (not built)** on built code.` is prose about the marker
        and used to exempt the whole file around it -- 7 of 7 citations, including lines above it.
    Both admitted shapes are the ones the docs actually use: `**Design (not built)**` alone, a list
    entry opening with it, `**Status**: **Design (not built)**`, and the phrase in a heading.
    """
    bare = re.sub(r"`[^`]*`", "", line)
    if re.match(r"^#{1,6}\s", bare):
        return DESIGN_MARK in bare
    rest = LIST_PREFIX.sub("", bare)
    marker = f"**{DESIGN_MARK}**"
    return rest.startswith(marker) or STATUS_PREFIX.sub("", rest).startswith(marker)


def git(repo, *args):
    """Run git, and REFUSE by name when it fails: a traceback is not a diagnosis.

    `--base nosuchrev`, an empty repository, unrelated histories and a shallow clone with no
    merge base all reach git with something it cannot answer, and all of them used to end in a
    CalledProcessError traceback at exit status 1 -- the same status a legitimate MISSING uses.
    """
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True)
    if r.returncode != 0:
        first = next((l for l in r.stderr.splitlines() if l.strip()), "")
        sys.exit(f"check_docs_claims: git {' '.join(args)} failed in {repo}: {first}")
    return r.stdout


def toplevel(where):
    """The work tree root. --repo defaults to '.', and from a subdirectory `git ls-files` lists only
    that subdirectory while `git diff --name-only` lists repo-root-relative paths -- so every file
    was skipped and the run printed a clean checked=0 (measured 2026-10-12)."""
    return git(where, "rev-parse", "--show-toplevel").strip()


def default_base(repo):
    """The merge-base with this repository's OWN default branch: master here, main elsewhere."""
    for ref in ("origin/HEAD", "origin/master", "origin/main"):
        if subprocess.run(["git", "-C", repo, "rev-parse", "--verify", "--quiet", ref],
                          capture_output=True).returncode == 0:
            return git(repo, "merge-base", "HEAD", ref).strip()
    sys.exit(f"check_docs_claims: {repo} has no origin/HEAD, origin/master or origin/main; "
             "pass --base REV or --all FILE.md")


def added_lines(repo, base, path):
    """Line numbers (1-based, in the current file) added since base.

    --no-color is not decoration: with color.ui=always git puts ANSI escapes ahead of every @@,
    the hunk regex matches nothing, and the run reports a clean checked=0 over a document it
    never read (measured 2026-10-12).
    """
    out = git(repo, "diff", "--no-color", "-U0", base, "--", path)
    if re.search(r"^Binary files .* differ$", out, re.M):
        sys.exit(f"check_docs_claims: {path} has no readable diff (binary, or a -diff attribute); "
                 "check it with --all or make it text")
    lines = set()
    for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", out, re.M):
        start, count = int(m.group(1)), int(m.group(2) or "1")
        lines.update(range(start, start + count))
    return lines


def fenced(text_lines):
    """Which lines sit inside a ``` or ~~~ fence: a `# comment` in a shell block is not a heading."""
    out, open_mark = [], None
    for line in text_lines:
        m = re.match(r"^\s*(```+|~~~+)", line)
        if open_mark is None and m:
            open_mark, inside = m.group(1)[:3], True
        elif open_mark is not None and m and m.group(1).startswith(open_mark):
            open_mark, inside = None, True
        else:
            inside = open_mark is not None
        out.append(inside)
    return out


def design_lines(text_lines):
    """Lines whose heading section, or top-level list entry, is marked Design (not built)."""
    n = len(text_lines)
    in_fence = fenced(text_lines)
    text_lines = ["" if in_fence[i] else l for i, l in enumerate(text_lines)]
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
    if "/" not in word or not re.fullmatch(r"\.?/?[\w.@+-]+(/[\w.@+-]+)*/?(:\d+)*", word):
        return False
    bare = os.path.normpath(word.split(":")[0]).rstrip("/")
    # A trailing slash is path syntax and nothing else, so `nosuchdir/` is checked rather than
    # skipped; prose does not end a word that way, where `and/or` has no slash to end on.
    return (word.rstrip().endswith("/") or bare.split("/")[0] in tops
            or re.search(r"\.[A-Za-z][\w+-]{0,7}$", bare) is not None)


def atoms(span, tops):
    """The checkable names inside one backticked span (empty: not a code citation)."""
    t = span.strip()
    if re.fullmatch(r"[A-Za-z_]\w*(::[\w~]+)+(\(\))?", t):
        return [t.split("::")[-1].rstrip("()")]
    if re.fullmatch(r"[0-9a-f]{7,40}", t) and re.search(r"\d", t) and re.search(r"[a-f]", t):
        return [("rev", t)]
    if re.fullmatch(r"[A-Za-z_]\w*\(\)", t) or re.fullmatch(r"[A-Za-z_]\w{3,}", t):
        return [t.rstrip("()")]
    if looks_like_path(t, tops):
        return [("path", os.path.normpath(t.split(":")[0]))]
    if re.fullmatch(r"--[\w-]+(=\S*)?", t):
        return [t.split("=")[0]]
    if t.startswith("sen ") or t.startswith("sensen_serve "):
        out = [w for w in t.split()[1:3] if re.fullmatch(r"[a-z][\w-]*", w)]
        out += [w.split("=")[0] for w in t.split() if w.startswith("--")]
        return out
    if re.match(r"(python3?|bash|sh|git|ninja|ctest|cmake)\s", t):
        return [("path", os.path.normpath(w.split(":")[0])) for w in t.split()[1:] if looks_like_path(w, tops)]
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
            # The self-test SPELLS invented names on purpose, so reading it would answer a doc's
            # citation of one with the gate's own test data: measured 2026-10-12, four of five
            # invented names passed in every repository because the fixture names them. A gate
            # may not be its own evidence. This script stays IN the corpus and deliberately
            # spells no invented name, so its own identifiers remain citable from a document.
            if os.path.basename(f) == "test_check_docs_claims.sh":
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
    repo = toplevel(a.repo)
    base = None
    if not a.all:
        base = a.base or default_base(repo)
    # -z, because --name-only QUOTES a non-ASCII name ("docs/caf\303\251.md") and the quoted
    # string then matches no file on disk and was skipped in silence. icase and the two other
    # Markdown extensions, because a *.md pathspec selects neither README.MD nor a .mdx.
    specs = [":(icase)*.md", ":(icase)*.markdown", ":(icase)*.mdx"]
    whole = set()
    files = a.files
    if not files:
        files = [f for f in git(repo, "diff", "--no-color", "--name-only", "-z", base, "--", *specs).split("\0") if f]
        # An UNTRACKED document is not in any diff, so a new doc in a lane -- the commonest case
        # there is -- was never checked. Every line of it is new, so it is checked whole.
        whole = {f for f in git(repo, "ls-files", "--others", "--exclude-standard", "-z", "--", *specs).split("\0") if f}
        files += sorted(whole)
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
        want = set(range(1, len(lines) + 1)) if (a.all or f in whole) else added_lines(repo, base, f)
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
