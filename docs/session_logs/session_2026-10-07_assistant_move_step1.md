# 2026-10-07 — #81 step 1: the shared assistant code moves into sensen

The mortgage assistant is moving from this repository into mortgage-nest-egg (#81; plan with the owner's four answers:
a new Railway service there with 2 replicas, shared code in sensen, the decoder path dropped, training stays here for
now). Step 1 moves the code both engines will need into sensen, with no behaviour change.

## What moved

- Into sensen (`7ceb38b5` on master, module commit `354e217c`): `sensen.encoder_reconstruct`, `sensen.encoder_assistant`,
  `sensen.numeric_lexer` (with `Decimal`, `parse_strict_decimal`), `sensen.utterance_guards`, and a new `sensen.ascii_text`
  holding the ASCII helpers that the lexer, the guards and both verification modules each carried privately.
- Out of OFC: `backend/src/modules/encoder_reconstruct.cppm`, `encoder_assistant.cppm/.cpp`, the lexer block of
  `mortgage_verification.cppm` and the guards. `mortgage_verification` re-exports `Decimal`, `parse_strict_decimal`,
  `LiteralTag`, `NumericLiteral` and `lex_numeric_literals` by using-declarations, so no consumer changed. The mortgage
  service no longer imports `assistant_verification` (it used only the two guards).
- The sensen pin went 985dec7c -> b4f794ba first (its own commit, `908f535`), so the move is attributable on its own.
  The closure check then named three real omissions (`logger_io.cpp`, `qwen38_io.cpp`, `weight_loader_factory_io.cpp`;
  dropping them fails the link on `Logger::log`), now listed in `sensen_slim`.
- Tests stay here, repointed: sensen builds with BUILD_TESTS OFF inside this tree, so a moved test would stop running in
  the gate that protects the engine.

## Gate

- ctest 218: 208 passed, 8 skipped, 2 not run (CausalityBenchGate*), 0 failed -- per-test identical across the old pin,
  the bump alone and the move. (The bump step alone once failed `LifecycleTransportTests`, an SGEE fd-count check under
  -j6 on a shared box; it passes alone in 30 s and does not touch sensen.)
- Parity: tokenizer 2472/2472, lexer 2472/2472 (10,687 literals), reconstruct 2169/2169, each byte-identical to baseline.
- Visitor regression 265/272 with a 0-row diff against `after_local_v5e.txt`; the 266 raw mortgage outputs byte-identical.
- Strategy: raw model output byte-identical over 1,500 holdout utterances on encoder `abd40b21`.
- Mutation arms, each restored: guard returns false (4/166 checks fail); `to_lower_char` identity (Assistant and Mortgage
  verification tests fail); year bound 100 -> 1 (41/431); decode tie-break `>` -> `>=` (EncoderOperationDecodeTest); literal
  span end offset+size (visitor set 59/272).
- A restore with `cp -p` kept the old mtime and left ninja serving mutated objects (first "final" run 59/272); the
  restored files were touched and every gate re-run on a clean build -- the numbers above are from that run.

## Not done here

- CLAUDE.md and other docs still name the old `backend/src/modules/` paths for the moved modules; the plan's docs lane
  (lane 8) rewrites them with the move's later steps.
- Next steps from the plan: the build recipe in sensen, the mfv service port, image + Railway service + gate, parity
  scoring, client flip, OFC deletion, docs.
