/**
 * @file encoder_lexer_parity.cpp
 * @brief Prove the DEPLOYED numeric lexer finds the same literals as the encoder trainer's.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ===========================================================================
 * WHY THIS IS THE FOURTH GATE
 * ===========================================================================
 * The encoder's per-literal pair head is indexed BY LITERAL POSITION: pair set
 * `i` belongs to literal `i`. So the engine's literal list must agree with the
 * trainer's index for index, or the model's prediction is applied to a different
 * number than the one it looked at -- a wrong answer that parses, satisfies every
 * bound and prices a different loan.
 *
 * Three things must agree, and only three:
 *
 *   ORDER and COUNT  -- the pair head is positional
 *   SPAN             -- `tokensInSpan` maps the span to tokens, and sensen REFUSES
 *                       (TokenStraddlesLiteral) rather than guessing if it does not line up
 *   VALUE            -- `reconstruct()` computes every parameter from it
 *
 * The TAG is NOT one of them, and that is measured rather than assumed:
 * `reconstruct()` applies a map BY NAME (`UNARY_FN[name](lit["value"])`) and the tag
 * predicate in `MAPS` gates CANDIDATE GENERATION during training only. Rewriting all
 * 3,381 literal tags in the reconstruct fixture to "bare" leaves both languages
 * byte-identical. The tag is compared here anyway, because it is free and because a
 * divergence in it is worth KNOWING even where it is not worth refusing.
 *
 * ---------------------------------------------------------------------------
 * TWO REPRESENTATION DIFFERENCES THAT ARE CONVERSIONS, NOT DISAGREEMENTS
 * ---------------------------------------------------------------------------
 * - The trainer bakes a k/m suffix into the value (`v *= 1000`); `NumericLiteral`
 *   keeps `value` BEFORE the multiplier and carries `scale` separately, deliberately,
 *   so M4 can be reasoned about rather than baked in. The comparison multiplies.
 * - `LiteralTag` has SIX values (Untagged, Money, Percent, Years, Months, Days) where
 *   the trainer has EIGHT -- `weeks` and `quarters` are absent. The holdout contains
 *   ZERO of either, so tags agree on it 754/754; that is a property of the CORPUS, not
 *   of the two lexers, and this file says so rather than letting the number imply
 *   otherwise. On a constructed "6 weeks ... 3 quarters" the deployed lexer answers
 *   Untagged where the trainer answers weeks/quarters, while spans and values still
 *   agree -- so the load-bearing three hold even on the case the corpus cannot show.
 *
 * Prints one canonical line per utterance for scripts/check_encoder_lexer_parity.py.
 * A probe rather than a ctest test, for the same reason as its two siblings: its input
 * is a generated fixture.
 * ===========================================================================
 */

#include <cstdio>
#include <new>
import std;
import mortgage_verification;
import fastjson;

auto main(int argc, char** argv) -> int {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <fixture.json>\n", argv[0]); return 1; }
    std::ifstream in(argv[1], std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    auto parsed = fastjson::parse(ss.str());
    if (!parsed || !parsed->is_array()) { std::fprintf(stderr, "bad fixture\n"); return 1; }
    int row = 0;
    for (const auto& e : parsed->as_array()) {
        const auto text = std::string(e["text"].as_string());
        std::string out = "{\"row\":" + std::to_string(row) + ",\"lits\":[";
        bool first = true;
        for (const auto& l : mortgage_calculator::assistant::verify::lex_numeric_literals(text)) {
            if (!first) out += ",";
            first = false;
            out += "{\"text\":\"";
            for (char c : l.text) { if (c=='"'||c=='\\') out += '\\'; out += c; }
            out += "\",\"offset\":" + std::to_string(l.offset)
                 + ",\"scale\":" + std::to_string(l.scale)
                 + ",\"tag\":" + std::to_string(static_cast<int>(l.tag)) + "}";
        }
        out += "]}";
        std::printf("%s\n", out.c_str());
        ++row;
    }
    return 0;
}
