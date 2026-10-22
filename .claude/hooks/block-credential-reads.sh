#!/bin/bash
# Refuse any Bash command that would print the CONTENTS of credential material.
#
# A permissions.deny Read(...) rule only governs the Read tool; `cat config/.env`
# is a Bash call and sails straight past it. This hook closes that gap.
#
# ORDER IS THE WHOLE DESIGN. An exemption tested as a SUBSTRING of the command is
# not an exemption, it is a bypass: `cat config/.env.example; cat config/.env` and
# `ls config/.env; cat config/.env` both contain an exempt fragment while the rest
# of the line prints the real file. So once a command names credential material:
#
#   1. any shell compounding or redirection  -> BLOCK, no exemption may apply;
#   2. only then are exemptions matched, ANCHORED against the whole command.
#
# Exit 2 tells Claude Code to block the call and feed stderr back as the reason.

cmd=$(jq -r '.tool_input.command // empty' 2>/dev/null)
[ -z "$cmd" ] && exit 0

refuse() {
  echo "BLOCKED: $1" >&2
  echo "" >&2
  echo "This command names credential material (a dotenv, key, token or credential" >&2
  echo "store). Permitted instead, as a SINGLE simple command with no ; & | \$( \` < > #:" >&2
  echo "  - load it:  scripts/run_with_env.py -- <your command>" >&2
  echo "  - inspect:  ls / stat / wc / sha256sum / git check-ignore" >&2
  echo "" >&2
  echo "If you need a VALUE from it, ask the user; do not read the file." >&2
  exit 2
}

creds='(^|[^A-Za-z0-9_./-])(config/)?\.env($|[^A-Za-z0-9])|\.env\.(local|production|prod|staging|development|dev)|\.railway/config\.json|\.aws/credentials|\.ssh/|\.netrc|\.npmrc|id_rsa|id_ed25519|\.(pem|p12|pfx|jks)($|[^A-Za-z0-9])|/secrets?/'

# Does the command name credential material at all? If not, this hook is silent.
printf '%s' "$cmd" | grep -qE "$creds" || exit 0

# (1) Compounding, substitution, redirection or a comment, while naming credential
# material. A second command can hide behind any of these, so refuse outright --
# no exemption below is allowed to rescue it.
printf '%s' "$cmd" | grep -qE '[;&|`<>#]|\$\(' \
  && refuse "it names credential material AND uses shell compounding or redirection."
case "$cmd" in *$'\n'*) refuse "it names credential material AND spans multiple lines." ;; esac

# (2) The command is now a single simple command. Exemptions, anchored.

# 2a. The sanctioned loader: hands the file to a child process, never echoes it
#     (CLAUDE.md: never source config/.env, the shell strips the JSON quotes).
printf '%s' "$cmd" \
  | grep -qE '^[[:space:]]*(python3?[[:space:]]+)?(\./)?scripts/run_with_env\.py([[:space:]]|$)' \
  && exit 0

# 2b. Example/template dotenvs carry no values -- but EVERY credential-naming
#     token must be one. One real path among them and the command is refused.
all_example=1
set -f  # word-split $cmd WITHOUT pathname expansion; a `*` in the command must
        # not be globbed against the real tree before it is inspected.
for tok in $cmd; do
  if printf '%s' "$tok" | grep -qE "$creds"; then
    printf '%s' "$tok" | grep -qE '\.env\.(example|sample|template)$' || all_example=0
  fi
done
[ "$all_example" -eq 1 ] && exit 0

# 2c. Metadata-only inspection: existence, size, ignore status, digest. None of
#     these reveals a value. Anchored, and the whole command must be just this.
printf '%s' "$cmd" | grep -qE '^[[:space:]]*(ls|stat|file|du|wc|basename|dirname|realpath|md5sum|sha1sum|sha256sum|git[[:space:]]+(check-ignore|ls-files|check-attr))([[:space:]]+[^[:space:]]+)*[[:space:]]*$' \
  && exit 0

refuse "it could print the contents of credential material."
