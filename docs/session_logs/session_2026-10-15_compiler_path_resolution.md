# Session 2026-10-15 — the documented compiler path did not exist, and a gate now keeps a literal out

@author Olumuyiwa Oluwasanmi

The open item the derived-range sweep reported and deliberately left: sensen's CLAUDE.md told every session to
configure with an absolute compiler path that is absent on this host. It was fixed in sensen as its own lane. Nothing
in this repository changed except this log; the sensen submodule pointer here still sits at its own pin, which is
deliberate — moving it implies a C++23 engine rebuild and a production deploy.

Paths under the sensen submodule are written WITHOUT backticks throughout, and so are sensen's own tool names. This
repository's documentation citation gate resolves a backticked name against this repository's own file list, which
sees backend/sensen as a single gitlink entry, so a backticked submodule path or a sensen-only script name would be
reported MISSING here.

## What was actually wrong

The reported symptom was one line. CLAUDE.md line 17, and the four mirrors that must stay byte-identical to it, told a
reader to pass a versioned LLVM root to CMake. That directory does not exist on this box: the compiler here is clang
23.1.2 under /usr/local/bin. The documented command therefore fails at its first step, and it fails with a CMake error
about a missing compiler — a message that names the symptom and not the cause. It had already cost a failed configure
in the session that found it.

Sweeping the class rather than that line found 56 reportable sites in 30 files, measured by the new gate run against
the parent commit. Six more live documents carried the same literal (QUICKSTART, the engineering notes twice, both
platform recipes in the build guide, both in the building guide, the code-facts and automated-reasoning pages, the
handoff, a generative-tooling config sample, the autograd and fused-attention recipes, a metaheuristics recipe), and
fourteen script defaults would have run unattended against it — including the repair harness, the TSan link check, the
measurement spike, six oracle build scripts and the live shell test's own goal text. sensen's update policy was wrong
the same way and mattered more: it named an absolute clangd for Serena, and /usr/bin/clangd on this host is 21.1.8,
which cannot read clang-23 module BMIs, while /usr/local/bin/clangd is 23.1.2. That is the exact reason the policy
names an absolute clangd at all.

## Why a second literal would not have been the fix

The path this box actually has is the one sensen's own engineering notes warn about: on another box /usr/local/bin held
a from-source clang 22, below the floor CMake enforces. Swapping one literal for the other moves the defect rather than
removing it, and sensen's code policy already states the principle for libc++ — a hardcoded absolute path "silently
resolves to whatever happens to sit there".

So the live instructions now name a RESOLUTION. A new script prints one verified absolute path for the c++, c or clangd
driver: it reads the clang floor from CMakeLists.txt rather than restating it, refuses a candidate below the floor,
refuses an Apple clang whose major is not comparable, and refuses a ccache or distcc shim. Two details are worth
keeping:

- A ccache shim answers --version as the compiler it wraps. A planted shim reporting a version ABOVE the floor is still
  refused, because the discriminator is what the file resolves to, not what it prints.
- The final symlink is deliberately NOT resolved. /usr/local/bin/clang++ is a symlink to clang beside it, and clang
  picks its C or C++ driver mode from the name it was invoked as — so resolving that link hands back a path that
  compiles C++ as C. Measured rather than asserted: the clang name fails to link a std::string program that the clang++
  name compiles and runs.

Nothing is written to stdout on failure, so a caller using command substitution gets an empty string rather than a
diagnostic used as a path. A Python module gives the five Python callers one copy of the same resolution, which also
removed a byte-identical helper duplicated in two mutation drivers whose last resort was an unguarded path that is
clang 21 here.

## The gate, and why it was narrowed instead of the documents widened

The hand-fix would not have held, and that is measured rather than predicted: sensen already carries a gate for the
clang FLOOR because the floor was corrected by hand in four of the seven places that stated it, and the three left
behind went on provisioning a compiler CMake had just started refusing. A path is the same shape as a number and there
are more copies of it.

The first version of the new gate reported 121 lines. Most were no defect at all: a path derived from a variable, a
path derived from a subshell, a guarded EXISTS branch with its assignment on the next line, a candidate loop, an
os.path.exists probe, an update-alternatives command that CREATES the path, a host-inventory table, a compiler error
message quoted inside a lane report, and dozens of dated records. Narrowing the gate rather than widening the documents
is the same correction made earlier this session to the lane-completion gate, where 846 of 886 problems were the gate's
own invention. A gate that cries wolf gets switched off.

Three decisions came out of that, each stated in the gate's own header as a limit:

- A path must begin at a real filesystem root to count as a literal. That is what separates a literal from a derived
  path, since a suffix pasted onto a variable begins at /bin.
- A dated record is not an instruction. Session logs, handoffs, result files, changelogs and benchmark reports are
  excluded by kind and counted — 466 of them — because editing a record of what was run makes the record false. Six
  live lines that are records carry an inline waiver with its reason, on the line above so the waived line's own
  citations stay out of the diff.
- Prose is prose. The gate's own explanation names the absent path while explaining it, and so does the code policy
  where the argument against hardcoding is written down; a gate that must flag its own explanation is a gate whose
  explanation gets deleted.

Two self-reference traps were hit on the way. One of the gate's guard tokens was the bare word "which", which silenced
a real finding sitting two lines below the prose "clangd 19 (which cannot read clang-23 module BMIs)" — an ordinary
English word cannot act as a guard. And the gate's own self-test became the largest finding in the tree, 16 of them,
the first time the new files were tracked: a positive control has to contain the very thing the gate reports. Its
fixtures now ASSEMBLE the literal from bin-directory components at run time, so the file is clean under the gate it
tests and the controls still turn red, which is what proves the assembled literal reaches the fixture.

## Evidence

| what | result |
|---|---|
| the gate on the parent | 56 findings, exit 1, 1,458 files scanned |
| the gate on the fix | 0 findings, 6 reasoned waivers, exit 0, 1,463 scanned |
| why the scanned set grew | the five files this lane adds, all five clean under the gate |
| the path gate's self-test | 27 cases, 0 failed, three repeats |
| the resolver's self-test | 15 cases, 0 failed, three repeats |
| registered ctest set | 6/6 passed, three repeats |
| mutation arms | 4, two per property in opposite directions, each red only on its own case |
| the registered ctest entry can fail | one literal reintroduced turned it red naming the file and line; restored byte-identically |
| end to end | the documented resolved command configures sensen with rc 0 |
| TSan link check | passes three times through both its new default and the explicit argument ctest passes it |
| lane precheck | 0 findings, 0 warnings, 0 waived, three runs |
| whole-tree completion | 92 records, 9 incomplete — all other lanes', none from this one |

The scanned set growing rather than staying identical is stated because an earlier draft of the commit message claimed
an identical set: that number was measured before the new files were tracked, and the gate cannot see an untracked
file.

## What was left alone, and why

- Dated records keep their literal paths. They record what was run on a named host.
- C++ sources are not scanned by the gate. The five C++ sites that name the path are all correct already — each probes
  for existence, offers the path as one candidate in a waterfall, or skips when it is absent — and that is the shape
  the gate wants rather than the one it forbids. A genuine hardcode in a .cpp is therefore invisible to it, which is
  stated in its header rather than papered over.
- sensen's distributed-verification page describes an env whitelist that sets CC and CXX to a versioned LLVM root, and
  no implementation site that builds such a whitelist was found. The line is waived as a design-document claim;
  whether that design is implemented at all is a separate question this lane did not answer.
- This repository's own CLAUDE.md names /usr/local/bin/clang++ directly. It is correct on this host today and it is the
  same defect in shape, so it is recorded as the next task rather than changed here: the resolution script lives in
  sensen, and carrying it to the sibling repositories needs a decision about where each one reads its clang floor
  from, since only sensen has the CMake variable the script consults.

## Landed

- sensen master: 5e076df8e75a18c8c8222f01ffed246699aaf084, on origin and the second remote, both confirmed by
  ls-remote rather than by the push output. 51 files synced to the NAS, 51 of 51 md5-matched.
- This repository: this log only. The sensen submodule pointer is unchanged.
