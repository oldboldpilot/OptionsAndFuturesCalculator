#!/usr/bin/env bash
#
# Adversarial code review gate.
#
# Phase 1 runs real static checks against the actual diff (raw `new` in
# changed C++ files, -ffast-math, unmasked secrets -- repo-wide for the
# latter two, changed-files-only for the former; see the comments on each
# check for why).
#
# Phase 2 sends that diff, plus config/cpp_details.txt and
# config/update_policy.txt (both binding on this repo), to three
# independently-installed CLI agents (agy, claude, cursor-agent) and asks
# each for a real APPROVE/REJECT verdict. A CLI that is missing, errors,
# times out, or returns unparseable output does NOT count as an approval --
# it is simply excluded from N. The gate requires at least 2 real approvals
# out of however many reviewers actually responded.
#
# Phase 2b runs a fourth, DISTINCT reviewer alongside the tri-CLI vote: the
# `claude` CLI with real Serena MCP tools enabled against the actual
# checked-out repository, not just the diff text -- blast radius via
# find_referencing_symbols, real clangd/tsserver diagnostics, and
# declaration/implementation drift. It does not supply one of the 2 required
# tri-CLI approvals; it can only ADD a tool-verified rejection, and only
# when it actually ran (unavailable/timeout/unparseable = no veto, not a
# silent pass and not a silent fail).
#
# Usage:
#   scripts/code_review_adversarial.sh [<commit-ish> | --staged | --unstaged]
#   (default target: HEAD)
#
# @author Olumuyiwa Oluwasanmi

set -euo pipefail

echo "============================================================"
echo "  ADVERSARIAL CODE REVIEW GATE"
echo "  Policies: config/cpp_details.txt & config/update_policy.txt"
echo "============================================================"

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

CPP_POLICY="${REPO_ROOT}/config/cpp_details.txt"
UPDATE_POLICY="${REPO_ROOT}/config/update_policy.txt"

AGY_CLI="${AGY_CLI:-/home/muyiwa/.local/bin/agy}"
CLAUDE_CLI="${CLAUDE_CLI:-/home/muyiwa/.local/bin/claude}"
CURSOR_CLI="${CURSOR_CLI:-/home/muyiwa/.local/bin/cursor-agent}"

# Per-reviewer wall-clock budget. Reviewers run in parallel (Phase 2), so the
# wall-clock cost of this gate is ~one reviewer's timeout, not the sum of
# three -- that is what keeps a couple of minutes realistic.
CLI_TIMEOUT_S="${CLI_TIMEOUT_S:-150}"
# Serena pays a one-time uvx/LSP cold-start cost the text-only CLIs don't, so
# it gets its own, slightly larger budget rather than dragging the whole
# gate's timeout up for everyone.
SERENA_TIMEOUT_S="${SERENA_TIMEOUT_S:-170}"

# Redact anything that looks like a credential-bearing URL before it is ever
# echoed, per config/update_policy.txt's secrets section.
redact() { sed -E 's#//[^@]*@#//<redacted>@#g'; }

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/code_review_adversarial.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

# ------------------------------------------------------------------------
# 1. Resolve a REAL target and produce the actual diff + changed-file list.
#    A caller that passes nothing gets HEAD, not silence -- the old script
#    accepted $1 and never read it, so every caller's commit SHA was
#    discarded and the "review" always ran against nothing in particular.
# ------------------------------------------------------------------------
TARGET="${1:-HEAD}"

case "$TARGET" in
  --staged)
    DIFF_DESC="staged changes (git diff --cached)"
    git diff --cached -- . > "$WORKDIR/diff.patch"
    git diff --cached --name-only -- . > "$WORKDIR/files.txt"
    ;;
  --unstaged)
    DIFF_DESC="unstaged working-tree changes (git diff)"
    git diff -- . > "$WORKDIR/diff.patch"
    git diff --name-only -- . > "$WORKDIR/files.txt"
    ;;
  *)
    if ! git rev-parse --verify --quiet "${TARGET}^{commit}" > /dev/null; then
        echo "ADVERSARIAL REVIEW: '$TARGET' is not a valid commit-ish, and not --staged/--unstaged." >&2
        echo "Refusing to run vacuously -- give it a real target." >&2
        exit 1
    fi
    RESOLVED="$(git rev-parse "$TARGET")"
    DIFF_DESC="commit ${TARGET} (${RESOLVED})"
    git show --format='' --patch "$TARGET" > "$WORKDIR/diff.patch"
    git diff-tree --no-commit-id --name-only -r "$TARGET" > "$WORKDIR/files.txt"
    ;;
esac

if [[ ! -s "$WORKDIR/diff.patch" ]]; then
    echo "ADVERSARIAL REVIEW: no changes found for $DIFF_DESC -- refusing to pass vacuously." >&2
    exit 1
fi

mapfile -t CHANGED_FILES < "$WORKDIR/files.txt"

echo "Target: $DIFF_DESC"
echo "Changed files: ${#CHANGED_FILES[@]}"
echo ""

# ------------------------------------------------------------------------
# Phase 1: Static Policy & Safety Audit
# ------------------------------------------------------------------------
echo "Phase 1: Static Policy & Safety Audit"
ERRORS=0

# Check 1: Raw pointer allocation.
#
# The previous version of this check scanned "${REPO_ROOT}/src", which does
# not exist in this repo -- the code lives in backend/src, backend/tests and
# frontend/src -- so it never examined a single file. Pointed at the real
# roots, the naive `\bnew\s+[A-Za-z0-9_]+` pattern fires 44 times across
# backend/src+tests+frontend/src, and 40 of those are prose ("a new SIMD
# variant", "brand-new key") inside comments, not allocations. The pattern
# below requires `new Type(` / `new Type[` -- an actual construction or
# array-new -- and strips `//` line comments first (not /* */ block
# comments; that is a known gap, noted below). That cuts the repo-wide count
# from 44 to 4, all genuine `new` expressions.
#
# Gated on CHANGED FILES ONLY, deliberately: a pre-commit gate should judge
# what is being committed, not flag pre-existing code the diff never
# touches. The 4 full-tree hits today (assistant_service.cpp x2,
# mortgage_assistant_service.cpp, calculator_service.cpp) are all
# pre-existing, deliberate, and already commented as such (one immediately
# wrapped in unique_ptr around a private constructor, one intentionally
# leaked for a short-lived test process) -- gating on them would block every
# future push for a decision already made and documented. If a future
# commit repeats that pattern in code it actually changes, this check will
# now catch it; the full-tree count below is informational only.
CPP_EXT_RE='\.(cpp|cc|cxx|h|hpp|hh|cppm|ixx)$'

scan_raw_new() {
    # $@: absolute file paths to scan.
    [[ $# -eq 0 ]] && return 0
    awk '
      {
        line = $0
        sub(/\/\/.*/, "", line)  # strip line comments (not /* */ block comments -- known gap)
        if (line ~ /new[ \t]+[A-Za-z_][A-Za-z0-9_:<>]*[ \t]*[(\[]/) {
          print FILENAME ":" FNR ": " $0
        }
      }
    ' "$@" 2>/dev/null | grep -v NOLINT || true
}

CHANGED_CPP_FILES=()
for f in "${CHANGED_FILES[@]}"; do
    case "$f" in
        backend/src/*|backend/tests/*|frontend/src/*)
            if [[ "$f" =~ $CPP_EXT_RE ]] && [[ -f "$REPO_ROOT/$f" ]]; then
                CHANGED_CPP_FILES+=("$REPO_ROOT/$f")
            fi
            ;;
    esac
done

RAW_NEW="$(scan_raw_new "${CHANGED_CPP_FILES[@]+"${CHANGED_CPP_FILES[@]}"}")"
if [[ -n "$RAW_NEW" ]]; then
    echo "  [REJECT] Check 1: raw 'new' allocation in changed code:"
    echo "$RAW_NEW" | sed 's/^/    /'
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] Check 1: no raw 'new' allocation in changed C++ files (${#CHANGED_CPP_FILES[@]} scanned)."
fi

FULLTREE_FILES=()
while IFS= read -r f; do
    [[ -n "$f" ]] && FULLTREE_FILES+=("$REPO_ROOT/$f")
done < <(git ls-files -- backend/src backend/tests frontend/src | grep -E "$CPP_EXT_RE" || true)
FULLTREE_RAW_NEW="$(scan_raw_new "${FULLTREE_FILES[@]+"${FULLTREE_FILES[@]}"}")"
FULLTREE_COUNT=0
[[ -n "$FULLTREE_RAW_NEW" ]] && FULLTREE_COUNT=$(echo "$FULLTREE_RAW_NEW" | wc -l)
echo "  [INFO] Full-tree scan (informational, NOT gating): ${FULLTREE_COUNT} raw 'new' site(s) exist"
echo "         repo-wide in backend/src, backend/tests, frontend/src (pre-existing, out of this diff's scope)."

# Check 2: Fast-math flag prohibition.
#
# Scans TRACKED files only (`git ls-files`), not the working tree. A
# pre-commit gate should judge what is being committed; generated build
# output is neither committed nor authored. The previous `--exclude-dir=build`
# matched only a directory named exactly "build", so
# `backend/build-int8/build.ninja` -- CMake's own generated output,
# gitignored and untracked -- tripped the gate and blocked every push with a
# violation no diff could ever fix. `git ls-files` excludes any ignored
# build dir by construction, whatever it is named.
# Documentation is excluded because the rule is about a compiler flag, not
# the string. Prose that says "never pass -ffast-math" is the policy being
# followed, not broken; docs/superpowers/specs/2026-07-26-opc-parity-design.md
# had already written this false positive up as a known defect of this very
# check.
FAST_MATH=$(git -C "${REPO_ROOT}" ls-files -z | grep -zvE '\.(md|txt)$' | grep -zvE '^docs/' | xargs -0 grep -nE "\-f""fast-math" 2>/dev/null | grep -v "cpp_details.txt" | grep -v "code_policy_check.sh" | grep -v "code_review_adversarial.sh" | grep -v "yaml" | grep -v "PRD_" | grep -v "backend/sensen" | grep -v "backend/StochasticGraphExecutionEngine" | grep -v "external/SGEE" | grep -v "backend/cpp23-logger" | grep -v "backend/nanobind" || true)
if [[ -n "$FAST_MATH" ]]; then
    echo "  [REJECT] Check 2: non-associative -f""fast-math flag found:"
    echo "$FAST_MATH" | sed 's/^/    /'
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] Check 2: no -f""fast-math flag in tracked files."
fi

# Check 3: Secret keys in code / environment.
# Tracked files only, for the same reason as Check 2 -- and additionally
# because config/.env is gitignored by design and holds real credentials, so
# scanning the working tree would report a "violation" that must never be
# fixed by removing it.
SECRETS=$(git -C "${REPO_ROOT}" ls-files -z | xargs -0 grep -nE "(sk-[A-Za-z0-9]{32,}|ghp_[A-Za-z0-9]{30,}|AKIA[0-9A-Z]{16})" 2>/dev/null | grep -v ".gitignore" || true)
if [[ -n "$SECRETS" ]]; then
    echo "  [REJECT] Check 3: unmasked secret keys detected:"
    echo "$SECRETS" | redact | sed 's/^/    /'
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] Check 3: no unmasked secret keys in tracked files."
fi

echo ""
if [[ $ERRORS -gt 0 ]]; then
    echo "Phase 1 FAILED: $ERRORS static check(s) rejected this diff."
    echo ""
    echo "============================================================"
    echo "  RESULT: Phase 1 static audit rejected this diff -- skipping"
    echo "  Phase 2 (no point spending CLI time reviewing code that is"
    echo "  already refused)."
    echo "============================================================"
    echo "REJECTED: Phase 1 static policy audit found $ERRORS violation(s)."
    exit 1
else
    echo "Phase 1 passed: all static checks clean."
fi
echo ""

# ------------------------------------------------------------------------
# Phase 2: Real Tri-CLI Adversarial Review
# ------------------------------------------------------------------------
echo "Phase 2: Tri-CLI Adversarial Review (requires >= 2 real APPROVE verdicts)"

# This sandbox's execve() rejects a single argument once it crosses ~128KiB
# (confirmed by bisection: 131000 B succeeds, 132000 B fails -- well under
# the 2 MiB `getconf ARG_MAX` reports, so something in this environment
# enforces a tighter limit than the kernel's advertised one). agy and
# cursor-agent take their prompt as a positional argument with no
# file/stdin alternative, so the WHOLE prompt (policy text + diff) is
# capped well under that line, not just the diff. Policy text is fixed size
# (~34 KB); the diff gets whatever is left.
PROMPT_BUDGET_BYTES=110000
POLICY_BYTES=$(( $(wc -c < "$CPP_POLICY") + $(wc -c < "$UPDATE_POLICY") ))
FILES_LIST_BYTES=$(wc -c < "$WORKDIR/files.txt")
FIXED_OVERHEAD_BYTES=2500   # instructions + headers + verdict schema text
DIFF_BUDGET=$(( PROMPT_BUDGET_BYTES - POLICY_BYTES - FILES_LIST_BYTES - FIXED_OVERHEAD_BYTES ))
if [[ $DIFF_BUDGET -lt 4000 ]]; then
    DIFF_BUDGET=4000
fi

DIFF_BYTES=$(wc -c < "$WORKDIR/diff.patch")
if [[ $DIFF_BYTES -gt $DIFF_BUDGET ]]; then
    head -c "$DIFF_BUDGET" "$WORKDIR/diff.patch" > "$WORKDIR/diff_capped.patch"
    {
        echo ""
        echo "... [diff truncated at ${DIFF_BUDGET} of ${DIFF_BYTES} bytes to stay under this reviewer's prompt limit -- review the full diff separately for anything beyond this point] ..."
    } >> "$WORKDIR/diff_capped.patch"
    DIFF_FOR_PROMPT="$WORKDIR/diff_capped.patch"
    echo "  [INFO] Diff is ${DIFF_BYTES} B; truncated to ${DIFF_BUDGET} B for the CLI reviewers (policy text is ${POLICY_BYTES} B)."
else
    DIFF_FOR_PROMPT="$WORKDIR/diff.patch"
fi

PROMPT_FILE="$WORKDIR/prompt.txt"
{
    echo "You are a rigorous but practical code reviewer gating a push to production"
    echo "for a live trading-tools website (Options & Futures Profit Calculator)."
    echo "You are reviewing: $DIFF_DESC"
    echo ""
    echo "The two files below are BINDING project policy, not suggestions."
    echo ""
    echo "=== config/cpp_details.txt ==="
    cat "$CPP_POLICY"
    echo ""
    echo "=== config/update_policy.txt ==="
    cat "$UPDATE_POLICY"
    echo ""
    echo "=== Changed files ==="
    cat "$WORKDIR/files.txt"
    echo ""
    echo "=== Diff ==="
    cat "$DIFF_FOR_PROMPT"
    echo ""
    echo "REJECT only for a clear violation of the two policy documents above, or an"
    echo "unambiguous, serious defect (crash, memory-safety violation, data race,"
    echo "security hole, or a correctness bug that changes program output) that is"
    echo "actually introduced or made worse BY THIS DIFF. Do not reject for:"
    echo "pre-existing conditions this diff does not touch, stylistic nitpicks,"
    echo "missing documentation for something already documented elsewhere, or"
    echo "hypothetical inputs outside what this diff's own tests/callers exercise."
    echo "If you have concerns that do not rise to that bar, list them as"
    echo "non-blocking caveats and still APPROVE."
    echo ""
    echo 'Respond with EXACTLY one JSON object and nothing else (no markdown fences,'
    echo 'no preamble, no trailing commentary):'
    echo '{"verdict":"APPROVE"|"REJECT","reasons":["..."]}'
} > "$PROMPT_FILE"

# ---- Verdict extraction -------------------------------------------------
# Pull the first {...} object containing a "verdict" key out of whatever the
# CLI printed (some wrap it in prose or code fences despite instructions).
# jq validates it really is well-formed JSON with the right shape; a CLI
# that returns anything else is UNPARSEABLE and therefore not an approval.
extract_verdict() {
    # $1: raw output file. Prints "APPROVE", "REJECT", or "UNPARSEABLE" on
    # stdout; reasons (if any) on stderr-safe fd 3 is overkill here, so
    # print "VERDICT|reasons_json" on stdout, one line.
    local raw="$1"
    local json
    json="$(grep -ozE '\{[^{}]*"verdict"[^{}]*\}' "$raw" 2>/dev/null | tr -d '\0' | head -c 20000 || true)"
    if [[ -z "$json" ]]; then
        echo "UNPARSEABLE|[]"
        return
    fi
    local verdict reasons
    verdict="$(echo "$json" | jq -r '.verdict // empty' 2>/dev/null || true)"
    reasons="$(echo "$json" | jq -c '.reasons // []' 2>/dev/null || true)"
    if [[ "$verdict" != "APPROVE" && "$verdict" != "REJECT" ]]; then
        echo "UNPARSEABLE|[]"
        return
    fi
    echo "${verdict}|${reasons:-[]}"
}

# ---- Per-CLI invocation --------------------------------------------------
# Each function fails closed: a nonzero return, a timeout, or unparseable
# output means "did not run" and the caller must not count it as tested,
# let alone approved.

run_claude() {
    local outfile="$1"
    [[ -x "$CLAUDE_CLI" ]] || return 1
    timeout "${CLI_TIMEOUT_S}s" "$CLAUDE_CLI" -p \
        --permission-mode plan \
        --tools "" \
        --output-format text \
        < "$PROMPT_FILE" > "$outfile" 2>&1
}

run_agy() {
    local outfile="$1"
    [[ -x "$AGY_CLI" ]] || return 1
    # agy's --print does not read stdin as a prompt (confirmed empirically:
    # it falls back to answering an unrelated question about whichever flag
    # was last on the command line). It also mis-parses when --mode or
    # --output-format is combined with --print and a long positional prompt
    # (confirmed empirically: same fallback behaviour). Bare `--print
    # "<prompt>"` is the only invocation that reliably consumes the prompt.
    timeout "${CLI_TIMEOUT_S}s" "$AGY_CLI" --print "$(cat "$PROMPT_FILE")" > "$outfile" 2>&1
}

run_cursor() {
    local outfile="$1"
    [[ -x "$CURSOR_CLI" ]] || return 1
    # --mode plan: read-only/planning, no edits -- correct for a reviewer
    # that should never touch the working tree.
    timeout "${CLI_TIMEOUT_S}s" "$CURSOR_CLI" -p \
        --mode plan \
        --output-format text \
        "$(cat "$PROMPT_FILE")" > "$outfile" 2>&1
}

# ------------------------------------------------------------------------
# Phase 2b: Serena semantic review -- a DISTINCT reviewer, not one of the
# tri-CLI vote pool above.
#
# A shell script cannot call MCP tools directly; there is no stdio/API
# surface for that from bash. What genuinely works, confirmed empirically
# against this repo: the `claude` CLI has serena configured as a user-level
# plugin (`claude mcp list` shows `plugin:serena:serena ... Connected`), and
# a non-interactive `claude -p` run with a `--mcp-config` pointing at
# serena's stdio server DOES autonomously call real serena tools
# (mcp__serena__get_symbols_overview, find_referencing_symbols,
# get_diagnostics_for_file, etc.) against the actual checked-out repository
# -- not a hallucination of them. That is the "invoke the Claude CLI with a
# Serena-enabled prompt" shape this gate uses.
#
# What it actually contributes that a diff-only reviewer cannot:
#   - Blast radius: find_referencing_symbols lists real callers of a
#     changed symbol. Confirmed working end-to-end on
#     frontend/src/store/useTreePricerStore.ts -- it correctly found all 3
#     consuming files, the exact lines, and which fields each destructures.
#   - Diagnostics: get_diagnostics_for_file surfaces real clangd/tsserver
#     errors and warnings a diff can't show.
#
# What does NOT work, honestly reported rather than papered over: this
# repo's backend is C++23 named modules on a custom toolchain
# (config/cpp_details.txt policy 50-52). Even with backend/compile_commands.json
# symlinked to backend/build/compile_commands.json so clangd can find it,
# clangd cannot resolve the gRPC include path or synthesize the module BMIs
# for `import <name>;`, so on EVERY backend C++ file -- regardless of what
# the diff touches -- get_diagnostics_for_file reports the same two
# boilerplate errors ('grpcpp/grpcpp.h' file not found, 'Module ... not
# found'), and because the AST never finishes building, find_symbol /
# find_referencing_symbols on backend files fail outright with "No symbol
# matching ...". The prompt below tells the model this explicitly so it
# reports "blast radius undetermined" instead of misreading a failed lookup
# as "zero callers, safe to ship."
SERENA_AVAILABLE=1
command -v uvx > /dev/null 2>&1 || SERENA_AVAILABLE=0
[[ -x "$CLAUDE_CLI" ]] || SERENA_AVAILABLE=0

SERENA_MCP_CONFIG="$WORKDIR/serena_mcp_config.json"
jq -n --arg project "$REPO_ROOT" '{
  mcpServers: {
    serena: {
      command: "uvx",
      args: ["--from", "git+https://github.com/oraios/serena", "serena",
             "start-mcp-server", "--context", "ide-assistant", "--project", $project]
    }
  }
}' > "$SERENA_MCP_CONFIG"

SERENA_PROMPT_FILE="$WORKDIR/serena_prompt.txt"
{
    echo "You are the SEMANTIC reviewer in a code-review gate for the Options &"
    echo "Futures Profit Calculator repo, reviewing: $DIFF_DESC"
    echo ""
    echo "You have serena MCP tools (get_symbols_overview, find_referencing_symbols,"
    echo "find_declaration, find_implementations, get_diagnostics_for_file,"
    echo "search_for_pattern, find_symbol) against the REAL checked-out repository at"
    echo "$REPO_ROOT -- not just the diff text below. USE THEM; a diff-only read is"
    echo "what the other reviewers already do, and is not your job."
    echo ""
    echo "KNOWN ENVIRONMENT LIMITS, so you do not misreport a tooling gap as a fact"
    echo "about the code:"
    echo "- Frontend (frontend/src/**/*.ts, *.tsx): the TypeScript language server"
    echo "  resolves normally. get_symbols_overview and find_referencing_symbols give"
    echo "  reliable results there -- use them fully."
    echo "- Backend (backend/src, backend/tests -- *.cpp, *.cppm, *.h): this codebase"
    echo "  uses C++23 named modules and a custom toolchain. clangd cannot resolve the"
    echo "  gRPC include path or synthesize module BMIs for 'import <name>;' here, so"
    echo "  on EVERY backend C++ file -- regardless of this diff --"
    echo "  get_diagnostics_for_file will show two boilerplate errors: \"'grpcpp/grpcpp.h'"
    echo "  file not found\" (pp_file_not_found) and \"Module '<name>' not found\""
    echo "  (module_not_found). Those are TOOLCHAIN artifacts, not defects this diff"
    echo "  introduced -- do not report them as findings. Only report a backend"
    echo "  diagnostic that is neither of those two."
    echo "- Because that AST does not fully build, find_referencing_symbols /"
    echo "  find_declaration / find_implementations on backend files will usually fail"
    echo "  with \"No symbol matching ...\". That failure means serena could not resolve"
    echo "  it in THIS environment -- it does NOT mean the symbol has zero real"
    echo "  callers. If a backend lookup fails, say blast radius is UNDETERMINED for"
    echo "  that symbol; never report \"no other callers\" on the strength of a failed"
    echo "  lookup."
    echo ""
    echo "Changed files:"
    cat "$WORKDIR/files.txt"
    echo ""
    echo "Diff (for context; the real files are also available to your tools):"
    cat "$DIFF_FOR_PROMPT"
    echo ""
    echo "You are under a real time budget (this gates a push) -- budget your tool"
    echo "calls, do not exhaustively re-derive things. Prioritize files with a NEW or"
    echo "CHANGED function/type signature over files that only add tests or comments."
    echo "For each such priority file where serena tools resolve (frontend TS"
    echo "reliably; attempt backend C++ but expect the degraded mode above):"
    echo "1. get_symbols_overview to see what changed at the symbol level."
    echo "2. For each function/type that is NEW or has a CHANGED SIGNATURE, call"
    echo "   find_referencing_symbols ONCE for its blast radius. Flag any real caller"
    echo "   outside this diff that the change would break."
    echo "3. get_diagnostics_for_file, filtered per the known-limits note above."
    echo "For lower-priority changed files (test-only, comment-only, or files where"
    echo "step 1 shows nothing new/changed), skip straight to reporting -- do not run"
    echo "the full tool sequence on them."
    echo ""
    echo "REJECT only if you find a real, tool-verified defect: a genuine caller"
    echo "broken by a signature/semantic change (from a find_referencing_symbols call"
    echo "that actually resolved), a real diagnostic beyond the known toolchain noise,"
    echo "or genuine declaration/implementation drift. Do not reject for style, and"
    echo "never reject on the strength of a failed/unresolved serena lookup -- that is"
    echo "a tooling gap, not evidence of a defect."
    echo ""
    echo "Report your tool findings first if you want, but your FINAL output must be"
    echo "EXACTLY one JSON object and nothing after it (no markdown fences after it):"
    echo '{"verdict":"APPROVE"|"REJECT","reasons":["..."]}'
} > "$SERENA_PROMPT_FILE"

run_serena() {
    local outfile="$1"
    [[ $SERENA_AVAILABLE -eq 1 ]] || return 1
    timeout "${SERENA_TIMEOUT_S}s" "$CLAUDE_CLI" -p \
        --permission-mode plan \
        --output-format text \
        --strict-mcp-config \
        --mcp-config "$SERENA_MCP_CONFIG" \
        < "$SERENA_PROMPT_FILE" > "$outfile" 2>&1
}

CLAUDE_OUT="$WORKDIR/claude.out"
AGY_OUT="$WORKDIR/agy.out"
CURSOR_OUT="$WORKDIR/cursor.out"
SERENA_OUT="$WORKDIR/serena.out"

CLAUDE_RC=0; AGY_RC=0; CURSOR_RC=0; SERENA_RC=0
run_claude "$CLAUDE_OUT" & CLAUDE_PID=$!
run_agy "$AGY_OUT" & AGY_PID=$!
run_cursor "$CURSOR_OUT" & CURSOR_PID=$!
run_serena "$SERENA_OUT" & SERENA_PID=$!

wait "$CLAUDE_PID" || CLAUDE_RC=$?
wait "$AGY_PID" || AGY_RC=$?
wait "$CURSOR_PID" || CURSOR_RC=$?
wait "$SERENA_PID" || SERENA_RC=$?

REVIEWERS_RUN=0
APPROVALS=0
REJECTIONS=0

report_reviewer() {
    local name="$1" rc="$2" outfile="$3" label="$4" cli_path="$5"
    if [[ ! -x "$cli_path" ]]; then
        echo "  [$label] not available ($cli_path is missing or not executable) -- NOT counted."
        return
    fi
    if [[ $rc -ne 0 ]]; then
        if [[ $rc -eq 124 ]]; then
            echo "  [$label] TIMED OUT after ${CLI_TIMEOUT_S}s -- NOT counted as an approval."
        else
            echo "  [$label] exited $rc -- NOT counted as an approval."
            [[ -s "$outfile" ]] && echo "$(tail -c 400 "$outfile" | redact)" | sed 's/^/        /'
        fi
        return
    fi
    local result verdict reasons
    result="$(extract_verdict "$outfile")"
    verdict="${result%%|*}"
    reasons="${result#*|}"
    if [[ "$verdict" == "UNPARSEABLE" ]]; then
        echo "  [$label] returned unparseable output -- NOT counted as an approval."
        echo "$(tail -c 400 "$outfile" | redact)" | sed 's/^/        /'
        return
    fi
    REVIEWERS_RUN=$((REVIEWERS_RUN + 1))
    if [[ "$verdict" == "APPROVE" ]]; then
        APPROVALS=$((APPROVALS + 1))
        echo "  [$label] APPROVE"
    else
        REJECTIONS=$((REJECTIONS + 1))
        echo "  [$label] REJECT"
    fi
    echo "$reasons" | jq -r '.[]' 2>/dev/null | sed 's/^/        - /' || true
}

report_reviewer "claude" "$CLAUDE_RC" "$CLAUDE_OUT" "claude" "$CLAUDE_CLI"
report_reviewer "agy" "$AGY_RC" "$AGY_OUT" "agy" "$AGY_CLI"
report_reviewer "cursor-agent" "$CURSOR_RC" "$CURSOR_OUT" "cursor-agent" "$CURSOR_CLI"

echo ""
echo "  Phase 2b: Serena semantic review (distinct from the ${REVIEWERS_RUN}-reviewer"
echo "  vote above -- does not supply one of the 2 required approvals; can only add"
echo "  a tool-verified rejection, and only counts when it actually ran)."
SERENA_VETO=0
if [[ $SERENA_AVAILABLE -eq 0 ]]; then
    echo "  [serena] not available (uvx or claude CLI missing) -- no semantic review performed."
elif [[ $SERENA_RC -ne 0 ]]; then
    if [[ $SERENA_RC -eq 124 ]]; then
        echo "  [serena] TIMED OUT after ${SERENA_TIMEOUT_S}s -- no semantic veto (fails open: unavailable != guilty)."
    else
        echo "  [serena] exited $SERENA_RC -- no semantic veto (fails open: unavailable != guilty)."
        [[ -s "$SERENA_OUT" ]] && echo "$(tail -c 400 "$SERENA_OUT" | redact)" | sed 's/^/        /'
    fi
else
    serena_result="$(extract_verdict "$SERENA_OUT")"
    serena_verdict="${serena_result%%|*}"
    serena_reasons="${serena_result#*|}"
    if [[ "$serena_verdict" == "UNPARSEABLE" ]]; then
        echo "  [serena] returned unparseable output -- no semantic veto."
        echo "$(tail -c 400 "$SERENA_OUT" | redact)" | sed 's/^/        /'
    elif [[ "$serena_verdict" == "REJECT" ]]; then
        SERENA_VETO=1
        echo "  [serena] REJECT (tool-verified semantic finding)"
        echo "$serena_reasons" | jq -r '.[]' 2>/dev/null | sed 's/^/        - /' || true
    else
        echo "  [serena] APPROVE (no blocking semantic finding)"
        echo "$serena_reasons" | jq -r '.[]' 2>/dev/null | sed 's/^/        - /' || true
    fi
fi

echo ""
echo "============================================================"
echo "  RESULT: ${APPROVALS} approval(s) / ${REJECTIONS} rejection(s), out of ${REVIEWERS_RUN} tri-CLI reviewer(s) that responded"
echo "  Serena semantic veto: $([[ $SERENA_VETO -eq 1 ]] && echo "YES" || echo "no")"
echo "============================================================"

if [[ $SERENA_VETO -eq 1 ]]; then
    echo "REJECTED: Serena's semantic review found a tool-verified defect (see above)."
    exit 1
elif [[ $APPROVALS -lt 2 ]]; then
    echo "REJECTED: fewer than 2 real approvals (got $APPROVALS of $REVIEWERS_RUN reviewers that responded)."
    exit 1
else
    echo "APPROVED: $APPROVALS of $REVIEWERS_RUN responding reviewers approved, Phase 1 clean, no Serena veto."
    exit 0
fi
