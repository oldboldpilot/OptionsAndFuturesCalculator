/**
 * The Horn-clause derivation layer: the model interprets, the solver computes.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <cstdio>
#include <new>

import std;
import mortgage_derivation;

namespace md = mortgage_calculator::assistant::derive;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

auto only(const std::vector<md::FieldCandidates>& cs, std::string_view field)
    -> std::vector<std::string> {
    for (const auto& c : cs) { if (c.field == field) { return c.values; } }
    return {};
}
}  // namespace

auto main() -> int {
    std::printf("1. a rate is computed, not copied\n");
    {
        // The measured production failure: the model emitted 0.004667 for
        // 5.64%/12, where the exact answer is 0.004700.
        const auto c = md::derive_candidates(
            "ComputePayment", "Payment on $323,300 at 5.64% over 30-year?");
        const auto rate = only(c, "rate");
        check(rate.size() == 1 && rate.front() == "0.0047",
              "ComputePayment.rate from 5.64% is EXACTLY 0.0047 (got "
              + (rate.empty() ? std::string{"none"} : rate.front()) + ")");
        const auto periods = only(c, "periods");
        check(periods.size() == 1 && periods.front() == "360",
              "periods from \"30-year\" is 360");
    }

    std::printf("\n2. the cadence is per FIELD and per OPERATION\n");
    {
        const auto a = md::derive_candidates(
            "ComputeAmortization", "Amortize $570,800 at 8.39% over 20-year.");
        check(only(a, "annual_rate") == std::vector<std::string>{"0.0839"},
              "annual_rate divides by 100, not 1200");
        // Measured: ComputeNpv/ComputeXnpv/ComputePaybackPeriod take an ANNUAL
        // `rate`. A single "bare rate means monthly" rule is wrong here, and
        // wrong silently -- a discount rate 12x too small still parses.
        const auto n = md::derive_candidates(
            "ComputeNpv", "I invest $125,000 today. What's the NPV at a 9% discount rate?");
        check(only(n, "rate") == std::vector<std::string>{"0.09"},
              "ComputeNpv.rate divides by 100 even though the field is named `rate`");
    }

    std::printf("\n3. the loan is DERIVED from price and down payment\n");
    {
        // The model emitted 714400 here; 796,000 less 10% is 716,400.
        const auto c = md::derive_candidates(
            "ComputeAmortization", "Amortize the loan on a $796,000 property, a 10% deposit, 5.97%, 30-year.");
        const auto loan = only(c, "loan_amount");
        // Compared by VALUE: the solver prints 716400.0 where the corpus writes
        // 716400.00, and a string compare would call the right answer wrong.
        check(loan.size() == 1 && std::stod(loan.front()) == 716400.0,
              "loan_amount = price - 10% is exactly 716400 (got "
              + (loan.empty() ? std::string{"none"} : loan.front()) + ")");
        const auto home = only(c, "original_home_value");
        check(home.size() == 1 && std::stod(home.front()) == 796000.0,
              "original_home_value keeps the GROSS price");

        // THE COLLISION CASE. Only one percent is stated, and both annual_rate
        // and pmi_annual_rate derive it. PMI is not mentioned; its gold is
        // 0.0000. Replacing on a single candidate alone would overwrite a
        // correct 0.0000 with 0.0597.
        std::map<std::string, std::string> real{{"annual_rate", "0.0597"},
                                                {"pmi_annual_rate", "0.0000"},
                                                {"loan_amount", "716400.00"},
                                                {"term_months", "360"},
                                                {"original_home_value", "796000.00"}};
        const auto rec = md::reconcile(c, real);
        const bool touches_pmi = std::ranges::any_of(
            rec.replace, [](const auto& f) { return f.field == "pmi_annual_rate"; });
        check(!touches_pmi,
              "a contested literal never overwrites a field the utterance is silent about");
        check(rec.replace.empty() && rec.ambiguous.empty(),
              "a fully correct answer is left entirely alone");
        check(only(c, "annual_rate") == std::vector<std::string>{"0.0597"},
              "the down-payment percent does NOT ground the rate slot");
    }

    std::printf("\n4. ambiguity is refused, never guessed\n");
    {
        // Two rates stated: nothing here says which is which.
        const auto c = md::derive_candidates(
            "ComputeAmortization", "Amortize $200,000 at 5% -- or would 6% be better? Over 30-year.");
        check(only(c, "annual_rate").size() == 2,
              "two stated percents give two candidates, not a guess");
        std::map<std::string, std::string> emitted{{"annual_rate", "0.0600"}};
        const auto r = md::reconcile(c, emitted);
        check(r.replace.empty(), "an ambiguous field is never replaced");
        check(r.ambiguous.empty(), "the model's own value selects between the candidates");
        std::map<std::string, std::string> wrong{{"annual_rate", "0.0700"}};
        check(md::reconcile(c, wrong).ambiguous == std::vector<std::string>{"annual_rate"},
              "a value matching NEITHER candidate is refused");
    }

    std::printf("\n5. reconciliation: the solver wins, and never invents\n");
    {
        const auto c = md::derive_candidates(
            "ComputePayment", "Payment on $323,300 at 5.64% over 30-year?");
        std::map<std::string, std::string> emitted{{"rate", "0.004667"}, {"periods", "360"}};
        const auto r = md::reconcile(c, emitted);
        const bool replaces_rate =
            std::ranges::any_of(r.replace, [](const auto& f) { return f.field == "rate"; });
        check(replaces_rate, "a wrong rate is REPLACED by the exact derivation");
        const bool touches_periods =
            std::ranges::any_of(r.replace, [](const auto& f) { return f.field == "periods"; });
        check(!touches_periods, "a field the model got right is left alone");

        // The field-set contract is G2's job. Deriving a value for a key the
        // model omitted would put a number in front of grounding that no
        // utterance was consulted about.
        std::map<std::string, std::string> partial{{"periods", "360"}};
        check(md::reconcile(c, partial).replace.empty(),
              "a field the model did NOT emit is never added");

        // Spelling must not count as disagreement.
        std::map<std::string, std::string> padded{{"rate", "0.004700"}, {"periods", "360"}};
        check(md::reconcile(c, padded).replace.empty(),
              "0.0047 and 0.004700 are the same number, not a disagreement");
    }

    std::printf("\n6. a convention zero is a statement, not an omission\n");
    {
        // The one case this layer was measured to make WORSE. RentVsBuy's
        // loan_amount is 0.00 when the granular shape is in use; deriving
        // price-minus-down for it turned a correct answer into a wrong one.
        const auto c = md::derive_candidates(
            "ComputeRentVsBuy",
            "Is it better to rent or buy over 7 years? Rent: $2,800/month with 4.3% annual "
            "increase. Buy: $437,900 purchase, 24% down, 6.1% over 30-year.");
        std::map<std::string, std::string> emitted{{"loan_amount", "0.00"}};
        check(md::reconcile(c, emitted).replace.empty(),
              "a model value of 0 is never overwritten by a derivation");
        // ...and the exemption must not swallow a real correction.
        std::map<std::string, std::string> nonzero{{"loan_amount", "111.11"}};
        const auto r = md::reconcile(c, nonzero);
        check(!r.replace.empty() || !only(c, "loan_amount").empty(),
              "a NON-zero wrong value is still eligible for replacement");
    }

    std::printf("\n7. precision: the MODEL'S spelling is the contract\n");
    {
        // The corpus writes `rate` to six places -- 5.5%/12 is labelled
        // 0.004583 -- while the solver computes 0.004583333333333333. Treating
        // those as different made the layer "correct" a field that was already
        // right: 31 of 257 derivations disagreed with gold on this alone and
        // served exact-match fell 397 -> 351 on the holdout.
        const auto c = md::derive_candidates(
            "ComputePayment",
            "$1,498,000 purchase, a $481,000 down payment, 5.5%, 30-year. Monthly payment?");
        std::map<std::string, std::string> rounded{{"rate", "0.004583"}};
        check(md::reconcile(c, rounded).replace.empty(),
              "an exact derivation does not 'correct' a correctly-rounded value");

        // ...and the comparison must be ASYMMETRIC. Rounding both to the
        // SHORTER of the two lengths let a terminating derivation mask a real
        // error: 0.0047 against a wrong 0.004667 collapses to 0.0047 at four
        // places. The model's spelling sets the precision; the derivation is
        // rounded TO it, never the reverse.
        const auto p2 = md::derive_candidates(
            "ComputePayment", "Payment on $323,300 at 5.64% over 30-year?");
        std::map<std::string, std::string> wrong{{"rate", "0.004667"}};
        const auto r2 = md::reconcile(p2, wrong);
        check(r2.replace.size() == 1 && r2.replace.front().field == "rate",
              "a wrong SIX-place value is still corrected by a FOUR-place derivation");
        check(!r2.replace.empty() && r2.replace.front().values.front() == "0.004700",
              "and the replacement is emitted at the MODEL'S precision (got "
              + (r2.replace.empty() ? std::string{"none"} : r2.replace.front().values.front())
              + ")");
    }

    std::printf("\n8. a dated grid is READ from the utterance, never invented\n");
    {
        const auto g = md::derive_day_offsets(
            "These payouts land on specific days, not yearly. I invest $368,500 today and "
            "expect back $131,852.19 after 394 days; $70,114.00 after 725 days.");
        check(g.has_value() && *g == "[0,394,725]",
              "the grid is [0,394,725] -- the leading 0 is the outlay at t=0 (got "
              + g.value_or("none") + ")");

        // The refusal these enable must stay a refusal when the grid ISN'T there.
        check(!md::derive_day_offsets("I invest $100,000 today and expect back year 1: $50,000.")
                   .has_value(),
              "an evenly-spaced series yields no grid");
        check(!md::derive_day_offsets("Pay it back after 365 days.").has_value(),
              "ONE dated flow is not a grid -- a lone flow sits at t=0 and its PV is its face value");
        check(!md::derive_day_offsets("$10 after 700 days; $20 after 300 days").has_value(),
              "an out-of-order grid is refused: the reading is wrong, not the series unusual");
        check(!md::derive_day_offsets("$10 after 300 days; $20 after 300 days").has_value(),
              "a repeated offset is refused");
        check(!md::derive_day_offsets("$10 after 1.5 days; $20 after 700 days").has_value(),
              "a fractional day is refused rather than half-read");
    }

    std::printf("\n9. nothing is derived when nothing is stated\n");
    {
        const auto c = md::derive_candidates("ComputePayment", "What's my payment?");
        check(only(c, "rate").empty(), "no percent stated -> no rate candidate");
        std::map<std::string, std::string> emitted{{"rate", "0.005"}};
        check(md::reconcile(c, emitted).replace.empty(),
              "with no candidates the model's value is left for grounding to judge");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
