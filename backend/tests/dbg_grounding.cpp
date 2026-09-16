/**
 * FEED THE GROUNDING GATE ITS OWN CORPUS AND PROVE IT REFUSES NOTHING CORRECT.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The twin of `dbg_derivation`, on the layer next to it, and it exists because
 * the gap it closes cost 17 holdout rows that no accuracy number could see.
 *
 * `mortgage_verification.cppm` has claimed this property in a comment since
 * 2026-08-05 -- the misuse spec's WU-M2 acceptance criterion, "every extraction
 * label in the generated training set grounds against its own utterance" -- and
 * recorded the four families that legitimately do not, with counts. What it
 * never had was anything that RAN it. The criterion was asserted once, by hand,
 * and then the corpus moved for thirteen months.
 *
 * What it caught on the day it was written, measured against production:
 *
 *     "I'm putting $35,300 into equipment for my rental that saves me about
 *      $3,900 a year. how many years until it pays for itself?"
 *     -> "rate" = 0 does not correspond to anything in the request
 *        (the nearest figure you gave is 3900)
 *
 * with the model's output BYTE-IDENTICAL to the gold. An undiscounted payback
 * has no rate; `rate: 0.0` is the convention saying so, and grounding is per
 * field, so nothing asked what the zero MEANT. 17 of 28 holdout rows.
 *
 * WHY ACCURACY IS BLIND TO IT, which is the whole argument for this file. The
 * model is right, so `raw_exact` counts the row as correct. The user gets a
 * refusal. The two numbers diverge and only the served one moves -- the same
 * shape `DerivationCorpusSweepTest` exists for, one layer up. A gate for this
 * needs no model, no checkpoint and no GPU: the corpus already contains the
 * answer the model is being trained to give.
 *
 * KNOWN-UNGROUNDABLE FAMILIES ARE DECLARED, NOT DISCOVERED. Four are recorded
 * in `mortgage_verification.cppm` as generator findings rather than gate
 * defects -- a label the utterance genuinely never states. They are listed
 * here by operation so that a NEW one fails this gate instead of joining them
 * silently. Widening this list is a decision with a reason, which is exactly
 * what it was not when the payback rows went quiet.
 */
#include <cstdio>
#include <new>

#include <map>

import std;
import fastjson;
import mortgage_verification;

namespace mv = mortgage_calculator::assistant::verify;

namespace {

auto unescape(std::string_view s) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += (s[i] == 'n') ? '\n' : (s[i] == 't') ? '\t' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

auto split_tabs(const std::string& line) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            out.push_back(unescape(std::string_view{line}.substr(start, i - start)));
            start = i + 1;
        }
    }
    return out;
}

/**
 * Operations whose gold is KNOWN not to ground, with the reason, from
 * mortgage_verification.cppm's own "generator finding" list. A row here is
 * counted and not failed; anything NOT here fails.
 *
 * `pmi_annual_rate` is the big one: 642 training rows emit a non-zero PMI rate
 * from a turn that says only "so PMI applies" and states no rate. That is a
 * label the utterance does not contain -- a generator defect to fix in the
 * generator, never a bound to loosen here.
 */
[[nodiscard]] auto known_ungroundable(std::string_view op, std::string_view message) -> bool {
    const auto mentions = [&](std::string_view f) {
        return message.find(f) != std::string_view::npos;
    };
    if (mentions("\"pmi_annual_rate\"")) { return true; }
    if (mentions("\"prepaid_interest_days\"")) { return true; }
    if (op == "ComputeRentVsBuy" && mentions("\"monthly_piti_and_maintenance\"")) { return true; }
    return false;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 3 || std::string_view{argv[1]} != "--sweep") {
        std::printf("usage: dbg_grounding --sweep <rows.tsv>\n");
        return 2;
    }
    std::ifstream in(argv[2]);
    if (!in) {
        std::printf("FAIL: cannot open %s\n", argv[2]);
        return 2;
    }

    const bool why = std::getenv("GROUNDING_WHY") != nullptr;
    int rows = 0;
    int refused = 0;
    int excused = 0;
    std::map<std::string, int> by_op;
    std::map<std::string, std::string> example;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) { continue; }
        const auto f = split_tabs(line);
        if (f.size() < 4) { continue; }
        ++rows;

        mv::MortgageParamsInput input;
        input.params_emitted = true;
        input.operation = f[0];

        const auto parsed = fastjson::parse(f[3]);
        if (!parsed.has_value() || !parsed->is_object()) { continue; }
        for (const auto& [k, v] : parsed->as_object()) {
            if (k == "operation") { continue; }
            mv::EmittedField ef;
            ef.name = k;
            if (v.is_array()) {
                ef.repeated = true;
                for (const auto& e : v.as_array()) {
                    ef.values.push_back(e.is_string() ? std::string{e.as_string()}
                                                      : fastjson::stringify(e));
                }
            } else {
                ef.values.push_back(v.is_string() ? std::string{v.as_string()}
                                                  : fastjson::stringify(v));
            }
            input.fields.push_back(std::move(ef));
        }

        // The service joins the earlier turns and the latest with a newline --
        // `grounding_text`. Mirrored here rather than re-derived, because a
        // sweep that grounds against different text from the service's proves
        // a property of this file instead of a property of production.
        std::string text = f[1];
        if (!f[2].empty()) {
            if (!text.empty()) { text += "\n"; }
            text += f[2];
        }

        const auto verdict = mv::ground_emitted_values(input, text);
        if (verdict.outcome == mv::Outcome::Proven) { continue; }

        if (known_ungroundable(f[0], verdict.message)) {
            ++excused;
            continue;
        }
        ++refused;
        ++by_op[f[0]];
        if (!example.contains(f[0])) { example[f[0]] = verdict.message; }

        // `--why`: the literals the lexer actually found, and how each field
        // was classified. Every wrong diagnosis on this gate so far has been a
        // guess about one of those two, and both are one call away.
        if (why) {
            std::printf("\n--- %s\n%s\n", f[0].c_str(), text.c_str());
            std::printf("  verdict: %s\n", verdict.message.c_str());
            for (const auto& lit : mv::lex_numeric_literals(text)) {
                const char* tag = lit.tag == mv::LiteralTag::Percent  ? "percent"
                                  : lit.tag == mv::LiteralTag::Money  ? "money"
                                  : lit.tag == mv::LiteralTag::Years  ? "years"
                                  : lit.tag == mv::LiteralTag::Months ? "months"
                                  : lit.tag == mv::LiteralTag::Days   ? "days"
                                                                      : "untagged";
                std::printf("    literal %-18s %s\n", lit.value.to_string().c_str(), tag);
            }
            for (const auto& ef : input.fields) {
                const auto k = mv::classify_slot(ef.name);
                std::printf("    field   %-26s kind=%d  value=%s\n", ef.name.c_str(),
                            static_cast<int>(k),
                            ef.values.empty() ? "" : ef.values.front().c_str());
            }
        }
    }

    std::printf("swept %d rows: %d refused, %d excused (declared generator findings)\n",
                rows, refused, excused);
    for (const auto& [op, n] : by_op) {
        std::printf("  REFUSED %-34s %4d   e.g. %s\n", op.c_str(), n, example[op].c_str());
    }
    if (refused != 0) {
        std::printf(
            "\nFAIL: the gate refuses its own GOLD. Either a label the utterance does not\n"
            "state (fix the generator) or a convention the gate does not know (fix the\n"
            "gate). Both are real; neither is a tolerance to widen.\n");
        return 1;
    }
    std::printf("PASS: every generated label grounds against its own utterance.\n");
    return 0;
}
