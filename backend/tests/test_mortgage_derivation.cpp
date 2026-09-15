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

    std::printf("\n10. a later turn supersedes an earlier statement of the same kind\n");
    {
        // THE MEASURED CLASS. 17 of the 19 multi-turn single-field failures on
        // the 563-row holdout are a revision the model ignored, answering with
        // the OPENING turn's figure. Every one of them parses, grounds and
        // satisfies every bound -- the earlier value really was stated.
        const std::string open_rate = "Amortize $451,300 at 5.25% over 20-year.";

        const auto rate = md::derive_candidates_in_turns("ComputeAmortization", open_rate,
                                                         "try 6.75%");
        const auto ar = only(rate, "annual_rate");
        check(ar.size() == 1 && ar.front() == "0.0675",
              "'try 6.75%' supersedes the opening 5.25% -- ONE candidate, not two (got "
              + std::to_string(ar.size()) + ": " + (ar.empty() ? "none" : ar.front()) + ")");

        const auto term = md::derive_candidates_in_turns("ComputeAmortization", open_rate,
                                                         "redo it over 15-year");
        const auto tm = only(term, "term_months");
        check(tm.size() == 1 && tm.front() == "180",
              "'redo it over 15-year' supersedes 20-year -> term_months 180 (got "
              + (tm.empty() ? "none" : tm.front()) + ")");

        // Years and months supersede ACROSS kinds: the same slot, restated in
        // the other unit. A per-kind rule would let both survive.
        const auto tm2 = only(md::derive_candidates_in_turns("ComputeAmortization", open_rate,
                                                             "make it 180 months"),
                              "term_months");
        check(tm2.size() == 1 && tm2.front() == "180",
              "a term restated in MONTHS supersedes one stated in years");
    }

    std::printf("\n11. the graph dates each operand separately\n");
    {
        // WHY THIS IS IN THE RULE BASE AND NOT A FILTER OVER THE LATEST TURN.
        // "redo it for $817,400" restates the price and says nothing about the
        // deposit, so the answer pairs the REVISED price with the ORIGINAL
        // turn's down payment -- a combination present in neither turn alone.
        const std::string open_buy =
            "Amortize the loan on a $796,000 property, a 10% deposit, 5.97%, 30-year.";

        const auto c = md::derive_candidates_in_turns("ComputeAmortization", open_buy,
                                                      "redo it for $817,400");
        const auto loan = only(c, "loan_amount");
        check(loan.size() == 1 && loan.front() == "735660.0",
              "the revised $817,400 less the ORIGINAL 10% deposit = 735660 (got "
              + (loan.empty() ? "none" : loan.front()) + ")");
        const auto home = only(c, "original_home_value");
        check(home.size() == 1 && home.front() == "817400",
              "the home value takes the revised GROSS price, not the net loan (got "
              + (home.empty() ? "none" : home.front()) + ")");
        const auto rate = only(c, "annual_rate");
        check(rate.size() == 1 && rate.front() == "0.0597",
              "a slot the revision did not mention keeps the opening turn's value");
    }

    std::printf("\n12. 'more' is an increment, and cannot be spent as a price\n");
    {
        // THE HIJACK THIS PREVENTS, stated as the test that fails without the
        // `names_increment` split: while "$750 more a month" is an ordinary
        // money literal it pairs with the opening turn's "10% down" under the
        // loan rule and derives 750 - 75 = 675. Both operands are real, the
        // arithmetic is exact, and the answer is a $675 mortgage -- the graph
        // combining two literals under a rule that cannot see that "more"
        // means an increment.
        const std::string open_buy =
            "Amortize the loan on a $796,000 property, a 10% deposit, 5.97%, 30-year.";

        const auto c = md::derive_candidates_in_turns("ComputeAmortization", open_buy,
                                                      "what if I pay $750 more a month?");
        const auto loan = only(c, "loan_amount");
        check(loan.size() == 1 && loan.front() == "716400.0",
              "an INCREMENT does not supersede the price: the loan is still 716400 (got "
              + (loan.empty() ? "none" : loan.front()) + ")");
        const auto over = only(c, "monthly_overpayment");
        check(over.size() == 1 && over.front() == "750",
              "'$750 more a month' fills the overpayment slot (got "
              + (over.empty() ? "none" : over.front()) + ")");

        // Both spellings the corpus generates, and the qualifier sits on
        // either side of the literal.
        const auto after = only(md::derive_candidates_in_turns(
                                    "ComputeAmortization", open_buy,
                                    "now add $300 extra a month"),
                                "monthly_overpayment");
        check(after.size() == 1 && after.front() == "300",
              "'$300 extra a month' -- the qualifier AFTER the literal (got "
              + (after.empty() ? "none" : after.front()) + ")");
        const auto before = only(md::derive_candidates(
                                     "ComputeAmortization",
                                     "Amortize $300,000 at 5% over 30-year, "
                                     "paying an extra $250/month"),
                                 "monthly_overpayment");
        check(before.size() == 1 && before.front() == "250",
              "'an extra $250/month' -- the qualifier BEFORE the literal (got "
              + (before.empty() ? "none" : before.front()) + ")");
    }

    std::printf("\n13. recency is strictly BETWEEN turns, never within one\n");
    {
        // THE PROPERTY THAT KEEPS EVERY SINGLE-TURN ROW UNCHANGED. The
        // comparison is strictly greater, so two literals stated in the same
        // breath do not supersede each other -- the sentence really is
        // ambiguous and the model keeps the selector role it is good at.
        const auto same_turn = md::derive_candidates(
            "ComputeAmortization", "Amortize $451,300 at 5.25% or 6.75% over 20-year.");
        check(only(same_turn, "annual_rate").size() == 2,
              "two rates in ONE turn stay ambiguous -- both candidates survive");

        // And the empty-latest call is the single-turn call, not a near-miss.
        const auto via_turns = md::derive_candidates_in_turns(
            "ComputeAmortization", "Amortize $451,300 at 5.25% or 6.75% over 20-year.", "");
        check(only(via_turns, "annual_rate") == only(same_turn, "annual_rate"),
              "an empty latest turn derives exactly what the one-turn call does");
    }

    std::printf("\n14. a repeated field is not this layer's to touch\n");
    {
        // ComputeAmortizationBatch takes parallel ARRAYS, so `term_months`
        // arrives as "[360,360]". The rule base derives the scalar 360 from
        // "30-year", `same_value` cannot parse the array and answers false, and
        // the field fell straight through to the replace path -- swapping a
        // correct two-element array for a scalar. Nothing in the raw score can
        // see this: the model is right and the layer breaks it afterwards.
        const auto c = md::derive_candidates(
            "ComputeAmortizationBatch",
            "Compare two offers on a $400,000 loan over 30-year: 6% and 6.5%.");
        std::map<std::string, std::string> emitted{{"term_months", "[360,360]"}};
        const auto v = md::reconcile(c, emitted);
        check(v.replace.empty(),
              "an array-valued field is left alone, not replaced with a scalar");

        // ...and the scalar case it was derived for still works, so the guard
        // is a shape test rather than a switch that turns the layer off.
        std::map<std::string, std::string> scalar{{"term_months", "300"}};
        const auto c2 = md::derive_candidates("ComputeAmortization",
                                              "Amortize $400,000 at 6% over 30-year.");
        const auto v2 = md::reconcile(c2, scalar);
        check(v2.replace.size() == 1 && v2.replace.front().field == "term_months",
              "a SCALAR term_months is still corrected to the stated 30 years");
    }

    std::printf("\n15. only a field in contention may veto a replacement\n");
    {
        // THE INCONSISTENCY THIS PINS. `claimed` is tallied over all candidates
        // while the skips are decided per field afterwards, so a field that is
        // going to be skipped anyway was still casting a vote -- and its vote
        // vetoed the one replacement the layer existed to make.
        const auto c = md::derive_candidates_in_turns(
            "ComputeAmortization", "Amortize $451,300 at 5.25% over 20-year.", "try 6.75%");

        // Both fields derive the SAME single value, which is the shape that
        // triggers the contest.
        check(only(c, "annual_rate").size() == 1 && only(c, "pmi_annual_rate").size() == 1 &&
                  only(c, "annual_rate").front() == only(c, "pmi_annual_rate").front(),
              "annual_rate and pmi_annual_rate both derive the one revised rate");

        std::map<std::string, std::string> emitted{
            {"annual_rate", "0.0525"}, {"pmi_annual_rate", "0.0000"}};
        const auto v = md::reconcile(c, emitted);
        check(v.replace.size() == 1 && v.replace.front().field == "annual_rate" &&
                  v.replace.front().values.front() == "0.0675",
              "a convention ZERO does not contest, so the stale rate is still corrected");

        // ...and a field genuinely holding the literal DOES contest, so the
        // guard is narrowed rather than removed.
        std::map<std::string, std::string> both{
            {"annual_rate", "0.0525"}, {"pmi_annual_rate", "0.0675"}};
        check(md::reconcile(c, both).replace.empty(),
              "two fields genuinely claiming one literal still refuse to choose");
    }

    std::printf("\n16. the solver never emits what the verifier would refuse\n");
    {
        // A derived value re-enters validation, so a replacement outside its
        // slot's band turns an answer the model got RIGHT into a refusal. This
        // is the HELOC case: "75%" answering a max-LTV question reached
        // annual_rate and came back "outside this assistant's interest-rate
        // range" on twelve rows the model had served perfectly.
        const auto c = md::derive_candidates_in_turns(
            "ComputeHeloc",
            "My home is worth $1,301,200, I owe $745,900. I want to draw $77,800 "
            "from a HELOC at 8.33% over 10 years.",
            "75%");
        std::map<std::string, std::string> emitted{
            {"annual_rate", "0.0833"}, {"max_ltv_rate", "0.75"}};
        const auto v = md::reconcile(c, emitted);
        const bool touches_rate = std::ranges::any_of(
            v.replace, [](const md::FieldCandidates& f) { return f.field == "annual_rate"; });
        check(!touches_rate,
              "a 75% LTV never replaces a stated 8.33% interest rate");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
