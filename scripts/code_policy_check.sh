#!/usr/bin/env bash
#
# Audits first-party code against config/cpp_details.txt.
#
# Scope is git-tracked files only, via `git ls-files`. That is the whole trick,
# and it is what the previous version got wrong in both possible directions at
# once:
#
#   * Rules 1 and 3 scanned "${REPO_ROOT}/src" — a directory that does not
#     exist, because the C++ lives in backend/src. grep matched nothing, so
#     both rules printed [PASS] unconditionally and had never once checked a
#     file.
#
#   * Rule 2 scanned the entire repository, pulling in backend/build (13 GB of
#     gitignored build output), _deps (downloaded third-party source),
#     node_modules and documentation. It failed on SGEE docs whose offence was
#     the sentence "no -f'fast-math'", so the gate was permanently red for a
#     reason no commit could fix.
#
# Two false PASSes and one false FAIL: the audit reported on nothing it
# actually inspected. `git ls-files` fixes the scope by construction — it omits
# build output, downloaded dependencies and node_modules, and lists submodules
# as single gitlink entries rather than descending into them. SGEE and sensen
# are separate projects carrying their own policies and their own checkers.
#
# The flag string is split ("-f""fast-math") so this script does not match
# itself.

set -euo pipefail

echo "============================================================"
echo "  CODE POLICY CHECKER (config/cpp_details.txt)"
echo "============================================================"

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"
POLICY_FILE="${REPO_ROOT}/config/cpp_details.txt"

if [[ ! -f "$POLICY_FILE" ]]; then
    echo "❌ ERROR: config/cpp_details.txt not found!"
    exit 1
fi

# First-party C++: the sources this repository owns.
mapfile -t CXX_FILES < <(git ls-files -- \
    'backend/src/*.cpp' 'backend/src/*.cppm' 'backend/src/*.h' 'backend/src/*.hpp' \
    'backend/include/*.h' 'backend/include/*.hpp' 'backend/include/*.cppm')

# Build configuration: the only places a compiler flag can actually be set.
# Documentation is deliberately out of scope — a .md mentioning a flag does not
# pass it to a compiler.
mapfile -t BUILD_FILES < <(git ls-files -- \
    'backend/CMakeLists.txt' 'backend/cmake/*' 'backend/Dockerfile' \
    'CMakeLists.txt' 'cmake/*' 'scripts/*.sh' 'railway.json' \
    | grep -v 'scripts/code_policy_check.sh' || true)

if [[ ${#CXX_FILES[@]} -eq 0 ]]; then
    echo "❌ ERROR: no first-party C++ sources matched — the audit scope is wrong."
    exit 1
fi

echo "🔍 Auditing ${#CXX_FILES[@]} C++ source(s) and ${#BUILD_FILES[@]} build file(s)"

ERRORS=0

# --------------------------------------------------------------------------
# Drop `file:line:` matches whose ONLY occurrence of the pattern sits inside a
# string literal. Reads `file:line:text` on stdin, strips "..." spans from the
# text, and keeps the line only if the pattern still matches what remains.
#
# Prose is not code. This exists because user-facing copy here really does say
# "upgrade for a new one", and a checker that reports that as a raw allocation
# teaches the reader to ignore it.
# --------------------------------------------------------------------------
strip_string_literals_then_match() {
    local pattern="$1"
    while IFS= read -r line; do
        local prefix="${line%%:*}"
        local rest="${line#*:}"
        local lineno="${rest%%:*}"
        local text="${rest#*:}"
        # Remove double-quoted spans (including escaped quotes) and raw strings.
        local stripped
        stripped=$(printf '%s' "$text" | sed -E 's/R"[^(]*\([^)]*\)[^"]*"//g; s/"([^"\\]|\\.)*"//g')
        if printf '%s' "$stripped" | grep -qE "$pattern"; then
            printf '%s:%s:%s\n' "$prefix" "$lineno" "$text"
        fi
    done
}

# --------------------------------------------------------------------------
# Rule 3: no raw owning pointers.
#
# Comment bodies are skipped so prose about "a new value" is not a violation.
# Placement new and NOLINT-annotated lines are permitted.
#
# STRING LITERALS are skipped for the same reason comment bodies are, and this
# was a real defect rather than a refinement: user-facing copy in this codebase
# genuinely contains the word -- api_key's "upgrade for a new one" and
# assistant_verification's prompt-injection phrase list "new instructions:" were
# both reported as raw allocations. A checker that cries wolf on prose trains
# the reader to skim its output, which is worse than not running it. The rule
# still fires on a real `new Widget` sitting outside quotes on the same line.
# --------------------------------------------------------------------------
RAW_NEW=$(grep -nE "\bnew\s+[A-Za-z_][A-Za-z0-9_]*" "${CXX_FILES[@]}" 2>/dev/null \
    | grep -vE "^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)" \
    | grep -v "NOLINT" \
    | grep -vE "\bnew[[:space:]]*\(" \
    | strip_string_literals_then_match "\bnew\s+[A-Za-z_]" \
    || true)
if [[ -n "$RAW_NEW" ]]; then
    echo "❌ POLICY VIOLATION: Raw 'new' allocation found (Rule 3 - No Raw Pointers):"
    echo "$RAW_NEW"
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] No raw 'new' pointer allocations in first-party C++."
fi

# --------------------------------------------------------------------------
# Rules 50 & 55: no -f'fast-math'. It makes floating point non-associative,
# breaking cross-host FP parity and, for SGEE, durable-replay determinism.
#
# Only lines that actually add the flag count. A commented-out line, or one
# recording that the flag is deliberately absent, is compliance being
# documented rather than breached.
# --------------------------------------------------------------------------
FAST_MATH=""
if [[ ${#BUILD_FILES[@]} -gt 0 ]]; then
    FAST_MATH=$(grep -nE -- "-f""fast-math" "${BUILD_FILES[@]}" 2>/dev/null \
        | grep -vE "^[^:]+:[0-9]+:[[:space:]]*(#|//)" \
        | grep -viE "no -f""fast-math|without|excluded|absent|never" \
        || true)
fi
if [[ -n "$FAST_MATH" ]]; then
    echo "❌ POLICY VIOLATION: -f""fast-math set in build configuration (Rules 50 & 55 - FP Parity Hazard):"
    echo "$FAST_MATH"
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] No -f""fast-math in first-party build configuration."
fi

# --------------------------------------------------------------------------
# Rule 31: trailing return types.
#
# A signature may wrap across lines, so the `->` can sit below the line that
# matched. Look ahead to the opening brace or semicolon rather than judging the
# matched line alone.
# --------------------------------------------------------------------------
MISSING_TRAILING=""
for f in "${CXX_FILES[@]}"; do
    while IFS=: read -r lineno _; do
        [[ -z "$lineno" ]] && continue
        if ! awk -v start="$lineno" 'NR>=start && NR<start+8 {
                buf = buf $0
                if (buf ~ /->/) { found=1; exit }
                if (buf ~ /[;{]/) { exit }
             } END { exit !found }' "$f"; then
            MISSING_TRAILING+="${f}:${lineno}:$(sed -n "${lineno}p" "$f")"$'\n'
        fi
    done < <(grep -nE "^auto[[:space:]]+[A-Za-z_][A-Za-z0-9_]*\(" "$f" 2>/dev/null || true)
done
if [[ -n "$MISSING_TRAILING" ]]; then
    echo "⚠️ WARNING: Potential missing trailing return type syntax (Rule 31):"
    echo "$MISSING_TRAILING"
else
    echo "  [PASS] Function definitions follow trailing return type style."
fi

if [[ $ERRORS -gt 0 ]]; then
    echo "============================================================"
    echo "❌ CODE POLICY CHECK FAILED WITH $ERRORS ERROR(S)."
    echo "============================================================"
    exit 1
else
    echo "============================================================"
    echo "✅ CODE POLICY AUDIT PASSED CLEANLY."
    echo "============================================================"
    exit 0
fi
