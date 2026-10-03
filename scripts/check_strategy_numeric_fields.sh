#!/usr/bin/env bash
# kStrategyNumericFields must list exactly the fields assistant_service.cpp reads with
# is_number(). The table is a SECOND copy of a fact the validator already states, and this
# re-derives it rather than trusting that the two were kept in step -- the same reasoning as
# check_vendored_protos.sh asserting identity against the source instead of a recorded hash.
#
# It cost 120 of 120 holdout rows to learn why this matters: the encoder emitted every value
# as a string and the service refused each one on "did not give an expiration", with the
# correct params sitting in the log.
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
SRC="$ROOT/backend/src/modules/assistant_service.cpp"
[ -f "$SRC" ] || { echo "FATAL: $SRC not found" >&2; exit 1; }

derived=$(grep -oE 'obj\["[a-z_]+"\]\.is_number' "$SRC" | sed -E 's/obj\["([a-z_]+)"\]\.is_number/\1/' | sort -u)
declared=$(sed -n '/kStrategyNumericFields{/,/};/p' "$SRC" | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u)

[ -n "$derived" ]  || { echo "FATAL: derived 0 is_number() fields -- the pattern moved" >&2; exit 1; }
[ -n "$declared" ] || { echo "FATAL: kStrategyNumericFields parsed empty" >&2; exit 1; }

if [ "$derived" != "$declared" ]; then
    echo "[FAIL] policy_strategy_numeric_fields" >&2
    echo "  is_number() in the validator : $(echo "$derived"  | tr '\n' ' ')" >&2
    echo "  kStrategyNumericFields       : $(echo "$declared" | tr '\n' ' ')" >&2
    echo "  A field the validator reads as a number and the table calls a string is rendered" >&2
    echo "  quoted and refused; the reverse is rendered bare and refused as a type error." >&2
    exit 1
fi
echo "[PASS] policy_strategy_numeric_fields ($(echo "$derived" | wc -l) fields agree)"
