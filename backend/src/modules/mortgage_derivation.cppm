/**
 * Derive a field's value from the utterance, instead of trusting the model's
 * arithmetic.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY THIS EXISTS, measured rather than supposed. Classifying v14's 154
 * held-out failures with every conversational turn in hand:
 *
 *     55.2%  the operation and the field set are RIGHT and one VALUE is wrong,
 *            and that value is one documented arithmetic step from a literal
 *            the user actually stated
 *     22.7%  the wrong operation was named
 *     17.5%  a field the operation discards (a harness artefact, since fixed)
 *      4.5%  everything else
 *
 * The canonical case is "Payment on $323,300 at 5.64% over 30-year?", where
 * the gold `rate` is 0.004700 and the model emitted 0.004667 -- it identified
 * the slot correctly and computed 5.6/12 instead of 5.64/12. A 0.6B model
 * doing decimal division is the weakest link in the chain, and it is the one
 * part of the job that does not need a model at all.
 *
 * WHY sensen.logic AND NOT z3. z3 is vendored and reachable, but `Term` has no
 * binary-float alternative -- logic.cppm carries a static_assert saying so --
 * so `5.64 / 1200` evaluates as an EXACT decimal and yields 0.0047, not
 * 0.004666666. That exactness is the property this whole surface is built on;
 * it is why finance.proto carries money as decimal strings, and it is exactly
 * what the model gets wrong. Pulling libz3 into the production image to get a
 * worse numeric contract would be the wrong trade twice over.
 *
 * WHAT IT WILL NOT DO. A derivation is used only when it is UNIQUE. Two
 * literals that could both fill a slot produce two solutions and the field is
 * left alone, because "refused, never repaired" forbids choosing between
 * plausible readings -- the same rule that refuses an inverted P&L bound
 * rather than swapping it. The TVM sign flip was admitted for the opposite
 * reason and is the precedent here: exactly one reading was admissible.
 *
 * It also never ADDS a field the model omitted. The field-set contract is
 * G2's job, checked separately, and inventing a key here would put a value in
 * front of grounding that no utterance was consulted about.
 */
module;

#include <cstdio>
#include <new>

export module mortgage_derivation;

import std;
import sensen.logic;
import sensen.logic_parser;
import mortgage_verification;

namespace mv = mortgage_calculator::assistant::verify;

export namespace mortgage_calculator::assistant::derive {

/**
 * Every value the utterance admits for one field, exact.
 *
 * A LIST rather than a single value, because the count is the decision:
 *
 *   1 candidate   the utterance determines the field. The solver's value is
 *                 used REGARDLESS of what the model typed -- the model's job
 *                 is to say which slots exist and what plays them, never to
 *                 do the arithmetic.
 *   >1 candidate  two literals could fill this slot. The model's own number
 *                 selects between them if it matches one exactly; otherwise
 *                 the field is refused. This is the only numeric role the
 *                 model keeps, and it is one it is good at -- picking, not
 *                 computing.
 *   0 candidates  no documented map reaches it. Left exactly as it is, for
 *                 grounding to accept or refuse as it does today.
 */
struct FieldCandidates {
    std::string field;
    std::vector<std::string> values;   ///< exact decimal text, solver-computed
};

/**
 * Fields of `operation` whose value is UNIQUELY derivable from `user_text`.
 *
 * The caller decides what to do with them; this function neither reads nor
 * changes the model's output. Keeping it that way is what lets the test assert
 * the derivation on its own, without a model or an engine.
 */
[[nodiscard]] auto derive_candidates(std::string_view operation, std::string_view user_text)
    -> std::vector<FieldCandidates>;

/**
 * Candidates for a request the user has since REVISED, latest turn winning.
 *
 * A clarifying exchange restates a slot: "Amortize $451,300 at 5.25% over
 * 20-year." then "try 6.75%". Both figures are the user's own words and both
 * reach the rule base, so the rate slot derives TWO candidates -- at which
 * point `reconcile` hands the choice to the model under the >1 rule, and the
 * model returns the FIRST turn's rate. 17 of the 19 multi-turn single-field
 * failures on the holdout are that, and not one is an arithmetic error.
 *
 * THE FIX IS IN THE GRAPH, NOT AROUND IT. Each literal is emitted with the
 * turn that stated it and `kRules` carries `superseded_*` clauses saying
 * "nothing later of this kind supersedes this", so recency is a constraint the
 * dependency rules solve WITH rather than a filter applied to their output.
 * That distinction is load-bearing: the operands of a rule are dated
 * independently, so "redo it for $817,400" combines the revised price with the
 * ORIGINAL turn's down payment -- a pairing that exists in neither turn alone
 * and that no post-filter over the latest turn could ever produce.
 *
 * The qualifier words matter for the same reason. "what if I pay $750 more a
 * month?" is a revision, and while its 750 is an ordinary `money` fact it
 * pairs with the opening turn's "10% down" and derives a $675 mortgage. The
 * lexer's `names_increment` routes it to `extra_money`, a kind no loan or
 * price rule mentions, so the hijack is impossible by construction -- and only
 * once that split exists is recency on plain `money` safe to state at all.
 *
 * With `latest` empty -- every first-turn call -- every fact is turn 0, no
 * `superseded_*` clause can fire, and this IS `derive_candidates`: unchanged
 * by construction rather than by a branch that happens to agree.
 */
[[nodiscard]] auto derive_candidates_in_turns(std::string_view operation,
                                              std::string_view earlier,
                                              std::string_view latest)
    -> std::vector<FieldCandidates>;

/**
 * Reconcile the model's params against the derivation, by the rule above.
 *
 * Returns the fields to REPLACE and the fields to REFUSE, separately, because
 * they are different outcomes and the caller reports them differently.
 *
 * DEPENDS ONLY ON THE UTTERANCE, which is what makes the latency free: the
 * request text is known before the first token is decoded, so `derive_candidates`
 * runs concurrently with the model rather than after it.
 */
struct Reconciliation {
    std::vector<FieldCandidates> replace;   ///< exactly one candidate, model differs
    std::vector<std::string> ambiguous;     ///< several candidates, model matches none
};

/**
 * The day-offset grid an utterance states, as a JSON array, or nothing.
 *
 * "$131,852.19 after 394 days; $70,114 after 725 days" -> `[0,394,725]`. The
 * leading 0 is the outlay at t=0, which every dated series in this corpus
 * carries and no utterance states in days.
 *
 * WHY THIS IS A FOLD AND NOT A HORN CLAUSE. Every other derivation here is an
 * arithmetic relation between one literal and one field, which is exactly what
 * a rule base expresses well. This is an ORDERED COLLECTION of every literal
 * carrying one tag, and expressing "gather these, in this order" in Prolog
 * would be ceremony around a loop -- the rule base earns its place on the
 * relations, not on everything.
 *
 * It exists because refusing was right for the reason given and wrong about
 * the facts. `dated_utterance_rejects_operation` refuses ComputeNpv on a dated
 * series because remapping would need a `dates` array the model never emitted,
 * and inventing one is fabrication. Measured on the v15 holdout: in 4 of 4
 * such failures the grid is stated in the utterance in full. Nothing is
 * invented by reading it, so the objection does not apply and the refusal can
 * become a repair.
 */
[[nodiscard]] auto derive_day_offsets(std::string_view user_text) -> std::optional<std::string>;

[[nodiscard]] auto reconcile(const std::vector<FieldCandidates>& candidates,
                             const std::map<std::string, std::string>& emitted)
    -> Reconciliation;

}  // namespace mortgage_calculator::assistant::derive

// ===========================================================================
// Implementation
// ===========================================================================

module :private;

namespace mortgage_calculator::assistant::derive {
namespace {

using sensen::logic::read_program;
using sensen::logic::read_term;
using sensen::logic::solve;
using sensen::logic::to_string;
using sensen::logic::apply_substitution;

/**
 * Operations whose bare `rate` field is PER MONTH rather than per year.
 *
 * MEASURED, NOT ASSUMED, and the measurement changed the answer. Over 20,000
 * corpus observations of (operation, field) against the percent the utterance
 * states: a field named exactly `rate` is annual/1200 on the periodic-TVM
 * family below, and annual/100 on the cash-flow family -- ComputeNpv 162/162,
 * ComputeXnpv 118/118, ComputePaybackPeriod 104/104. A single rule reading
 * "bare rate means monthly" would have been wrong on all three, and wrong in
 * the silent direction: a discount rate 12x too small still parses, still
 * grounds, and still returns a plausible NPV.
 *
 * Everything else divides by 100. That is not a default chosen for tidiness --
 * every `*_annual_rate`, `*_percent`, `*_ltv` and `annual_*` field in the
 * corpus measured /100 with no exceptions.
 *
 * `test_mortgage_derivation` re-derives this set from the corpus and fails if
 * the two disagree, so the table cannot drift away from the data that produced
 * it.
 */
constexpr std::array<std::string_view, 7> kPerMonthRateOperations{
    "ComputePayment",        "ComputePeriods",          "ComputePresentValue",
    "ComputeFutureValue",    "ComputeInterestPayment",  "ComputePrincipalPayment",
    "ComputeCumulative",
};

[[nodiscard]] auto rate_divisor(std::string_view operation, std::string_view field) -> int {
    if (field != "rate") {
        return 100;
    }
    return std::ranges::contains(kPerMonthRateOperations, operation) ? 1200 : 100;
}

/** The literal's value with its k/million suffix applied, as exact text. */
[[nodiscard]] auto literal_text(const mv::NumericLiteral& lit) -> std::string {
    std::string out;
    out.reserve(lit.text.size() + 8);
    for (const char c : lit.text) {
        if (c != ',' && c != '$' && c != '%') {
            out.push_back(c);
        }
    }
    if (out.empty()) {
        return "0";
    }
    // `scale` is carried separately so M4 is applied where it can be reasoned
    // about rather than baked into the lexeme.
    if (lit.scale == 1'000) {
        return out + " * 1000";
    }
    if (lit.scale == 1'000'000) {
        return out + " * 1000000";
    }
    return out;
}

/**
 * The maps, as Horn clauses.
 *
 * This string IS the extensibility point. A new derivation is a new clause,
 * not new C++ -- which is the whole reason the rule base is here and not in a
 * hand-written candidate loop.
 */
constexpr std::string_view kRules = R"PL(
% --- RECENCY: a later turn SUPERSEDES an earlier statement of the same kind
% A clarifying exchange arrives CONCATENATED -- "Amortize $451,300 at 5.25%
% over 20-year." and "try 6.75%" are one text by the time they reach here --
% so the rate slot legitimately derives both 0.0525 and 0.0675 and the layer
% hands the choice to the model, which returns the opening turn's. Measured on
% the 563-row holdout: 17 of the 19 multi-turn single-field failures are that,
% and not one is an arithmetic error.
%
% The missing fact is not another map. It is WHEN each literal was said. Every
% literal therefore carries its turn, and these clauses say "nothing later of
% this kind supersedes it" -- recency expressed as a constraint over the facts,
% where the graph can use it, rather than as a filter wrapped around the solver.
%
% STRICTLY GREATER, which is what keeps a single-turn utterance unchanged: two
% literals in the SAME turn do not supersede each other, so a genuinely
% ambiguous sentence still derives both and the model keeps the selector role.
% A first-turn request emits only turn 0 and no clause below can fire.
superseded_pct(T)   :- pct(_, T2), T2 > T.
superseded_money(T) :- money(_, T2), T2 > T.
superseded_extra(T) :- extra_money(_, T2), T2 > T.

% A down payment may be restated as either kind, so these supersede across
% kinds -- "20% down" revised by "$80,000 down" is one statement replacing
% another, and a per-kind rule would let both survive.
superseded_down(T)  :- down_pct(_, T2), T2 > T.
superseded_down(T)  :- down_money(_, T2), T2 > T.

% Years and months likewise: "over 20-year" revised by "redo it over 180
% months" fills the same slot from a different kind.
superseded_term(T)  :- years(_, T2), T2 > T.
superseded_term(T)  :- months(_, T2), T2 > T.

% --- a rate slot takes a stated percent over its own cadence ------------
% A percent the lexer tagged as naming a DOWN PAYMENT is excluded, because
% "20% down" is not an interest rate -- the production utterance that priced a
% 20% mortgage is the reason M0 exists, and the same exclusion applies here.
derive(F, V) :- rate_slot(F, D), pct(P, T), \+ superseded_pct(T), V is P / D.

% --- a month count from a term stated in years or months ---------------
derive(F, V) :- months_slot(F), years(Y, T), \+ superseded_term(T), V is Y * 12.
derive(F, V) :- months_slot(F), months(M, T), \+ superseded_term(T), V is M.

% --- a year count -------------------------------------------------------
derive(F, V) :- years_slot(F), years(Y, T), \+ superseded_term(T), V is Y.

% --- GRAPH DEPENDENCY: the loan is the price minus the down payment ----
% Neither operand is the field's own value; both come from the utterance and
% the subtraction is exact at scale 10^18. This is the case the model gets
% wrong by a digit -- 796,000 less 10% emitted as 714,400 rather than 716,400.
%
% THE OPERANDS ARE INDEPENDENTLY DATED, which is the point of doing this in the
% graph rather than over the latest turn alone. "redo it for $817,400" restates
% the price and says nothing about the deposit, so the new price combines with
% the ORIGINAL down payment -- a combination no single turn contains.
derive(F, V) :- loan_slot(F), money(P, Tp), \+ superseded_money(Tp),
                down_pct(D, Td), \+ superseded_down(Td), V is P - (P * D / 100).
derive(F, V) :- loan_slot(F), money(P, Tp), \+ superseded_money(Tp),
                down_money(Dn, Td), \+ superseded_down(Td), P > Dn, V is P - Dn.

% --- GRAPH DEPENDENCY: the home value IS the price ----------------------
% PMI drops off against the property, not the loan, so this field takes the
% gross figure where `loan_amount` takes the net one.
derive(F, V) :- price_slot(F), money(P, Tp), \+ superseded_money(Tp),
                down_pct(_, _), V is P.
derive(F, V) :- price_slot(F), money(P, Tp), \+ superseded_money(Tp),
                down_money(_, _), V is P.

% --- an increment is STATED, not computed -------------------------------
% "$750 more a month" names the overpayment slot in words. `extra_money` is a
% separate KIND from `money` precisely so it appears in no loan or price rule:
% without that split the 750 pairs with the opening turn's "10% down" and
% derives a $675 mortgage, which is the graph combining two real literals under
% a rule that cannot see that "more" means an increment.
derive(F, V) :- extra_slot(F), extra_money(M, T), \+ superseded_extra(T), V is M.
)PL";

/**
 * Compare two decimal TEXTS by value, not by spelling.
 *
 * The solver prints `0.0047` where the corpus writes `0.004700`; they are the
 * same number and a string comparison would call every derivation a
 * disagreement. `mv::Decimal` is the engine's own fixed-point type at scale
 * 10^18 -- the same scale sensen.logic computes in -- so parsing both sides
 * and comparing units is exact, with no tolerance to tune.
 */
/** Decimal places in a numeric literal's text, 0 if it carries none. */
[[nodiscard]] auto places_of(std::string_view text) -> int {
    const auto dot = text.find('.');
    if (dot == std::string_view::npos) {
        return 0;
    }
    return static_cast<int>(text.size() - dot - 1);
}

/** `value` rounded half-up to `places`, as text. Fixed point at
 *  mv::Decimal::kPlaces, which is 15 -- never assume 18. */
[[nodiscard]] auto round_to(std::string_view value, int places) -> std::string {
    const auto d = mv::parse_strict_decimal(value);
    // mv::Decimal::kPlaces, NOT a literal. It is 15, this function was first
    // written against a hardcoded 18, and the result was wrong by a factor of
    // a thousand -- 0.004700 rendered as 0.000005. The module exports the
    // constant for exactly this reason.
    if (!d.has_value() || places < 0 || places > mv::Decimal::kPlaces) {
        return std::string{value};
    }
    __int128 units = d->units();
    const bool negative = units < 0;
    if (negative) {
        units = -units;
    }
    __int128 scale = 1;
    for (int i = 0; i < mv::Decimal::kPlaces - places; ++i) {
        scale *= 10;
    }
    const __int128 rounded = ((units + scale / 2) / scale) * scale;
    const mv::Decimal out{negative ? -rounded : rounded};
    // Render from the units: 10^18 fixed point, trimmed to `places`.
    __int128 u = out.units();
    const bool neg = u < 0;
    if (neg) {
        u = -u;
    }
    __int128 whole = u;
    for (int i = 0; i < mv::Decimal::kPlaces; ++i) {
        whole /= 10;
    }
    __int128 frac = u;
    for (int i = 0; i < mv::Decimal::kPlaces - places; ++i) {
        frac /= 10;
    }
    __int128 whole_shifted = whole;
    for (int i = 0; i < places; ++i) {
        whole_shifted *= 10;
    }
    frac -= whole_shifted;
    std::string text = neg ? "-" : "";
    std::string w;
    if (whole == 0) {
        w = "0";
    }
    while (whole > 0) {
        w.insert(w.begin(), static_cast<char>('0' + static_cast<int>(whole % 10)));
        whole /= 10;
    }
    text += w;
    if (places > 0) {
        std::string f;
        for (int i = 0; i < places; ++i) {
            f.insert(f.begin(), static_cast<char>('0' + static_cast<int>(frac % 10)));
            frac /= 10;
        }
        text += '.' + f;
    }
    return text;
}

/**
 * Do these two decimal texts agree AT THE PRECISION THE MODEL USED?
 *
 * Not a tolerance, and the distinction matters. The corpus writes `rate` to six
 * places -- 5.5%/12 is labelled `0.004583` -- while the solver computes the
 * exact `0.004583333333333333`. Those are the same number in the contract's own
 * terms, and comparing them at scale 10^18 made the layer "correct" a field
 * that was already right.
 *
 * Measured before this existed: 31 of 257 single-candidate derivations
 * disagreed with gold, every one of them this, and served exact-match on the
 * holdout fell 397 -> 351. The arithmetic was never wrong; the comparison was.
 *
 * So the MODEL'S OWN SPELLING sets the precision of the comparison. That is not
 * a convenience -- the emitted text is what the proto carries and what the
 * Finance service will parse, so it is the contract, and a solver that silently
 * lengthens it is changing the answer rather than fixing it.
 */
[[nodiscard]] auto same_value(std::string_view derived, std::string_view emitted) -> bool {
    const auto dd = mv::parse_strict_decimal(derived);
    const auto de = mv::parse_strict_decimal(emitted);
    if (!dd.has_value() || !de.has_value()) {
        return false;
    }
    if (*dd == *de) {
        return true;
    }
    // ASYMMETRIC, DELIBERATELY. The first version rounded both to
    // min(places(derived), places(emitted)) and that is a real defect, not a
    // rounding nicety: a terminating derivation is SHORT, so `0.0047` against
    // the model's wrong `0.004667` collapsed to four places, matched, and the
    // error was masked -- the layer silently stopped correcting the very case
    // it was built for. The model's spelling is the contract, so the DERIVED
    // value is rounded TO it and never the reverse.
    const auto rounded = mv::parse_strict_decimal(round_to(derived, places_of(emitted)));
    return rounded.has_value() && *rounded == *de;
}



/**
 * The rule clauses, parsed ONCE.
 *
 * `Program` is a `std::vector<Clause>`, so the per-request facts are appended
 * to these rather than re-parsing forty lines of Prolog on every call.
 */
[[nodiscard]] auto rule_clauses() -> const sensen::logic::Program& {
    static const sensen::logic::Program rules = [] {
        auto parsed = read_program(std::string{kRules});
        return parsed.has_value() ? std::move(*parsed) : sensen::logic::Program{};
    }();
    return rules;
}

/**
 * The single goal `derive(F, V)`, parsed once and solved once.
 *
 * Asking per field cost one `solve` per DECLARED field -- sixteen on
 * ComputeClosingCosts and ComputeRentVsBuy, of which fourteen derive nothing,
 * which is why the cost tracked field count rather than utterance length.
 * Leaving F unbound returns every (field, value) pair the rule base admits in
 * one search. The rules are unchanged; only the number of searches is.
 */
[[nodiscard]] auto all_pairs_goal() -> const sensen::logic::Term& {
    static const sensen::logic::Term goal = *read_term("derive(F, V).");
    return goal;
}

/**
 * Append every numeric literal in `text` as a fact tagged with `turn`.
 *
 * The turn is what the recency clauses in `kRules` compare. It is an ordinary
 * argument rather than a separate predicate per turn, so the rule base stays
 * the same size however many turns arrive.
 */
auto emit_literal_facts(std::string_view text, int turn, std::string& facts) -> void {
    const std::string tail = ", " + std::to_string(turn) + ").\n";
    for (const auto& lit : mv::lex_numeric_literals(text)) {
        const std::string v = literal_text(lit);
        switch (lit.tag) {
            case mv::LiteralTag::Percent:
                facts += (lit.names_down_payment ? "down_pct(" : "pct(") + v + tail;
                break;
            case mv::LiteralTag::Money:
                // Three kinds, not two. An increment is neither a price nor a
                // deposit, and giving it its own predicate is what keeps it out
                // of the loan and price rules -- see `names_increment`.
                facts += (lit.names_down_payment ? "down_money("
                          : lit.names_increment  ? "extra_money("
                                                 : "money(") + v + tail;
                break;
            case mv::LiteralTag::Years:  facts += "years(" + v + tail; break;
            case mv::LiteralTag::Months: facts += "months(" + v + tail; break;
            case mv::LiteralTag::Days:   facts += "days(" + v + tail; break;
            case mv::LiteralTag::Untagged: break;
        }
    }
}

}  // namespace

/** One turn is the degenerate exchange: everything is turn 0, so no recency
 *  clause in `kRules` can fire and the derivation is what it always was. */
auto derive_candidates(std::string_view operation, std::string_view user_text)
    -> std::vector<FieldCandidates> {
    return derive_candidates_in_turns(operation, user_text, {});
}

auto derive_candidates_in_turns(std::string_view operation, std::string_view earlier,
                                std::string_view latest) -> std::vector<FieldCandidates> {
    std::vector<FieldCandidates> out;
    const auto fields = mv::fields_of(operation);
    if (fields.empty()) {
        return out;
    }

    std::string facts;
    emit_literal_facts(earlier, 0, facts);
    if (!latest.empty()) {
        emit_literal_facts(latest, 1, facts);
    }

    for (const auto& f : fields) {
        const std::string name{f.field};
        switch (mv::classify_slot(f.field)) {
            case mv::SlotKind::Rate:
                facts += "rate_slot(" + name + ", "
                       + std::to_string(rate_divisor(operation, f.field)) + ").\n";
                break;
            case mv::SlotKind::MonthCount: facts += "months_slot(" + name + ").\n"; break;
            case mv::SlotKind::YearCount:  facts += "years_slot(" + name + ").\n"; break;
            case mv::SlotKind::Money:
                // These three sets are the rule base's whole vocabulary of
                // money ROLES, and membership is by exact name. A money field
                // outside all three derives NOTHING -- there is no
                // unclassified-slot error to trip, the goal simply matches no
                // clause and the graph falls through in silence. So each set
                // has to be swept as a CLASS whenever the proto grows a
                // synonym, which is how `property_price` sat outside
                // kPurchasePriceFields while meaning exactly what
                // `original_home_value` means.
                //
                // kPurchasePriceFields is the GROSS purchase price: with a
                // deposit stated, `loan_amount` takes the net figure and these
                // keep the gross one. PMI drops off against the property, so a
                // price field missing from here is a PMI schedule measured
                // against the wrong number -- and on a revision turn, against a
                // stale one.
                //
                // `property_value` (ComputeRefinance) is deliberately ABSENT.
                // It is an appraised value rather than a purchase price, and a
                // refinance utterance states no deposit, so the price rule's
                // own `down_pct`/`down_money` premise can never hold for it.
                // Listing it would add a clause that cannot fire, which reads
                // exactly like coverage.
                static constexpr std::array<std::string_view, 2> kLoanSlotFields{
                    "loan_amount", "present_value"};
                static constexpr std::array<std::string_view, 3> kPurchasePriceFields{
                    "original_home_value", "home_price", "property_price"};
                static constexpr std::array<std::string_view, 2> kExtraSlotFields{
                    "monthly_overpayment", "extra_monthly_payment"};

                if (std::ranges::contains(kLoanSlotFields, name)) {
                    facts += "loan_slot(" + name + ").\n";
                } else if (std::ranges::contains(kPurchasePriceFields, name)) {
                    facts += "price_slot(" + name + ").\n";
                } else if (std::ranges::contains(kExtraSlotFields, name)) {
                    facts += "extra_slot(" + name + ").\n";
                }
                break;
            default: break;
        }
    }

    auto facts_prog = read_program(facts);
    if (!facts_prog) {
        return out;   // a malformed fact base derives nothing; grounding still runs
    }
    const auto& rules = rule_clauses();
    sensen::logic::Program prog = std::move(*facts_prog);
    prog.insert(prog.end(), rules.begin(), rules.end());

    const auto& goal = all_pairs_goal();
    auto res = solve(prog, {goal}, 1024);
    if (!res) {
        return out;
    }

    // `derive(loan_amount, 716400.0)` -> ("loan_amount", "716400.0"). Rendering
    // the substituted goal keeps the number in the solver's own exact spelling.
    std::map<std::string, std::vector<std::string>> grouped;
    for (const auto& sub : *res) {
        const std::string rendered = to_string(apply_substitution(sub, goal));
        const auto open_paren = rendered.find('(');
        const auto comma = rendered.rfind(", ");
        const auto close_paren = rendered.rfind(')');
        if (open_paren == std::string::npos || comma == std::string::npos ||
            close_paren == std::string::npos || comma <= open_paren + 1 ||
            close_paren <= comma + 2) {
            continue;
        }
        std::string field = rendered.substr(open_paren + 1, comma - open_paren - 1);
        std::string value = rendered.substr(comma + 2, close_paren - comma - 2);
        auto& values = grouped[std::move(field)];
        if (std::ranges::find(values, value) == values.end()) {
            values.push_back(std::move(value));
        }
    }

    // Emitted in the proto's field order, not the map's, so the output is
    // byte-identical to the per-field loop this replaced.
    for (const auto& f : fields) {
        const auto it = grouped.find(std::string{f.field});
        if (it != grouped.end() && !it->second.empty()) {
            out.push_back({std::string{f.field}, it->second});
        }
    }

    return out;
}


auto reconcile(const std::vector<FieldCandidates>& candidates,
               const std::map<std::string, std::string>& emitted) -> Reconciliation {
    Reconciliation r;

    // ONE LITERAL CANNOT FILL TWO SLOTS, and forgetting that would have made
    // this layer actively harmful. On "Amortize the loan on a $796,000
    // property, a 10% deposit, 5.97%, 30-year." both `annual_rate` and
    // `pmi_annual_rate` derive 0.0597 from the single stated percent -- the
    // utterance says nothing about PMI, whose gold is 0.0000. Replacing on a
    // single candidate alone would have overwritten a CORRECT 0.0000 with a
    // wrong 0.0597, turning a model that got the field right into one that
    // does not. Found by printing the candidates rather than by reasoning.
    //
    // The resolution is the selector role again: where several fields claim
    // the same value, the model's own assignment says which one meant it. A
    // field already holding that value keeps it; the others are left alone,
    // because nothing in the utterance distinguishes them.
    // ONLY A FIELD ACTUALLY IN CONTENTION MAY CLAIM A LITERAL, and counting
    // every candidate here was a real inconsistency: the tally was taken over
    // ALL candidates while the skip decisions are made PER FIELD below, so a
    // field that is about to be skipped anyway still got a vote -- and its vote
    // could veto the one replacement this layer existed to make.
    //
    // Measured. On "Amortize $451,300 at 5.25% over 20-year." revised by "try
    // 6.75%", `annual_rate` and `pmi_annual_rate` both derive the single value
    // 0.0675. The model emitted `pmi_annual_rate` as the convention 0.0000 --
    // which the zero rule below skips regardless, because a convention zero is
    // a statement that the field does not apply -- yet it counted as a second
    // claimant, `claimed` reached 2, and the CORRECT 0.0675 was refused as
    // contested. The whole recency rule produced zero behavioural change
    // because of this line, which is how it was found.
    //
    // A field the model left out cannot claim either: this layer never ADDS a
    // field, so an absent one can never hold the literal it would contest.
    std::map<std::string, int> claimed;
    for (const auto& c : candidates) {
        if (c.values.size() != 1) {
            continue;
        }
        const auto held = emitted.find(c.field);
        if (held == emitted.end()) {
            continue;               // never replaceable, so never a claimant
        }
        const auto held_decimal = mv::parse_strict_decimal(held->second);
        if (!held_decimal.has_value() || held_decimal->is_zero()) {
            continue;               // declined by convention, or not a scalar
        }
        ++claimed[c.values.front()];
    }

    for (const auto& c : candidates) {
        const auto it = emitted.find(c.field);
        if (it == emitted.end()) {
            continue;   // never ADD a field; the field-set contract is G2's
        }
        // THE EMITTED VALUE MUST BE A SCALAR DECIMAL for this layer to have
        // anything to say about it. A REPEATED field arrives as JSON array
        // text -- ComputeAmortizationBatch's `term_months` is "[360,360]" --
        // which parses as no decimal at all, so `same_value` answers false and
        // the field fell through to the replace path, swapping a correct
        // two-element array for the scalar 360. The rule base derives scalars;
        // an array is a different shape and `derive_day_offsets` is where this
        // module handles one. Found by sweeping the corpus with GOLD standing
        // in for the model's output, which is the only way a layer that
        // corrupts a CORRECT answer shows up at all -- the row scores
        // raw-exact and serves wrong, so no raw metric can see it.
        if (!mv::parse_strict_decimal(it->second).has_value()) {
            continue;
        }
        if (c.values.size() == 1) {
            const std::string& only_value = c.values.front();
            if (same_value(only_value, it->second)) {
                continue;                       // already right, nothing to do
            }
            // A CONVENTION ZERO IS A STATEMENT, NOT AN OMISSION, and
            // overwriting one is the only way this layer has been observed to
            // make an answer worse. On "Is it better to rent or buy over 7
            // years? ... Buy: $437,900 purchase..." ComputeRentVsBuy's
            // `loan_amount` is 0.00 -- the granular shape is in use and the
            // legacy composite is switched off -- and price-minus-down
            // derived 332300 for it, turning a correct answer into a wrong
            // one. The model saying 0 means "this shape does not apply", and
            // no derivation from the utterance can contradict that, because
            // the utterance is silent about it. Same reasoning as
            // kConventionValues: a convention value is exempt from grounding
            // precisely because it corresponds to nothing in the request.
            const auto emitted_decimal = mv::parse_strict_decimal(it->second);
            if (emitted_decimal.has_value() && emitted_decimal->is_zero()) {
                continue;
            }
            if (claimed[only_value] > 1) {
                // Contested. Another field may legitimately hold this literal,
                // and this one may legitimately hold a convention the utterance
                // never mentions. Refusing to choose is the whole rule.
                continue;
            }
            // Uncontested and genuinely different: the solver is
            // authoritative. It emits at the MODEL'S precision, because the
            // emitted text is what the proto carries -- lengthening it would
            // change the contract rather than correct the value.
            const std::string replacement = round_to(only_value, places_of(it->second));

            // THE SOLVER MUST NOT EMIT WHAT THE VERIFIER WILL REFUSE, and this
            // is the guard that was missing. A derived value goes straight back
            // through validation, so a replacement outside its slot's band does
            // not merely fail to help -- it converts an answer the model got
            // RIGHT into a refusal. Measured: a HELOC exchange answering "what
            // is your maximum LTV?" with "75%" let that 75 reach `annual_rate`,
            // and twelve perfectly-served rows came back
            // `annual_rate = 0.75 is outside this assistant's interest-rate
            // range`. G5 owns the bound and is asked here rather than copied,
            // for the reason this project already records against two tables of
            // the same thing drifting apart.
            const auto as_decimal = mv::parse_strict_decimal(replacement);
            if (!as_decimal.has_value() ||
                mv::slot_bound_violation(c.field, *as_decimal).has_value()) {
                continue;
            }
            FieldCandidates fixed = c;
            fixed.values = {replacement};
            r.replace.push_back(std::move(fixed));
            continue;
        }
        // Several readings for one field. The model's own number selects
        // between them -- picking, not computing.
        const bool matches = std::ranges::any_of(
            c.values, [&](const std::string& v) { return same_value(v, it->second); });
        if (!matches) {
            r.ambiguous.push_back(c.field);
        }
    }
    return r;
}


auto derive_day_offsets(std::string_view user_text) -> std::optional<std::string> {
    std::vector<std::string> days;
    for (const auto& lit : mv::lex_numeric_literals(user_text)) {
        if (lit.tag != mv::LiteralTag::Days) {
            continue;
        }
        std::string t;
        for (const char c : lit.text) {
            if (c != ',' && c != '$' && c != '%') {
                t.push_back(c);
            }
        }
        // A day offset is a whole number of days; "1.5 days" is not a grid
        // point this contract can carry, and half-reading one would be worse
        // than declining the whole utterance.
        if (t.empty() || t.find('.') != std::string::npos) {
            return std::nullopt;
        }
        days.push_back(std::move(t));
    }
    if (days.size() < 2) {
        // One dated flow is not a grid. A lone cash flow sits at t=0 and its
        // present value IS its face value -- the same exemption
        // sensen::check_dated_span makes, for the same reason.
        return std::nullopt;
    }
    // STRICTLY INCREASING, checked rather than assumed: an unsorted or repeated
    // grid means the reading is wrong, not that the series is unusual.
    for (std::size_t i = 1; i < days.size(); ++i) {
        if (days[i].size() < days[i - 1].size() ||
            (days[i].size() == days[i - 1].size() && days[i] <= days[i - 1])) {
            return std::nullopt;
        }
    }
    std::string out = "[0";
    for (const auto& d : days) {
        out += ',';
        out += d;
    }
    out += ']';
    return out;
}

}  // namespace mortgage_calculator::assistant::derive
