# Session 2026-10-22 — a Read deny rule does not stop `cat`, and an exemption tested as a substring is a bypass

@author Olumuyiwa Oluwasanmi

The dotenv under config/ is written WITHOUT backticks throughout this log. It is untracked and
deliberately so, and this repository's documentation citation gate resolves a backticked name
against the tracked file list, where that path does not appear.

## What was asked for, and why the obvious implementation does nothing

The request was an ignore file so that credential material cannot be read. An ignore file alone
does not refuse anything, and the permission rule that looks like the answer —
`permissions.deny` with a `Read(...)` pattern — governs only the Read tool. Reading a dotenv
with `cat` is a Bash call. It does not consult that rule and never did.

So the guard is three layers, of which only the third refuses a shell read:

1. `.claudeignore` — advisory, and on its own it guarantees nothing.
2. `.claude/settings.json` — 17 `permissions.deny` `Read(...)` rules over the dotenvs, keys,
   certificates and the credential stores under the home directory.
3. `.claude/hooks/block-credential-reads.sh` — a `PreToolUse` hook on Bash, the only layer that
   stops `cat`, `grep`, `sed`, `awk`, `xxd`, `strings` and `cp`.

A gate placed where the traffic is not is the same defect this repository's advertising policy
ran into from the other side: the condition was true, and nothing that could be fetched said so.

## The bypass, which was in the first cut and was found by review

The hook is fail-closed on credential paths with a small exemption list: the sanctioned loader
`scripts/run_with_env.py`, which hands the file to a child process without echoing it; example
and template dotenvs, which hold no values; and metadata-only commands whose output cannot
contain a value. The first cut tested each of those as a SUBSTRING of the command, and exited
zero as soon as one matched. That is not an exemption, it is a bypass: a compound command whose
first half is exempt and whose second half prints the real file was waved through. Three such
shapes were found — an example dotenv followed by the real one, a metadata command followed by
a read, and a read with the loader's name in a trailing comment.

The repair is an ordering, not a longer pattern list. Once a command names credential material:

1. refuse ANY shell compounding, substitution or redirection — a semicolon, an ampersand, a
   pipe, a command substitution, a backquote, a redirection in either direction, a comment
   character, or a newline — because a second command can hide behind each of them, and no
   exemption is permitted to rescue such a line;
2. only then match exemptions, ANCHORED against the whole command.

The example-dotenv exemption additionally now requires that EVERY credential-naming token in
the command is an example or template, rather than that one of them is. `set -f` precedes the
token loop so that a `*` in the command is word-split without being expanded against the real
tree first.

### What the second mutation arm found, which no amount of reading would have

Reverting the metadata exemption to an unanchored alternation of `ls|stat|du|wc` did not merely
weaken it — it permitted reads of the AWS credential store and of a production dotenv. `ls`
occurs inside the word "credentia-ls", and `du` inside "pro-du-ction". The guard tokens are
English letter pairs, so an unanchored search finds them inside the very filenames the rule
exists to protect. `grep -qE 'ls|stat|du'` does not ask whether the command IS `ls`; it asks
whether those two letters occur anywhere, and on a credential path they do. The start-of-string
and end-of-string anchors are what turn a letter-pair search into a question about the command's
identity.

## The arms

`.claude/hooks/test-block-credential-reads.sh` holds 41 cases, including every reported bypass
as a positive control. It lives in a file rather than an inline command because the live hook
blocks any Bash call that names credential material — including a test runner that passes such
a command as an argument. Each arm was restored before the next, and the restore was verified by
digest rather than assumed.

| arm | result |
|---|---|
| baseline | 41 pass, 0 fail |
| the compound-command refusal removed | 6 failed |
| the anchored exemptions reverted to substring | 3 failed |
| baseline restored, digest compared | 41 pass, 0 fail |

Both repairs are independently load-bearing: six of the bypasses are caught by the refusal and
three more only by the anchoring, so neither alone is sufficient.

## The cost, which is real and was accepted rather than carved around

Two legitimate things are now refused. A `scripts/run_with_env.py` invocation that also names
the dotenv AND pipes its output is blocked, and so is any compound command that merely MENTIONS
a credential path — including writing documentation about one, which is how this log came to be
written through the file-writing tool rather than a shell heredoc.

Carving an exception for either case means reasoning about which part of a compound command the
path belongs to, and that is exactly the reasoning that produced the bypass. The friction is the
price of the gate being decidable. A single simple command, or the file-writing tool for prose,
is the way through.

## What is NOT claimed

The hook was written and validated before it was loaded. A probe at the time showed it inactive:
Claude Code's configuration watcher only reloads directories that already held a settings file
when the session started, and `.claude/settings.json` was new. It loaded later in the same
session, which is known because it then blocked two of this session's own commands. The probe
used a pattern-matching path with nothing behind it; a positive control for a credential guard
must never be a real secret.

The `Read(...)` deny rules were NOT independently confirmed to be active. The command that would
have created a harmless dummy file to test them was itself refused, so that layer is reasoned
about rather than measured, and is recorded here as such.
