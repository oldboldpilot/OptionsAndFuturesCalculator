# 2026-10-07 — DRY: the canonical flags leave this repository, and three smaller duplicates go with them

Lane `dry-shared-recipe` (code policy P4: one authoritative representation of each piece of KNOWLEDGE; and do not couple things that
only look alike). The work is in sensen (`backend/sensen`, six commits ahead of its master `92d3f0b1` (tip `77f895af`)); this repository's part is
the consumer edit and the pin. Details and the mutation arms are in sensen's session log of the same name.

## What this repository changed

- **`backend/CMakeLists.txt`**: the ~135-line canonical-flags block (the ABI-tag define, the C++23 and module-scanning switches,
  `CANONICAL_FLAGS`, the two `-stdlib=libc++` linker lines, and the comments that argue for each) is now two lines,
  `include(sensen/cmake/SensenCanonicalFlags.cmake)` and `sensen_canonical_flags()`. The arguments moved with the code. It was the
  "one known duplicate" in mortgage-nest-egg; a second copy of a flag set is two sets that agree until one is edited in a hurry, and
  the symptom of that is a diagnostic that names a BMI, not a flag.
- **The pin** moves to the sensen lane tip. Taking sensen `92d3f0b1` first (the baseline commit `4da6c7d`) was not optional: its
  `numeric_lexer` imports `float_types`, so `test_mortgage_verification`, `test_mortgage_grammar` and `test_encoder_lexer_parity`
  (which compile the lexer as their own sources) no longer built until each listed `float_types.cppm` and `cpu_features.cppm`.
  That is the transitional-copies problem #81 step 7 removes; it is not part of this lane.
- **No other OFC code changed.** `fromGguf` is consumed as a value by both assistant services and by `encoder_assistant_probe`, and
  none of them wrapped it in a try/catch, so there was nothing to delete; the `if (!built)` branch that says "the assistant is
  unavailable; every other service is unaffected" is reachable for an unreadable path now.

## Gate

- **Compile commands** (`ninja -t commands all build_tests`, 4188 lines before, 4186 after): 4185 are byte-identical. The three that
  differ are exactly the dead module — the dependency scan and the compile of `special/foundation.cppm` are gone — and the archive
  command for `libsensen_slim.a`, which has 128 members against 129: the same members minus that one, `gp_ara_interfaces` at a
  different position because it is listed by group now. Every other compile and link command for every target is unchanged,
  including the flags.
- **ctest 218**: 208 passed, 8 skipped, 2 not run (CausalityBenchGate, CausalityBenchGateCanFail), 0 failed — per-test status
  identical to the baseline taken first on commit `4da6c7d` (the two 218-line status lists diff empty).
- **Parity**: tokenizer 2472/2472 on both routes, lexer 2472/2472 (10,687 literals), reconstruct 2169/2169 (16,779 exact, 288
  within one BigDecimal ulp), each output byte-identical to the previous lane's recorded C++ output.
- **`scripts/sensen_module_closure.py --check`**: OK, 128 files built.

## For mortgage-nest-egg (not done here; the step-4 lane owns that tree)

The three edits are listed in the lane report: call `sensen_canonical_flags()` instead of carrying the block; make
`scripts/check_encoder_only.sh` a thin caller of sensen's gate (`--forbid`, `--search`, `--control`, `--require`); delete the second
compile of `gp_ara_interfaces.cppm`, which `ENCODER LOGIC` now supplies.
