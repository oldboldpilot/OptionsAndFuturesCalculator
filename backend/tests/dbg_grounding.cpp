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


/**
 * One gold value as the MODEL would have written it on the wire.
 *
 * `fastjson::stringify` is a JSON serializer, not a wire formatter, and it
 * renders the number 100000 as `1e+05`. The verifier's grammar refuses that --
 * correctly, because no model emits it -- so the sweep reported a corpus row as
 * ungroundable when the only thing wrong was this function:
 *
 *   "1e+05" in field "cost" is not a bare decimal this contract accepts
 *
 * on a row whose utterance plainly says $100,000. The corpus stores `cost` as a
 * JSON number; the model emits a decimal string; the gate has to compare what
 * the SERVICE would see. Formatting an integral value with no exponent and no
 * trailing `.0` is what closes that gap.
 */
[[nodiscard]] auto as_wire_text(const fastjson::json_value& v) -> std::string {
    if (v.is_string()) { return std::string{v.as_string()}; }
    if (v.is_number()) {
        const double d = v.as_number();
        if (d == std::floor(d) && std::fabs(d) < 1e15) {
            return std::format("{}", static_cast<std::int64_t>(d));
        }
        // Enough places for a rate like 0.0631 and for money's two, without the
        // trailing zeros a fixed width would add to an already-exact value.
        auto s = std::format("{:.6f}", d);
        while (s.size() > 1 && s.back() == '0') { s.pop_back(); }
        if (!s.empty() && s.back() == '.') { s.pop_back(); }
        return s;
    }
    return fastjson::stringify(v);
}

/**
 * Fields whose GOLD is known not to ground, by name rather than by message.
 *
 * The same three families `known_ungroundable` excuses, needed a second time
 * because the ask sweep below has to skip them BEFORE it mutates anything: a
 * field the utterance never states is one this gate has no opinion about, and
 * feeding it in would assert the opposite of what the corpus says.
 */
[[nodiscard]] auto gold_does_not_ground(std::string_view field) -> bool {
    return field == "pmi_annual_rate" || field == "prepaid_interest_days" ||
           field == "monthly_piti_and_maintenance";
}

/**
 * The gold value moved somewhere the utterance does not mention.
 *
 * x1.37 rather than a constant: a fixed sentinel would ground by accident on
 * whichever row happened to contain it, and the multiplier keeps the value in
 * the same order of magnitude, so what comes back is an ORDINARY ungrounded
 * refusal rather than an out-of-range one. Counted slots are rounded, because a
 * fractional `term_months` is refused on SHAPE and would test the grammar
 * instead of the question.
 */
[[nodiscard]] auto invent_off_corpus(std::string_view field, const std::string& gold)
    -> std::string {
    double v = 0.0;
    const auto [ptr, ec] = std::from_chars(gold.data(), gold.data() + gold.size(), v);
    if (ec != std::errc{} || v == 0.0) { return {}; }
    v *= 1.37;
    switch (mv::classify_slot(field)) {
        case mv::SlotKind::MonthCount:
        case mv::SlotKind::YearCount:
        case mv::SlotKind::PeriodIndex:
        case mv::SlotKind::Frequency:
            return std::format("{}", static_cast<std::int64_t>(std::llround(v)));
        default:
            break;
    }
    auto s = std::format("{:.6f}", v);
    while (s.size() > 3 && s.back() == '0' && s[s.size() - 2] != '.') { s.pop_back(); }
    return s;
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

    // The ask sweep's own counters -- see the block at the bottom of the row
    // loop for what it proves and why it is in this binary rather than a third.
    int ask_probes = 0;
    int ask_unusable = 0;
    int ask_wrong = 0;
    std::map<std::string, std::string> ask_example;

    // The STATED-ONLY sweep's counters (2026-10-06). The service shapes what the model emitted
    // BEFORE it verifies -- `shape_stated_params` drops every value the visitor's words do not
    // support and turns a missing essential into a question -- so the gold is shaped the same way
    // here. Two properties, one per direction a filter can be wrong in:
    //   asked_wrongly   the layer would ASK for an essential the gold STATES and grounds;
    //   dropped_stated  the layer would DROP a gold figure the words ground (silently losing a
    //                   figure the visitor gave, with no refusal and no question).
    int shaped_asks = 0;
    int asked_wrongly = 0;
    int dropped_stated = 0;
    std::map<std::string, std::string> shape_example;

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
                for (const auto& e : v.as_array()) { ef.values.push_back(as_wire_text(e)); }
            } else {
                ef.values.push_back(as_wire_text(v));
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

        // ===================================================================
        // THE ASK SWEEP: a question must never be asked about something the
        // user SAID.
        // ===================================================================
        // The complement of the gate below, on the same corpus and in the same
        // pass. That gate proves the verifier does not REFUSE its own gold;
        // this one proves the layer on top of it does not turn a refusal into a
        // CLARIFYING QUESTION when the utterance plainly carries the answer.
        //
        // The two fail in opposite directions and only one of them is visible
        // from an accuracy number. Under-asking shows up as `asked_ok` and
        // needs a model, an engine and a holdout -- measured 46/86 on v20,
        // which is what sent anyone looking. OVER-asking shows up as nothing at
        // all: the row is a refusal either way, `raw_exact` cannot move, and
        // the only symptom is a user being asked for a figure they just gave.
        // That is the direction with no natural alarm, so it is the direction
        // that gets a gate.
        //
        // The probe is a MUTATION, which is what makes it a test of the
        // question rather than of the corpus: each field that has a wording is
        // moved off-corpus one at a time, and the verdict must stay a refusal.
        // A mutation that still verifies is counted and skipped -- it means the
        // invented value grounded anyway, so there is nothing to assert.
        //
        // It is deliberately scoped to fields `clarifying_question` can word.
        // Anything else keeps its refusal by construction and asserting on it
        // would pass for the wrong reason.
        for (std::size_t fi = 0; fi < input.fields.size(); ++fi) {
            const auto& ef = input.fields[fi];
            if (ef.repeated || ef.values.size() != 1) { continue; }
            if (gold_does_not_ground(ef.name)) { continue; }
            if (mv::clarifying_question(f[0], ef.name).empty()) { continue; }

            const auto invented = invent_off_corpus(ef.name, ef.values.front());
            if (invented.empty()) { continue; }  // a convention zero states nothing

            auto probe = input;
            probe.fields[fi].values[0] = invented;

            // The service SHAPES first. A mutated value is then either DROPPED as a default (and, if
            // the field is essential, asked about) or KEPT and refused. Asking about the mutated
            // field is the defect: the words state it, so the model got it WRONG.
            const auto probe_shape = mv::shape_stated_params(probe, text);
            const auto asks_this = [&](const std::string& asked) {
                if (asked.empty()) { return false; }
                if (asked == ef.name) { return true; }
                for (const auto& req : mv::essential_requirements(f[0], {})) {
                    const bool both = std::ranges::find(req.any_of, asked) != req.any_of.end() &&
                                      std::ranges::find(req.any_of, ef.name) != req.any_of.end();
                    if (both) { return true; }
                }
                return false;
            };
            if (!probe_shape.missing_field.empty()) {
                if (!asks_this(probe_shape.missing_field)) { continue; }   // a DIFFERENT field is missing
                ++ask_probes;
                ++ask_wrong;
                if (!ask_example.contains(ef.name)) {
                    ask_example[ef.name] = f[0] + " :: " + mv::clarifying_question(f[0], ef.name) +
                                           "  <- but the user said " + ef.values.front() + "\n      " + text;
                }
                continue;
            }
            auto kept_probe = probe;
            std::erase_if(kept_probe.fields, [&](const mv::EmittedField& pf) {
                return std::ranges::find(probe_shape.kept, pf.name) == probe_shape.kept.end();
            });
            const auto pv = mv::verify_mortgage_output(kept_probe, text);
            if (pv.outcome == mv::Outcome::Proven) {
                ++ask_unusable;  // the invented value grounded (or was dropped); nothing to assert
                continue;
            }
            ++ask_probes;
            if (pv.reason != mv::ReasonCode::UnstatedField || !asks_this(pv.field)) { continue; }

            ++ask_wrong;
            if (!ask_example.contains(ef.name)) {
                ask_example[ef.name] =
                    f[0] + " :: " + mv::clarifying_question(f[0], ef.name) + "  <- but the user said " +
                    ef.values.front() + "\n      " + text;
            }
        }

        // ===================================================================
        // SHAPE FIRST, EXACTLY AS THE SERVICE DOES, then verify what is KEPT.
        // ===================================================================
        const auto shaped = mv::shape_stated_params(input, text);
        const auto is_kept = [&](std::string_view name) {
            return std::ranges::find(shaped.kept, name) != shaped.kept.end();
        };
        // Alone, does the gold figure ground against the words? (The single-field probe is what
        // separates "the visitor said it" from "the model put it there".)
        const auto grounds_alone = [&](const mv::EmittedField& ef) {
            mv::MortgageParamsInput one;
            one.params_emitted = true;
            one.operation = input.operation;
            one.fields.push_back(ef);
            return mv::ground_emitted_values(one, text).outcome == mv::Outcome::Proven;
        };
        const auto is_zero = [](const mv::EmittedField& ef) {
            return std::ranges::all_of(ef.values, [](const std::string& v) {
                const auto d = mv::parse_strict_decimal(v);
                return d.has_value() && d->is_zero();
            });
        };
        // A convention CONSTANT is dropped on purpose even when some figure happens to equal it.
        const auto is_convention_name = [](std::string_view n) {
            return n == "payments_per_year" || n == "periods_per_year" || n == "compound_frequency" ||
                   n == "factor" || n == "pmi_drop_off_ltv" || n == "timing" || n == "max_ltv_rate";
        };
        for (const auto& ef : input.fields) {
            // An ENUM or a boolean is a class default when the words are silent (straight-line,
            // paid in cash, undiscounted): it grounds trivially, which says nothing about whether
            // the visitor stated it. The numeric figures are the ones a filter can lose.
            const auto kind = mv::classify_slot(ef.name);
            if (kind == mv::SlotKind::Enumeration || kind == mv::SlotKind::Boolean) { continue; }
            if (ef.repeated || is_kept(ef.name) || is_zero(ef) || is_convention_name(ef.name)) { continue; }
            if (!grounds_alone(ef)) { continue; }
            ++dropped_stated;
            if (!shape_example.contains("drop:" + f[0] + "." + ef.name)) {
                shape_example["drop:" + f[0] + "." + ef.name] =
                    "DROPS a figure the words ground: " + ef.name + " = " + ef.values.front() + "\n      " + text;
            }
        }
        if (!shaped.missing_field.empty()) {
            // The service ASKS here. Right if the gold does not state that figure; wrong if it does.
            ++shaped_asks;
            for (const auto& req : mv::essential_requirements(f[0], {})) {
                if (std::ranges::find(req.any_of, shaped.missing_field) == req.any_of.end()) { continue; }
                for (const auto& ef : input.fields) {
                    if (std::ranges::find(req.any_of, ef.name) == req.any_of.end()) { continue; }
                    // AN ENUM GROUNDS TRIVIALLY, and the drop loop above already skips it for that
                    // reason: "INTEREST" proves nothing about whether the visitor SAID interest.
                    // Counting it here called the right question -- "interest or principal?", for a
                    // cumulative figure asked of "from 1 to 7" -- a question about a stated figure.
                    const auto ask_kind = mv::classify_slot(ef.name);
                    if (ask_kind == mv::SlotKind::Enumeration || ask_kind == mv::SlotKind::Boolean) { continue; }
                    if (ef.repeated || is_zero(ef) || !grounds_alone(ef)) { continue; }
                    ++asked_wrongly;
                    if (!shape_example.contains("ask:" + f[0] + "." + ef.name)) {
                        shape_example["ask:" + f[0] + "." + ef.name] =
                            "ASKS about " + shaped.missing_field + " but the words state " + ef.name + " = " +
                            ef.values.front() + "\n      " + text;
                    }
                }
            }
            continue;
        }
        mv::MortgageParamsInput kept_input = input;
        std::erase_if(kept_input.fields, [&](const mv::EmittedField& ef) { return !is_kept(ef.name); });

        const auto verdict = mv::ground_emitted_values(kept_input, text);
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
    std::printf("shaping: %d rows the service would ASK about, %d asked wrongly, %d stated figures dropped\n",
                shaped_asks, asked_wrongly, dropped_stated);
    for (const auto& [key, ex] : shape_example) {
        std::printf("  %s: %s\n", key.c_str(), ex.c_str());
    }
    if (asked_wrongly != 0 || dropped_stated != 0) {
        std::printf(
            "\nFAIL: the stated-only filter is wrong in the direction with no alarm. Either it\n"
            "asks for a figure the visitor gave, or it silently drops one -- and no refusal and\n"
            "no question marks the loss. Narrow the evidence rule; never widen the gold.\n");
        return 1;
    }
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

    std::printf("ask sweep: %d probes, %d asked wrongly, %d unusable (mutation still grounded)\n",
                ask_probes, ask_wrong, ask_unusable);
    for (const auto& [field, ex] : ask_example) {
        std::printf("  WOULD ASK about %-28s %s\n", field.c_str(), ex.c_str());
    }
    if (ask_wrong != 0) {
        std::printf(
            "\nFAIL: the layer would ask a clarifying question about a field the utterance\n"
            "STATES. That is not a cosmetic slip -- it hides a corrupted value behind a\n"
            "question, which is the one thing `refine_unstated` promises never to do.\n"
            "Narrow what counts as evidence; do not widen the question table.\n");
        return 1;
    }
    if (ask_probes == 0) {
        std::printf(
            "\nFAIL: the ask sweep probed nothing, so it proved nothing. Either no field\n"
            "has a clarifying wording any more or the mutation stopped biting.\n");
        return 1;
    }
    std::printf("PASS: no clarifying question is asked about a field the utterance states.\n");
    return 0;
}
