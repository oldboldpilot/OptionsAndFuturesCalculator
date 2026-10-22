#!/bin/bash
# Test suite for block-credential-reads.sh.
#
# It lives in a FILE rather than an inline command because the hook under test
# blocks any Bash call that names credential material -- including a test runner
# that passes such a command as an argument. Running it by path keeps the
# invoking command clean.
#
# The first block holds the bypasses an automated review found in the first cut:
# every exemption was tested as a SUBSTRING, so a compound command containing an
# exempt fragment was waved through while its second half printed the real file.
# They are kept as positive controls -- if any of them goes back to ALLOW, the
# anchoring regressed.

cd "$(dirname "$0")/../.." || exit 1
H=.claude/hooks/block-credential-reads.sh
pass=0; fail=0

run() { # $1 = expected BLOCK|ALLOW, $2 = command under test
  printf '%s' "{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":$(printf '%s' "$2" | jq -Rs .)}}" \
    | bash "$H" >/dev/null 2>&1
  [ $? -eq 2 ] && got=BLOCK || got=ALLOW
  if [ "$got" = "$1" ]; then
    pass=$((pass+1)); printf '  PASS  %-5s  %s\n' "$got" "$2"
  else
    fail=$((fail+1)); printf '  FAIL  got=%-5s want=%-5s  %s\n' "$got" "$1" "$2"
  fi
}

DOTENV='config/.env'
EXAMPLE='config/.env.example'

echo "=== reported bypasses: must BLOCK ==="
run BLOCK "cat $EXAMPLE; cat $DOTENV"
run BLOCK "ls $DOTENV; cat $DOTENV"
run BLOCK "cat $DOTENV  # run_with_env.py"
run BLOCK "ls $DOTENV && cat $DOTENV"
run BLOCK "stat $DOTENV | cat $DOTENV"
run BLOCK "scripts/run_with_env.py -- true; cat $DOTENV"
run BLOCK "echo \$(cat $DOTENV)"
run BLOCK "echo \`cat $DOTENV\`"
run BLOCK "cat $DOTENV > /tmp/leak"
run BLOCK "cat < $DOTENV"
run BLOCK "sha256sum $EXAMPLE; cat $DOTENV"
run BLOCK "cat $DOTENV || true"
run BLOCK "$(printf 'ls %s\ncat %s' "$DOTENV" "$DOTENV")"
run BLOCK "cat $EXAMPLE $DOTENV"

echo
echo "=== direct reads: must BLOCK ==="
run BLOCK "cat $DOTENV"
run BLOCK "grep FINANCE_API_KEY $DOTENV"
run BLOCK "sed -n '1,5p' $DOTENV"
run BLOCK "awk '{print}' $DOTENV"
run BLOCK "xxd $DOTENV"
run BLOCK "cat ~/.ssh/id_rsa"
run BLOCK "strings certs/server.pem"
run BLOCK "cat .env.production"
run BLOCK "head -20 /home/muyiwa/.railway/config.json"
run BLOCK "cat /home/muyiwa/.aws/credentials"
run BLOCK "cp $DOTENV /tmp/copy"

echo
echo "=== sanctioned, metadata-only, unrelated: must ALLOW ==="
run ALLOW "scripts/run_with_env.py -- ./backend/build/engine"
run ALLOW "python3 scripts/run_with_env.py $DOTENV -- ninja -C backend/build"
run ALLOW "./scripts/run_with_env.py -- ctest --test-dir backend/build"
run ALLOW "ls -la $DOTENV"
run ALLOW "stat -c %s $DOTENV"
run ALLOW "wc -l $DOTENV"
run ALLOW "sha256sum $DOTENV"
run ALLOW "git check-ignore -v $DOTENV"
run ALLOW "git ls-files $DOTENV"
run ALLOW "cat $EXAMPLE"
run ALLOW "cat $EXAMPLE config/.env.sample"
run ALLOW "cat README.md"
run ALLOW "git status --short"
run ALLOW "ls *.md"
run ALLOW "npm test -- --watch=false | tail -5"
run ALLOW "echo hello"

echo
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
