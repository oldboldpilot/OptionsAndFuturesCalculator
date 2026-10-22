#!/bin/bash
# Test suite for the credential read guard.
#
# It lives in a FILE rather than an inline command because the hook under test
# blocks any Bash call that names credential material -- including a test runner
# that passes such a command as an argument. Running it by path keeps the
# invoking command clean.
#
# Pass the hook to exercise as $1 (default: the installed one). Two groups are
# kept as permanent positive controls, each from an automated review finding:
#
#   group A -- exemptions tested as a SUBSTRING of the command, so a compound
#              line with an exempt fragment was waved through;
#   group B -- the raw string reasoned about instead of parsed, so the loader
#              exemption ran an arbitrary child, quote-splitting and globs
#              evaded detection, and a list-of-files flag echoed contents.
#
# If any control returns to ALLOW, that repair regressed.

cd "$(dirname "$0")/../.." || exit 1
H=${1:-.claude/hooks/block_credential_reads.py}
case "$H" in
  *.py) RUNNER=(python3 -I "$H") ;;
  *)    RUNNER=(bash "$H") ;;
esac
pass=0; fail=0

run() { # $1 = expected BLOCK|ALLOW, $2 = command under test
  printf '%s' "{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":$(printf '%s' "$2" | jq -Rs .)}}" \
    | "${RUNNER[@]}" >/dev/null 2>&1
  [ $? -eq 2 ] && got=BLOCK || got=ALLOW
  if [ "$got" = "$1" ]; then
    pass=$((pass+1)); printf '  PASS  %-5s  %s\n' "$got" "$2"
  else
    fail=$((fail+1)); printf '  FAIL  got=%-5s want=%-5s  %s\n' "$got" "$1" "$2"
  fi
}

D='config/.env'
E='config/.env.example'

echo "=== group A: substring-exemption bypasses (must BLOCK) ==="
run BLOCK "cat $E; cat $D"
run BLOCK "ls $D; cat $D"
run BLOCK "cat $D  # run_with_env.py"
run BLOCK "ls $D && cat $D"
run BLOCK "stat $D | cat $D"
run BLOCK "scripts/run_with_env.py -- true; cat $D"
run BLOCK "echo \$(cat $D)"
run BLOCK "echo \`cat $D\`"
run BLOCK "cat $D > /tmp/leak"
run BLOCK "cat < $D"
run BLOCK "sha256sum $E; cat $D"
run BLOCK "cat $D || true"
run BLOCK "$(printf 'ls %s\ncat %s' "$D" "$D")"
run BLOCK "cat $E $D"

echo
echo "=== group B: raw-string-reasoning bypasses (must BLOCK) ==="
# B1: the loader exemption must not launder an arbitrary child.
run BLOCK "scripts/run_with_env.py -- cat $D"
run BLOCK "python3 scripts/run_with_env.py -- sh -c 'cat $D'"
run BLOCK "scripts/run_with_env.py -- env"
run BLOCK "scripts/run_with_env.py -- bash -lc id"
run BLOCK "scripts/run_with_env.py $D -- printenv"
# B2: quoting, escaping and globbing must not hide the path.
run BLOCK 'cat conf*/.en*'
run BLOCK 'cat "config/.e""nv"'
run BLOCK "cat 'config'/.env"
run BLOCK 'cat config/.en\v'
run BLOCK 'cat ./config/./.env'
# B3: a flag that treats the file as a list of names echoes its contents.
run BLOCK "file -f $D"
run BLOCK "wc --files0-from=$D"
run BLOCK "wc --files-from=$D"
run BLOCK "sha256sum -c $D"
run BLOCK "md5sum -c $D"

echo
echo "=== group C: the loader's child must not be a generic executor (must BLOCK) ==="
# Each of these loads the secrets and then runs a command of the caller's
# choosing. They name no credential path, so only the loader rule catches them.
run BLOCK "scripts/run_with_env.py -- npx whatever"
run BLOCK "scripts/run_with_env.py -- uv run python -c 'import os'"
run BLOCK "scripts/run_with_env.py -- go run x.go"
run BLOCK "scripts/run_with_env.py -- cargo run"
run BLOCK "scripts/run_with_env.py -- yarn whatever"
run BLOCK "scripts/run_with_env.py -- pnpm whatever"
run BLOCK "scripts/run_with_env.py -- make -f /tmp/evil.mk"
run BLOCK "scripts/run_with_env.py -- make --eval=x"
run BLOCK "scripts/run_with_env.py -- npm exec whatever"
run BLOCK "scripts/run_with_env.py -- ninja -f /tmp/evil.ninja"
run BLOCK "scripts/run_with_env.py -- npm whatever"
run BLOCK "scripts/run_with_env.py -- ./scripts/anything.sh"
run BLOCK "scripts/run_with_env.py -- ./a.out"
run BLOCK "scripts/run_with_env.py -- wrangler whatever"
# A short option with its value ATTACHED is one token that equals no denylist
# entry, so a whole-token comparison misses it.
run BLOCK "scripts/run_with_env.py -- make -f/tmp/evil.mk"
run BLOCK "scripts/run_with_env.py -- make --eval=x"
run BLOCK "scripts/run_with_env.py -- cmake -P /tmp/evil.cmake"
run BLOCK "scripts/run_with_env.py -- cmake -P/tmp/evil.cmake"
run BLOCK "scripts/run_with_env.py -- pytest -p evil"
run BLOCK "scripts/run_with_env.py -- pytest -pevil"
run BLOCK "scripts/run_with_env.py -- make -e"
# A flag DENYLIST cannot be completed: a clustered short option equals neither
# of its letters, and a GNU long option accepts any unambiguous abbreviation.
run BLOCK "scripts/run_with_env.py -- make -kf/tmp/evil.mk"
run BLOCK "scripts/run_with_env.py -- make -kf /tmp/evil.mk"
run BLOCK "scripts/run_with_env.py -- make --ev=x"
run BLOCK "scripts/run_with_env.py -- make --eva=x"
run BLOCK "scripts/run_with_env.py -- cmake -C /tmp/evil.cmake"
run BLOCK "scripts/run_with_env.py -- cmake -C/tmp/evil.cmake"
run BLOCK "scripts/run_with_env.py -- ninja -t deps"
run BLOCK "scripts/run_with_env.py -- pytest --pdb"
run BLOCK "scripts/run_with_env.py -- npm run build --prefix /tmp"
# A permitted flag NAME does not imply a permitted VALUE: a cache entry can name
# a script to execute or a launcher to run every compile through, and a
# directory flag can point outside this repository.
run BLOCK "scripts/run_with_env.py -- cmake -DCMAKE_PROJECT_INCLUDE=/tmp/evil.cmake"
run BLOCK "scripts/run_with_env.py -- cmake -DCMAKE_TOOLCHAIN_FILE=/tmp/evil.cmake"
run BLOCK "scripts/run_with_env.py -- cmake -DCMAKE_CXX_COMPILER_LAUNCHER=/tmp/evil"
run BLOCK "scripts/run_with_env.py -- cmake -DCMAKE_CXX_COMPILER=/tmp/evil"
run BLOCK "scripts/run_with_env.py -- cmake -DCMAKE_BUILD_TYPE=../../etc"
run BLOCK "scripts/run_with_env.py -- cmake -B /tmp/out"
run BLOCK "scripts/run_with_env.py -- cmake -B/tmp/out"
run BLOCK "scripts/run_with_env.py -- ctest --test-dir /tmp/evil"
run BLOCK "scripts/run_with_env.py -- ninja -C /tmp/evil"
run BLOCK "scripts/run_with_env.py -- ninja -C/tmp/evil"
run BLOCK "scripts/run_with_env.py -- ninja -C ../../tmp"
run BLOCK "scripts/run_with_env.py -- make SHELL=/tmp/evil"
run BLOCK "scripts/run_with_env.py -- make CC=/tmp/evil"

echo
echo "=== direct reads (must BLOCK) ==="
run BLOCK "cat $D"
run BLOCK "grep FINANCE_API_KEY $D"
run BLOCK "sed -n '1,5p' $D"
run BLOCK "awk '{print}' $D"
run BLOCK "xxd $D"
run BLOCK "cat ~/.ssh/id_rsa"
run BLOCK "strings certs/server.pem"
run BLOCK "cat .env.production"
run BLOCK "head -20 /home/muyiwa/.railway/config.json"
run BLOCK "cat /home/muyiwa/.aws/credentials"
run BLOCK "cp $D /tmp/copy"
run BLOCK "tar cf /tmp/x.tar $D"

echo
echo "=== sanctioned, metadata-only, unrelated (must ALLOW) ==="
run ALLOW "scripts/run_with_env.py -- ./backend/build/engine"
run ALLOW "python3 scripts/run_with_env.py $D -- ninja -C backend/build"
run ALLOW "./scripts/run_with_env.py -- ctest --test-dir backend/build"
run ALLOW "scripts/run_with_env.py -- npm run build"
run ALLOW "scripts/run_with_env.py -- ninja -C backend/build build_tests"
run ALLOW "scripts/run_with_env.py -- ninja -Cbackend/build"
run ALLOW "scripts/run_with_env.py -- ninja -j8 -C backend/build"
run ALLOW "scripts/run_with_env.py -- ctest --test-dir backend/build --output-on-failure"
run ALLOW "scripts/run_with_env.py -- cmake --build backend/build"
run ALLOW "scripts/run_with_env.py -- make -C backend -j4"
run ALLOW "scripts/run_with_env.py -- cmake -DCMAKE_CXX_COMPILER=/usr/local/bin/clang++ -B backend/build"
run ALLOW "scripts/run_with_env.py -- cmake -DCMAKE_BUILD_TYPE=Release --build backend/build"
run ALLOW "scripts/run_with_env.py -- cmake -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -B backend/build"
run ALLOW "scripts/run_with_env.py -- cmake -S . -B backend/build"
run ALLOW "ls -la $D"
run ALLOW "stat -c %s $D"
run ALLOW "wc -l $D"
run ALLOW "sha256sum $D"
run ALLOW "git check-ignore -v $D"
run ALLOW "git ls-files $D"
run ALLOW "cat $E"
run ALLOW "cat $E config/.env.sample"
run ALLOW "cat README.md"
run ALLOW "git status --short"
run ALLOW "ls *.md"
run ALLOW "npm test -- --watch=false | tail -5"
run ALLOW "echo hello"
run ALLOW "grep -rn TODO src/"

echo
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
