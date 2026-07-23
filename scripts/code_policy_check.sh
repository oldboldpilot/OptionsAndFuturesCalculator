#!/usr/bin/env bash
set -euo pipefail

echo "============================================================"
echo "  CODE POLICY CHECKER (config/cpp_details.txt)"
echo "============================================================"

REPO_ROOT="$(git rev-parse --show-toplevel)"
POLICY_FILE="${REPO_ROOT}/config/cpp_details.txt"

if [[ ! -f "$POLICY_FILE" ]]; then
    echo "❌ ERROR: config/cpp_details.txt not found!"
    exit 1
fi

echo "🔍 Auditing codebase against C++23 & safety policy..."

ERRORS=0

# Rule 1: Check for forbidden raw pointer allocations (new / delete)
RAW_NEW=$(grep -rnE "\bnew\s+[A-Za-z0-9_]+" "${REPO_ROOT}/src" 2>/dev/null | grep -v "NOLINT" || true)
if [[ -n "$RAW_NEW" ]]; then
    echo "❌ POLICY VIOLATION: Raw 'new' allocation found (Rule 3 - No Raw Pointers):"
    echo "$RAW_NEW"
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] No raw 'new' pointer allocations detected."
fi

# Rule 2: Check for fast-math flag violation
FAST_MATH=$(grep -rnE "\-f""fast-math" "${REPO_ROOT}" 2>/dev/null | grep -v "cpp_details.txt" | grep -v "code_policy_check.sh" || true)
if [[ -n "$FAST_MATH" ]]; then
    echo "❌ POLICY VIOLATION: -f""fast-math flag detected (Rule 50 & 55 - FP Parity Hazard):"
    echo "$FAST_MATH"
    ERRORS=$((ERRORS + 1))
else
    echo "  [PASS] No non-associative -f""fast-math flags detected."
fi

# Rule 3: Check trailing return types in header/module definitions
MISSING_TRAILING=$(grep -rnE "^auto\s+[A-Za-z0-9_]+\(" "${REPO_ROOT}/src" 2>/dev/null | grep -v "\->" || true)
if [[ -n "$MISSING_TRAILING" ]]; then
    echo "⚠️ WARNING: Potential missing trailing return type syntax (Rule 31):"
    echo "$MISSING_TRAILING"
else
    echo "  [PASS] Function declarations follow trailing return type style."
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
