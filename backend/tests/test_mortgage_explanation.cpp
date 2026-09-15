/**
 * The PMI explanation layer: the solver says it, and it can only say what it
 * was told.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <cstdio>
#include <new>

import std;
import mortgage_explanation;

namespace mx = mortgage_calculator::assistant::explain;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

auto joined(const std::vector<std::string>& lines) -> std::string {
    std::string all;
    for (const auto& l : lines) { all += l + " "; }
    return all;
}

auto mentions(const std::vector<std::string>& lines, std::string_view needle) -> bool {
    return joined(lines).find(needle) != std::string::npos;
}

/** Every run of digits in the prose, so a sentence can be checked against the
 *  facts it claims to describe. */
auto numerals(const std::vector<std::string>& lines) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string cur;
    // A '.' is only part of a number when digits sit around it -- otherwise it
    // is the end of a sentence, and counting that as a numeral made this helper
    // report the prose as containing a figure the facts did not.
    const auto flush = [&out, &cur]() {
        while (!cur.empty() && cur.back() == '.') { cur.pop_back(); }
        if (std::ranges::any_of(cur, [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            })) {
            out.push_back(cur);
        }
        cur.clear();
    };
    for (const char ch : joined(lines)) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.') {
            cur.push_back(ch);
        } else {
            flush();
        }
    }
    flush();
    return out;
}

/** A scenario where PMI terminates part-way, with no overpayment. */
auto terminating() -> mx::PmiFacts {
    mx::PmiFacts f;
    f.monthly_premium = "212.50";
    f.ends_month = 137;
    f.total_paid = "29112.50";
    f.threshold_ltv = "0.80";
    f.basis_value = "796000";
    f.term_months = 360;
    f.monthly_overpayment = "0";
    f.baseline_ends_month = 137;
    return f;
}
}  // namespace

auto main() -> int {
    std::printf("1. a terminating scenario is explained, with the month and the basis\n");
    {
        const auto lines = mx::explain_pmi(terminating());
        for (const auto& l : lines) { std::printf("    | %s\n", l.c_str()); }
        check(!lines.empty(), "a scenario with PMI produces sentences");
        check(mentions(lines, "month 137"),
              "the termination month is stated");
        check(mentions(lines, "80% of the $796000"),
              "the threshold is rendered as a PERCENT against the basis value");
        check(mentions(lines, "$212.5 a month"),
              "the premium is stated without trailing zeros");
        check(mentions(lines, "29112.5"),
              "the total paid is stated");
    }

    std::printf("\n2. NO NUMBER APPEARS THAT WAS NOT A FACT\n");
    {
        // The property the whole design exists for. A model narrating this could
        // emit a fluent, plausible, wrong month; nothing here can, because a
        // figure reaches the text only as a clause argument. Asserted
        // MECHANICALLY rather than trusted, because a renderer typo would break
        // it just as effectively as a hallucination would.
        const auto f = terminating();
        const auto lines = mx::explain_pmi(f);
        const std::vector<std::string> allowed{
            "212.5", "212.50", "137", "80", "0.80", "796000", "29112.5", "29112.50", "360"};
        std::vector<std::string> stray;
        for (const auto& n : numerals(lines)) {
            if (std::ranges::find(allowed, n) == allowed.end()) { stray.push_back(n); }
        }
        std::string detail;
        for (const auto& s : stray) { detail += s + " "; }
        check(stray.empty(),
              "every numeral in the prose traces to a fact (stray: " +
                  (detail.empty() ? std::string{"none"} : detail) + ")");
    }

    std::printf("\n3. no PMI is SAID, not left to silence\n");
    {
        mx::PmiFacts f;
        f.monthly_premium = "0";
        f.term_months = 360;
        const auto lines = mx::explain_pmi(f);
        check(lines.size() == 1 && mentions(lines, "no mortgage insurance"),
              "a loan with no PMI gets exactly one sentence saying so");
        check(!mentions(lines, "stops after month"),
              "and no termination sentence, which would be about a premium that does not exist");
    }

    std::printf("\n4. PMI that never terminates says so rather than implying it ends\n");
    {
        mx::PmiFacts f = terminating();
        f.ends_month = 0;              // never reached the threshold
        f.baseline_ends_month = 0;
        const auto lines = mx::explain_pmi(f);
        check(mentions(lines, "whole 360-month term"),
              "the full-term outcome is stated with the term length");
        check(!mentions(lines, "stops after month"),
              "and the terminating sentence is NOT also emitted");
    }

    std::printf("\n5. the overpayment sentence is earned, not assumed\n");
    {
        mx::PmiFacts f = terminating();
        f.monthly_overpayment = "250";
        f.ends_month = 104;
        f.baseline_ends_month = 137;
        const auto moved = mx::explain_pmi(f);
        check(mentions(moved, "33 months early"),
              "the months saved are COMPUTED by the solver (137 - 104), not by the renderer");
        check(mentions(moved, "month 104") && mentions(moved, "month 137"),
              "both the new and the baseline month are named");

        // An overpayment that moves nothing gets no sentence: claiming a saving
        // of zero months reads as a defect in the calculator rather than as a
        // property of the scenario.
        mx::PmiFacts unmoved = terminating();
        unmoved.monthly_overpayment = "250";
        const auto same = mx::explain_pmi(unmoved);
        check(!mentions(same, "months early"),
              "an overpayment that does not move the date earns no sentence");
    }

    std::printf("\n6. the paragraph order is FIXED, not the solver's search order\n");
    {
        // Ordering by solution order would make the paragraph depend on clause
        // layout, so a rule added at the top of the base would silently reorder
        // the user's explanation.
        mx::PmiFacts f = terminating();
        f.monthly_overpayment = "250";
        f.ends_month = 104;
        f.baseline_ends_month = 137;
        const auto lines = mx::explain_pmi(f);
        const auto pos = [&lines](std::string_view needle) {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].find(needle) != std::string::npos) { return static_cast<int>(i); }
            }
            return -1;
        };
        const int termination = pos("stops after month");
        const int cost = pos("over the life of the loan");
        const int saving = pos("months early");
        check(termination >= 0 && cost > termination && saving > cost,
              "termination, then cost, then what the overpayment bought");

        const auto again = mx::explain_pmi(f);
        check(again == lines, "the same facts produce the identical paragraph every run");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
