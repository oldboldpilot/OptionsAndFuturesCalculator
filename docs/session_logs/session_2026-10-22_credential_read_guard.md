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
3. `.claude/hooks/block_credential_reads.py` — a `PreToolUse` hook on Bash, the only layer that
   stops `cat`, `grep`, `sed`, `awk`, `xxd`, `strings` and `cp`.

A fourth entry, `sandbox.filesystem.denyRead`, is declared for when the sandbox is enabled. It
is the only one of the four that is an actual boundary; see the closing section.

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

## The second finding: reasoning about the raw string at all was the mistake

A second review of the committed version found fifteen more bypasses, in three families, and
each was reproduced before being believed:

* **the loader exemption laundered an arbitrary child.** `scripts/run_with_env.py -- cat` on the
  dotenv satisfied the exemption and printed the file. Worse, two shapes named no credential
  path whatsoever — a child of `env`, and a child of `bash -lc` — and printed the whole loaded
  set. The exemption's own purpose is to hand the child the secrets, so an arbitrary child is
  the read with extra steps.
* **quoting, escaping and globbing hid the path.** A regex over the raw text saw nothing in a
  path split across two adjacent quoted strings, in one with a backslash before a letter, or in
  a wildcard that expands to it.
* **a "metadata-only" tool echoed the contents.** `file` with a list-of-names flag prints each
  line of its argument as a filename; `wc` with the same kind of flag and the checksum tools in
  check mode do likewise. All were on the metadata allowlist.

The repair is to stop deciding from the string. The hook is now Python: it tokenises with the
standard-library shlex module, normalises and glob-expands each token, and classifies the resulting PATHS rather than
matching text. Children of the loader and flags of the metadata tools are both deny-by-default
against fixed allowlists, and `file` is gone from the metadata set entirely.

Two subtleties only measurement found. First, shlex alone is insufficient: in a command
substitution it yields a token with the closing parenthesis attached, whose basename matches no
rule, so the metacharacter travels INTO the token and hides the path — the command is therefore
tokenised a second time with metacharacters replaced by spaces and both token sets searched.
Second, the loader must be judged whenever it is INVOKED, not only when a credential path is
named, because its most dangerous uses name no path at all.

## The third finding, and the ceiling it makes visible

A third review observed that the loader's child allowlist was checked by NAME only, and that
several names on it exist precisely to execute an arbitrary command: a package runner, a Python
project runner, a Go or Rust `run` subcommand. Others accept a flag that does the same — a
makefile named on the command line, a package manager's exec subcommand. And the blanket
acceptance of any dot-slash path let any local script through. All true, and all now closed: the
generic executors are off the list, the exec-style flags are refused, a package manager must
carry one of a fixed set of subcommands, and a built binary is matched by shape — a direct child
of a build directory of this repository — rather than by a leading dot-slash.

That hardening is worth having, because those names were pure surface with no use this
repository needs. But it should not be mistaken for closure, and this is the honest limit of the
whole approach: the loader exemption cannot be made safe, because its PURPOSE is to put the
secrets in a child's environment. Every remaining name runs project-defined work — the build
tool runs what the build file says, the package manager what the manifest says — and a built
binary is a file someone can replace. Three rounds of patching a command filter is evidence that
the problem is not decidable from the command line, not that the next round will finish it.

So this round is the last of its kind. Further confidence has to come from the operating system,
not from a better pattern.

### The denylist could not be completed, so it was inverted

A fifth review made the decisive observation: a denylist of FLAG SPELLINGS cannot be finished.
A short option may be clustered with another, so a token naming a makefile equals neither of its
letters; and a GNU long option accepts any unambiguous abbreviation, so a three-letter prefix
reaches the dangerous one. Both were reproduced. For every entry a denylist holds there is a
spelling that misses it, which is the same shape of defect as the first finding one more time:
the check was asking about the text rather than about the thing.

So the flag check was inverted. Each permitted child now carries a per-tool ALLOWLIST of flags,
and any token beginning with a dash that is not on it refuses the command. Deciding membership of
a small allowlist is possible; deciding non-membership of every dangerous spelling is not. The
allowlists were drawn from the build and test invocations in this repository's own instructions,
and seven of those invocations are now ALLOW cases in the suite, so the narrowing is measured
against the workflow rather than guessed at. One asymmetry is worth noting: the directory flag is
permitted for the build tools, where it selects a directory, and refused for the configuration
tool, where the same letter loads and executes a script.

### One more mechanical defect, and what was declined

A fourth review found a genuine bug of the same family as the first: the flag denylist compared
WHOLE tokens, and a short option may carry its value attached, so a makefile named as one token
with no space equalled no entry and passed. Flags are now normalised to the bare flag before the
comparison — an attached short value and an equals-sign value both reduce correctly — and script
mode for the configuration tool, which executes a named script, joined the denylist. Reverting
the normalisation reopens exactly the three attached-option shapes.

Declined, with reasons, from the same review: removing the test runners, and pinning the Python
one to repository test paths. Both run TEST CODE, which is arbitrary code by construction, so
neither change closes the hole it names; and the build-and-test invocations in this repository's
own instructions are exactly those commands, so removing them breaks the documented workflow in
exchange for no security. This is the ceiling described above, reached from a different side.

## The arms

`.claude/hooks/test-block-credential-reads.sh` holds 95 cases, including every reported bypass
from all five findings as a positive control. It lives in a file rather than an inline command because the live hook
blocks any Bash call that names credential material — including a test runner that passes such
a command as an argument. Each arm was restored before the next, and the restore was verified by
digest rather than assumed.

| arm | result |
|---|---|
| baseline | 95 pass, 0 fail |
| the compound-command refusal removed | 6 failed |
| the anchored exemptions reverted to substring | 3 failed |
| the loader's child allowlist removed entirely | 3 failed |
| the second tokenisation removed | 2 failed |
| the metadata flag allowlist removed | 5 failed |
| the loader's child argv rules reverted to name-only | 14 failed |
| flag normalisation reverted to whole-token comparison | 3 failed |
| the per-tool flag allowlist disabled | 19 failed |

Every repair is independently load-bearing: no two arms fail the same cases, so none of the eight
is redundant and none alone is sufficient. The first two arms were measured against the shell
implementation and the last six against the Python one that replaced it. Each arm was restored
before the next and the restore compared by digest, with the baseline re-run to 95 after the
last.

## The cost, which is real and was accepted rather than carved around

Three legitimate things are now refused. A `scripts/run_with_env.py` invocation that also names
the dotenv AND pipes its output is blocked; so is any compound command that merely MENTIONS a
credential path — including writing documentation about one, which is how this log came to be
written through the file-writing tool rather than a shell heredoc; and so is a loader child
outside the build-and-test allowlist, which means an interpreter or a one-off shell under the
loader has to be spelled as a script and invoked as a program.

Carving an exception for either case means reasoning about which part of a compound command the
path belongs to, and that is exactly the reasoning that produced the bypass. The friction is the
price of the gate being decidable. A single simple command, or the file-writing tool for prose,
is the way through.

## What this is, and what it is NOT

This is defence in depth. It is not a security boundary, and it should not be described as one.
A determined command can always reach a file the process is allowed to open: through an
interpreter, a statically linked helper, or a path assembled at runtime from pieces no inspector
of the command line ever sees. Three rounds of patching a filter is evidence for that claim, not
against it — each round closed the shapes that had been thought of.

The boundary is the operating system: the file's own permissions, or a sandbox that makes the
path unreadable. `sandbox.filesystem.denyRead` is declared for the latter and takes effect when
the sandbox is enabled. The dotenv's mode was NOT inspected — the command to read it was itself
refused — so whether it is already owner-only is unverified here and worth confirming.

What the hook buys is that a careless or accidental read is refused loudly, at the moment it is
attempted, instead of landing in a transcript. That is worth having, and it is all it is.

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
