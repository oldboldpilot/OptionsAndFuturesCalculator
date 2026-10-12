#!/usr/bin/env bash
# test_check_docs_claims.sh -- gate for tools/check_docs_claims.py on a throwaway fixture repo.
# Each case is one the first cut of the checker got wrong or one a docs lane produced on 2026-10-07:
#   a real flag that starts with '-' (git grep without -e read it as an option: real flags were called invented),
#   an invented function, an invented flag, a design-only name in a "Design (not built)" section and in an
#   INDEX-style entry, prose caught between two backtick spans, a real quoted message, a real path.
set -u
here=$(cd "$(dirname "$0")" && pwd)
checker="$here/check_docs_claims.py"
fx=$(mktemp -d "${TMPDIR:-/tmp}/docs_claims.XXXXXX")
fx2=$(mktemp -d "${TMPDIR:-/tmp}/docs_claims2.XXXXXX")
fx3=$(mktemp -d "${TMPDIR:-/tmp}/docs_claims3.XXXXXX")
trap 'rm -rf "$fx" "$fx2" "$fx3"' EXIT
fail=0
check() { if [ "$2" = "$3" ]; then echo "[PASS] $1"; else echo "[FAIL] $1: expected '$3', got '$2'"; fail=1; fi; }

g() { git -C "$fx" -c user.name=fixture -c user.email=fixture@example.invalid "$@"; }
g init -q
mkdir -p "$fx/src" "$fx/docs"
cat > "$fx/src/tool.cpp" <<'EOF'
auto realFunction() -> int { return 0; }
// usage: tool --real-flag --served-model-name NAME
constexpr auto kMsg = "the real error message for a refused call";
EOF
printf '# Doc\n\nIntro.\n' > "$fx/docs/a.md"
g add -A && g commit -q -m base
base=$(g rev-parse HEAD)

cat >> "$fx/docs/a.md" <<'EOF'

## Usage
Call `realFunction()` and pass `--real-flag` or `--served-model-name`.
The helper `inventedFunction` takes `--fake-flag`.
On refusal it prints `the real error message for a refused call`.
It lives in `src/tool.cpp` and not in `src/missing_file.cpp`; it writes a `.sllq` store.
If `-` is passed, input is read from stdin; when `, the patch is read from` happens nothing is checked.

## Planned engine -- Design (not built)
`FutureRegistry` composes `FutureRecipe`.

## Notes
A sentence that mentions Design (not built) in passing does not make `NotARealName` design.
A row documenting the marker `**Design (not built)**` does not make `AlsoNotARealName` design.

## Index
- [Thing](x.md) -- built.
  - Uses `realFunction`.
- [Engine](y.md) -- uses `PlannedThing`.
  - **Status**: **Design (not built)**.
EOF
g add -A && g commit -q -m docs

out=$(python3 "$checker" --repo "$fx" --base "$base")
rc=$?
echo "$out"
missing=$(grep -c '^MISSING' <<<"$out")
check "exit status is 1 while a citation is missing" "$rc" "1"
# 6: the 4 invented names, plus the second NotARealName line and the quoted marker on it -- a
# backticked string that is not in the code is a citation like any other.
check "exactly the 6 invented citations are missing (a passing mention of the phrase is not a marker)" "$missing" "6"
check "a name next to a passing mention of the phrase is missing" "$(grep -c '^MISSING.*NotARealName' <<<"$out")" "2"
check "a line that DOCUMENTS the marker inside backticks does not apply it" "$(grep -c '^MISSING.*AlsoNotARealName' <<<"$out")" "1"
check "invented function is missing" "$(grep -c '^MISSING.*inventedFunction' <<<"$out")" "1"
check "invented flag is missing" "$(grep -c '^MISSING.*--fake-flag' <<<"$out")" "1"
check "invented path is missing" "$(grep -c '^MISSING.*missing_file' <<<"$out")" "1"
check "a real flag starting with '-' is found (the git grep -e bug)" "$(grep -c 'real-flag\|served-model-name' <<<"$out")" "0"
check "a real quoted message is found" "$(grep -c 'real error message' <<<"$out")" "0"
check "design-section names are design, not missing" "$(grep -c '^DESIGN.*FutureRe' <<<"$out")" "2"
check "a design-marked index entry is design" "$(grep -c '^DESIGN.*PlannedThing' <<<"$out")" "1"
check "prose between two backtick spans is a fragment" "$(grep -c '^FRAGMENT' <<<"$out")" "1"
check "a file extension such as .sllq is not a fragment" "$(grep -c '^FRAGMENT.*sllq' <<<"$out")" "0"

# Clean docs pass: drop the invented lines and the status must be 0.
sed -i '/inventedFunction/d; /missing_file/d; /NotARealName/d; /AlsoNotARealName/d' "$fx/docs/a.md"
g commit -q -am clean
python3 "$checker" --repo "$fx" --base "$base" > /dev/null
check "exit status is 0 when nothing is missing" "$?" "0"

# --all checks whole files, not only the added lines.
python3 "$checker" --repo "$fx" --all docs/a.md > /dev/null
check "--all on the cleaned file passes" "$?" "0"

# ---------------------------------------------------------------------------
# Portability arms (2026-10-12). The checker is copied into five other repositories;
# three of them are on `main`, and their code lives in directories a sensen-shaped prefix
# list does not name. Each arm is a MEASURED defect of the sensen-only version: the default
# base crashed with a Python traceback in ThinButQuickWebFramework and mortgage-nest-egg,
# and `deploy/gateway/no_such_file.sh` in an OptionsAndFuturesCalculator doc was neither
# found nor reported -- an invented path read as a pass, which is the direction that hides.
h() { git -C "$fx2" -c user.name=fixture -c user.email=fixture@example.invalid "$@"; }
h init -q -b main
mkdir -p "$fx2/deploy" "$fx2/docs"
printf 'echo real\n' > "$fx2/deploy/run.sh"
printf '# Doc\n' > "$fx2/docs/b.md"
h add -A && h commit -q -m base
base2=$(h rev-parse HEAD)
h update-ref refs/remotes/origin/main "$base2"              # name the default branch without a network
h symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main
cat >> "$fx2/docs/b.md" <<EOF

A real path \`deploy/run.sh\` and an invented one \`deploy/no_such.sh\`.
Prose with a slash: \`and/or\` names no tracked directory and no file, so it is not checked.
Revision \`$base2\` resolves here; \`dd94b7890\` is another repository's commit.
The word \`acceded\` is seven letters that are all hex digits and is still an identifier.
EOF
h add -A && h commit -q -m docs

out2=$(python3 "$checker" --repo "$fx2")                     # no --base: the default must work on main
rc2=$?
echo "$out2"
check "the default base works on a repository whose default branch is main" "$rc2" "1"
check "exactly the invented path and the hex-letter word are missing" "$(grep -c '^MISSING' <<<"$out2")" "2"
check "an invented path under a tracked directory no prefix list names is missing" "$(grep -c '^MISSING.*deploy/no_such.sh' <<<"$out2")" "1"
check "a real path under that same directory is found" "$(grep -c 'deploy/run.sh' <<<"$out2")" "0"
check "prose with a slash is not treated as a path" "$(grep -c 'and/or' <<<"$out2")" "0"
check "a revision this repository resolves is found" "$(grep -c "$base2" <<<"$out2")" "0"
check "another repository's revision is reported as a revision, not missing" "$(grep -c '^REVISION.*dd94b7890' <<<"$out2")" "1"
check "a word spelled only in hex letters is an identifier, not a revision" "$(grep -c '^MISSING.*acceded' <<<"$out2")" "1"

sed -i '/no_such.sh/d; /acceded/d' "$fx2/docs/b.md"
h commit -q -am clean
python3 "$checker" --repo "$fx2" > /dev/null
check "a foreign revision alone leaves the exit status 0" "$?" "0"

# A file named on the command line that is not there must REFUSE. Measured 2026-10-12: the
# checker was handed a path that did not exist in fastestjsoninthewest and answered
# "checked=0 missing=0" at exit 0 -- a pass over a document nothing had looked at.
out_gone=$(python3 "$checker" --repo "$fx2" --all docs/no_such_doc.md 2>&1)
check "a named file that is not there refuses with a status" "$?" "1"
check "the refusal names the file" "$(grep -c 'no such file: docs/no_such_doc.md' <<<"$out_gone")" "1"
check "it does not report a vacuous clean pass" "$(grep -c 'checked=0 missing=0' <<<"$out_gone")" "0"

# A repository with no origin at all must REFUSE by name rather than traceback.
git -C "$fx3" init -q
mkdir -p "$fx3/docs" && printf '# Doc\n' > "$fx3/docs/c.md"
git -C "$fx3" -c user.name=fixture -c user.email=fixture@example.invalid add -A
git -C "$fx3" -c user.name=fixture -c user.email=fixture@example.invalid commit -q -m base
out3=$(python3 "$checker" --repo "$fx3" 2>&1)
check "no origin ref refuses with a status" "$?" "1"
check "the refusal names what is missing and what to pass" "$(grep -c 'no origin/HEAD, origin/master or origin/main' <<<"$out3")" "1"
check "the refusal is not a Python traceback" "$(grep -c 'Traceback' <<<"$out3")" "0"

[ $fail -eq 0 ] && echo "=== check_docs_claims: all passed ===" || echo "=== check_docs_claims: FAILURES ==="
exit $fail
