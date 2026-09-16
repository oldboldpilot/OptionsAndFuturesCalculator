/**
 * The PMI scenario, explained in words by the SAME solver that reasons about it.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY THIS IS HORN CLAUSES AND NOT A MODEL, given the calculation is entirely
 * deterministic. A language model narrating a deterministic result reintroduces
 * this project's documented dangerous failure in prose: `present_value =
 * 304000.00` on a 495,000 utterance parsed, satisfied every bound, named a real
 * field and priced a different loan. "Your PMI ends in month 137" when the
 * schedule says 142 is that same defect, and it is HARDER to catch, because
 * fluent sentences read as authority. Nothing here can do it: a number reaches
 * the text only as a clause argument, so the prose cannot contain a figure the
 * engine did not compute. That is the grounding gate inverted -- G1..G5 refuse a
 * model value not derivable from the USER'S INPUT; this refuses, by
 * construction, a narrative value not derivable from the ENGINE'S OUTPUT.
 *
 * WHY THE ENGLISH IS NOT IN THE RULE BASE. The clauses decide WHAT is worth
 * saying and carry the figures; `render` turns one atom into one sentence. Two
 * reasons, both learned elsewhere in this tree: a rule base full of sentence
 * fragments stops being readable as logic, and a renderer that can only
 * interpolate arguments cannot retype a number, which is exactly the property
 * that makes the paragraph above true.
 *
 * WHY THE OUTPUT IS ITEMISED RATHER THAN A PARAGRAPH. Each point carries a
 * stable TOPIC beside its sentence. A caller can then render bullets, suppress
 * one line, group two, or re-order for a narrow screen without parsing English
 * back out of a blob -- and a test can assert that the termination point EXISTS
 * rather than that some sentence contains a substring, which is the difference
 * between checking a fact and checking a wording. The topics are the layer's
 * contract; the sentences are its presentation, and only one of those is stable.
 *
 * WHAT IT DELIBERATELY DOES NOT DO. It does not compute. `pmi_ends` is a fact
 * the amortisation schedule already knows; deriving it here would give two
 * answers to one question and no way to tell which is wrong -- the same reason
 * the mortgage assistant names an operation and lets the Finance service run it.
 */
module;

#include <new>

export module mortgage_explanation;

import std;
import sensen.logic;
import sensen.logic_parser;

export namespace mortgage_calculator::assistant::explain {

/**
 * The facts a PMI scenario is explained FROM. Every one is produced by the
 * amortisation run; none is inferred here.
 *
 * Money arrives as decimal STRINGS for the reason `finance.proto` carries it
 * that way -- rounding a premium to double and multiplying it over 360 periods
 * is how a total drifts from the schedule it claims to summarise.
 */
struct PmiFacts {
    /** Monthly premium. "0" means the loan carries no PMI at all. */
    std::string monthly_premium = "0";
    /** Month the premium stopped. 0 means it never did within the term. */
    int ends_month = 0;
    /** Total premium paid across the schedule. */
    std::string total_paid = "0";
    /** The terminating LTV, as a fraction: "0.80" for the usual 80%. */
    std::string threshold_ltv = "0.80";
    /** The value the LTV is measured against -- purchase price, or the
     *  appreciated value when the caller selected that basis. */
    std::string basis_value = "0";
    /** Scheduled term, used only to say "the whole term" meaningfully. */
    int term_months = 0;
    /** Monthly overpayment, if any. */
    std::string monthly_overpayment = "0";
    /** What `ends_month` would have been with no overpayment. Lets the layer
     *  say what the extra payment BOUGHT, which is the question people
     *  actually ask and the one a single schedule cannot answer. */
    int baseline_ends_month = 0;
};

/**
 * One itemised point: what it is about, and how to say it.
 *
 * `topic` is the stable half. It is what a caller keys on, so it is a
 * dotted identifier rather than a sentence fragment and it does not change
 * when the wording improves.
 */
struct ExplanationPoint {
    std::string topic;
    std::string text;

    // --- the itemisation, so a sentence can be traced to the figure it is about
    //
    // A paragraph of correct sentences is still unauditable if the reader
    // cannot tell WHICH output each one describes. These three carry that:
    // `item` numbers the point for display ("3."), and `field`/`value` name
    // the exact output line and the exact figure it was built from.
    //
    // `value` is the figure VERBATIM, not re-rendered from `text`. That makes
    // the pair checkable: a caller -- or a test -- can assert the number in
    // the sentence is the number in the result, which is the property that
    // stops prose and figures drifting apart. Reading it back out of the
    // English would only prove the sentence agrees with itself.
    int item = 0;              ///< 1-based display ordinal; 0 = not itemised
    std::string field;         ///< machine name of the output line
    std::string value;         ///< that line's figure, exactly as computed
};

/** Topic identifiers, named so callers and tests do not spell them by hand. */
inline constexpr std::string_view kTopicNoPmi        = "pmi.none";
inline constexpr std::string_view kTopicTermination  = "pmi.termination";
inline constexpr std::string_view kTopicFullTerm     = "pmi.full_term";
inline constexpr std::string_view kTopicCost         = "pmi.cost";
inline constexpr std::string_view kTopicOverpayment  = "pmi.overpayment";

/**
 * The itemised points for a scenario, in a fixed order.
 *
 * Empty only when the facts say nothing worth saying, which is itself a
 * supported answer rather than a failure.
 */
[[nodiscard]] auto explain_pmi(const PmiFacts& facts) -> std::vector<ExplanationPoint>;

// ---------------------------------------------------------------------------
// The OUTPUT-DRIVEN explainer
// ---------------------------------------------------------------------------
//
// `explain_pmi` above is hand-written: one struct, one rule base, one
// vocabulary, all specific to mortgage insurance. That does not scale past the
// feature it was written for -- rental cash flow, the interest deduction, the
// rental tax position and affordability would each need their own copy, and
// each copy is another place for a sentence to drift from the number it
// describes.
//
// This explains a RESULT instead. The caller hands over the fields an
// operation actually produced and gets back itemised points covering the ones
// that carry data. Nothing here knows what a rental is.
//
// A FIELD WITH NO DATA IS NOT EXPLAINED, and deciding what "no data" means is
// the whole of the design:
//
//   * absent        -- the operation did not produce the field at all.
//   * empty         -- produced, but with no value in it.
//   * zero, where zero MEANS absent.
//
// That last one is the case worth stating, because a zero is not self-
// describing. `heloc_drawn_amount = 0` says "there is no HELOC in this
// scenario" and a sentence about it is noise; `total_profit = 0` says the
// investor breaks exactly even, which is the finding. Both are the literal
// `0`. So a field declares what ITS zero means and the caller cannot forget
// to, because `ZeroMeaning` has no default that is right for both.
//
// This mirrors a rule the project already relies on rather than inventing one:
// `kConventionValues` exempts an INPUT zero from grounding precisely because a
// zero there means "not this shape". An input zero is `Absent`. A computed
// output zero is `Real`.

/** What a literal zero means in a given field. There is deliberately no
 *  default: the two readings produce different explanations and guessing
 *  between them is how a real finding gets dropped. */
enum class ZeroMeaning : std::uint8_t {
    Absent,   ///< zero means "not part of this scenario" -- say nothing
    Real,     ///< zero is the computed answer -- say it
};

/** How to place a figure in a sentence. It never changes the figure. */
enum class Unit : std::uint8_t {
    Money, Rate, Share, Months, Years, Count, Plain,
};

/**
 * One field of an operation's result.
 *
 * `value` is carried as the string the engine produced. It is never parsed
 * into a double and re-rendered -- that is a second place for a number to be
 * reshaped, and this module's entire claim is that it cannot reshape one.
 * Sign and magnitude are read by comparing characters, not by arithmetic.
 */
struct ResultField {
    std::string name;                               ///< machine name, e.g. "total_profit"
    std::string label;                              ///< human label, e.g. "total profit"
    std::string value;                              ///< exactly as computed
    Unit unit = Unit::Money;
    ZeroMeaning zero = ZeroMeaning::Absent;
    bool produced = true;                           ///< false = the operation did not emit it
};

/** The result to explain. `operation` prefixes every topic, so points from two
 *  operations never collide in a caller keying on topic. */
struct ResultView {
    std::string operation;
    std::vector<ResultField> fields;

    /**
     * `{dependent, prerequisite}`: explain `dependent` only if `prerequisite`
     * carries data.
     *
     * WITHOUT THIS, DROPPING EMPTY COLUMNS IS NOT ENOUGH TO STOP A FALSE
     * SENTENCE. A rental with no HELOC still carries a `heloc_annual_rate`,
     * and a rate is a perfectly ordinary non-zero number -- so field-by-field
     * filtering keeps it and the explanation announces the interest rate of a
     * loan that does not exist. The prerequisite is `heloc_drawn_amount`, and
     * it is absent, which is the only thing that can settle it.
     *
     * Declared by the caller rather than known here, so this module still
     * knows nothing about rentals -- and resolved through the Horn-clause
     * engine rather than a loop, because the relation is TRANSITIVE: a field
     * behind a suppressed field is itself suppressed, and that is a closure,
     * not a check.
     */
    std::vector<std::pair<std::string, std::string>> depends_on;
};

/**
 * Itemised points for the fields that carry data, in the order given.
 *
 * Order is the CALLER's, preserved exactly: the result struct already lists its
 * fields in the order a reader wants them, and re-sorting here would put this
 * module in the business of deciding what matters.
 */
[[nodiscard]] auto explain_result(const ResultView& view) -> std::vector<ExplanationPoint>;

/** The fields that would be explained, without rendering them. Exposed because
 *  it is the half worth asserting directly: a test for "the empty columns are
 *  skipped" should not have to read English to find out. */
[[nodiscard]] auto carrying_data(const ResultView& view) -> std::vector<std::string>;


}  // namespace mortgage_calculator::assistant::explain

namespace mortgage_calculator::assistant::explain {
namespace detail {

/**
 * The rule base. It decides WHAT to say; it never decides a number.
 *
 * Each clause's head is `say/1` wrapping a flat term whose arguments are the
 * figures the sentence needs. Flat on purpose: `render` splits one level of
 * arguments, and a nested term would need a parser, which is a second place for
 * a number to be reshaped.
 */
constexpr std::string_view kRules = R"PL(
% --- no PMI at all -------------------------------------------------------
% Said explicitly rather than by silence: "nothing about PMI" and "no PMI on
% this loan" are different answers, and only one of them is reassuring.
say(no_pmi) :- pmi_monthly(0).

% --- PMI that terminates -------------------------------------------------
% The threshold is carried as a PERCENT for the sentence and as a fraction in
% the facts, converted HERE so the renderer never does arithmetic. A renderer
% that multiplies by 100 is a renderer that can be wrong about a number.
say(pmi_until(X, M, Pct, V)) :-
    pmi_monthly(X), X > 0,
    pmi_ends(M), M > 0,
    threshold(T), Pct is T * 100,
    basis_value(V).

% --- PMI that never terminates -------------------------------------------
% Reached when the balance never falls to the threshold within the term, which
% is the honest outcome for a small deposit on a long schedule.
say(pmi_whole_term(X, N)) :-
    pmi_monthly(X), X > 0,
    pmi_ends(0),
    term(N).

% --- what it cost --------------------------------------------------------
say(pmi_cost(C)) :- pmi_monthly(X), X > 0, pmi_total(C).

% --- what the overpayment bought -----------------------------------------
% Only stated when it actually moved the date. An extra payment that changes
% nothing is worth no sentence, and claiming a saving of zero months reads as
% a defect in the calculator rather than a property of the scenario.
say(pmi_pulled_forward(S, B, A)) :-
    overpayment(O), O > 0,
    baseline_ends(B), B > 0,
    pmi_ends(A), A > 0,
    B > A,
    S is B - A.
)PL";

/**
 * The atom vocabulary: every term `say/1` can wrap, with the topic it becomes
 * and where it sits in the itemised list.
 *
 * ONE table rather than three parallel ones. Rank, topic and arity drifting
 * apart is the defect this repository has recorded against hand-maintained
 * lists more than once, and a point whose rank says "third" while its topic
 * says something else is not a thing that can be reasoned about. `arity` is
 * here so a clause that grows an argument fails the vocabulary test rather
 * than silently rendering nothing.
 */
struct PointSpec {
    std::string_view functor;
    std::size_t arity;
    int rank;
    std::string_view topic;
};

constexpr std::array<PointSpec, 5> kVocabulary{{
    {.functor = "no_pmi",             .arity = 0, .rank = 0, .topic = kTopicNoPmi},
    {.functor = "pmi_until",          .arity = 4, .rank = 1, .topic = kTopicTermination},
    {.functor = "pmi_whole_term",     .arity = 2, .rank = 1, .topic = kTopicFullTerm},
    {.functor = "pmi_cost",           .arity = 1, .rank = 2, .topic = kTopicCost},
    {.functor = "pmi_pulled_forward", .arity = 3, .rank = 3, .topic = kTopicOverpayment},
}};

[[nodiscard]] auto spec_for(std::string_view functor) -> const PointSpec* {
    for (const auto& s : kVocabulary) {
        if (s.functor == functor) { return &s; }
    }
    return nullptr;
}

[[nodiscard]] auto rule_clauses() -> const sensen::logic::Program& {
    static const sensen::logic::Program rules = [] {
        auto parsed = sensen::logic::read_program(std::string{kRules});
        return parsed.has_value() ? std::move(*parsed) : sensen::logic::Program{};
    }();
    return rules;
}

[[nodiscard]] auto say_goal() -> const sensen::logic::Term& {
    static const sensen::logic::Term goal = *sensen::logic::read_term("say(S).");
    return goal;
}

/** A decimal string with no trailing noise, for prose. "212.500000" is a
 *  correct premium and a bad sentence. */
[[nodiscard]] auto tidy(std::string s) -> std::string {
    if (s.find('.') == std::string::npos) { return s; }
    while (!s.empty() && s.back() == '0') { s.pop_back(); }
    if (!s.empty() && s.back() == '.') { s.pop_back(); }
    return s.empty() ? std::string{"0"} : s;
}

/** Split `f(a, b, c)` into "f" and {"a","b","c"}. One level only, by design. */
[[nodiscard]] auto split_atom(const std::string& rendered)
    -> std::pair<std::string, std::vector<std::string>> {
    const auto open = rendered.find('(');
    if (open == std::string::npos || rendered.back() != ')') {
        return {rendered, {}};
    }
    const std::string functor = rendered.substr(0, open);
    const std::string body = rendered.substr(open + 1, rendered.size() - open - 2);

    std::vector<std::string> args;
    std::string cur;
    for (const char ch : body) {
        if (ch == ',') {
            args.push_back(tidy(cur));
            cur.clear();
        } else if (ch != ' ' || !cur.empty()) {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) { args.push_back(tidy(cur)); }
    return {functor, args};
}

/** One atom, one sentence. The ONLY place English lives, and it can do nothing
 *  to a figure but place it. */
[[nodiscard]] auto render(std::string_view functor, const std::vector<std::string>& a)
    -> std::string {
    if (functor == "no_pmi") {
        return "This loan carries no mortgage insurance.";
    }
    if (functor == "pmi_until" && a.size() == 4) {
        return "Mortgage insurance of $" + a[0] + " a month stops after month " + a[1] +
               ", once the balance falls to " + a[2] + "% of the $" + a[3] +
               " it is measured against.";
    }
    if (functor == "pmi_whole_term" && a.size() == 2) {
        return "Mortgage insurance of $" + a[0] + " a month runs for the whole " + a[1] +
               "-month term: the balance never reaches the cancellation threshold.";
    }
    if (functor == "pmi_cost" && a.size() == 1) {
        return "You pay $" + a[0] + " in mortgage insurance over the life of the loan.";
    }
    if (functor == "pmi_pulled_forward" && a.size() == 3) {
        return "The extra monthly payment ends it " + a[0] + " months early, at month " + a[2] +
               " instead of month " + a[1] + ".";
    }
    return {};
}

}  // namespace detail

auto explain_pmi(const PmiFacts& facts) -> std::vector<ExplanationPoint> {
    std::string program;
    const auto fact = [&program](std::string_view name, const std::string& value) {
        program += std::string{name} + "(" + value + ").\n";
    };

    fact("pmi_monthly", facts.monthly_premium.empty() ? "0" : facts.monthly_premium);
    fact("pmi_ends", std::to_string(facts.ends_month));
    fact("pmi_total", facts.total_paid.empty() ? "0" : facts.total_paid);
    fact("threshold", facts.threshold_ltv.empty() ? "0" : facts.threshold_ltv);
    fact("basis_value", facts.basis_value.empty() ? "0" : facts.basis_value);
    fact("term", std::to_string(facts.term_months));
    fact("overpayment", facts.monthly_overpayment.empty() ? "0" : facts.monthly_overpayment);
    fact("baseline_ends", std::to_string(facts.baseline_ends_month));

    auto facts_prog = sensen::logic::read_program(program);
    if (!facts_prog) {
        // A malformed fact base explains nothing. It does NOT guess: an
        // explanation assembled around a fact that would not parse is the one
        // thing this layer exists to make impossible.
        return {};
    }

    sensen::logic::Program prog = std::move(*facts_prog);
    const auto& rules = detail::rule_clauses();
    prog.insert(prog.end(), rules.begin(), rules.end());

    const auto& goal = detail::say_goal();
    auto solutions = sensen::logic::solve(prog, {goal}, 1024);
    if (!solutions) {
        return {};
    }

    std::vector<std::pair<int, ExplanationPoint>> ranked;
    for (const auto& sub : *solutions) {
        // `say(pmi_cost(29125.00))` -> the inner atom, in the solver's own
        // exact spelling of the number.
        const std::string rendered =
            sensen::logic::to_string(sensen::logic::apply_substitution(sub, goal));
        const auto open = rendered.find('(');
        if (open == std::string::npos || rendered.back() != ')') { continue; }
        const std::string inner = rendered.substr(open + 1, rendered.size() - open - 2);

        const auto [functor, args] = detail::split_atom(inner);
        const auto* spec = detail::spec_for(functor);
        // A clause whose atom is not in the vocabulary is DROPPED rather than
        // appended: adding a rule without deciding what its point is called and
        // where it belongs should produce nothing, not an unlabelled sentence
        // at an arbitrary position.
        if (spec == nullptr || args.size() != spec->arity) { continue; }

        std::string sentence = detail::render(functor, args);
        if (sentence.empty()) { continue; }

        ExplanationPoint point{.topic = std::string{spec->topic}, .text = std::move(sentence)};
        if (std::ranges::none_of(ranked, [&point](const auto& p) {
                return p.second.topic == point.topic && p.second.text == point.text;
            })) {
            ranked.emplace_back(spec->rank, std::move(point));
        }
    }

    std::ranges::stable_sort(ranked, {}, &std::pair<int, ExplanationPoint>::first);

    std::vector<ExplanationPoint> out;
    out.reserve(ranked.size());
    // Numbered AFTER the sort, deliberately: the ordinal is what the reader
    // sees beside the sentence, so numbering before ordering would print
    // "1, 3, 2" and make the itemisation worse than none.
    for (auto& [r, p] : ranked) {
        p.item = static_cast<int>(out.size()) + 1;
        out.push_back(std::move(p));
    }
    return out;
}

namespace detail {

/** Zero by INSPECTION, not by arithmetic: "0", "-0.00", "0.000000" are all
 *  zero and none of them is parsed. Parsing would re-render, and re-rendering
 *  a figure is the one thing this module must not do. */
[[nodiscard]] auto is_zero(std::string_view v) -> bool {
    bool saw_digit = false;
    for (const char ch : v) {
        if (ch == '-' || ch == '+' || ch == '.' || ch == ' ') { continue; }
        if (ch < '0' || ch > '9') { return false; }
        saw_digit = true;
        if (ch != '0') { return false; }
    }
    return saw_digit;
}

[[nodiscard]] auto is_negative(std::string_view v) -> bool {
    return !v.empty() && v.front() == '-' && !is_zero(v);
}

/** A field carries data unless it was never produced, is empty, or is a zero
 *  whose zero means absence. */
[[nodiscard]] auto has_data(const ResultField& f) -> bool {
    if (!f.produced || f.value.empty()) { return false; }
    return !(is_zero(f.value) && f.zero == ZeroMeaning::Absent);
}

/** A term the Horn engine will accept as an atom: lower-case, underscores. */
[[nodiscard]] auto atomise(std::string_view name) -> std::string {
    std::string out;
    out.reserve(name.size());
    for (const char ch : name) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_') {
            out.push_back(ch);
        } else if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back('_');
        }
    }
    if (out.empty() || (out.front() >= '0' && out.front() <= '9')) { out.insert(out.begin(), 'f'); }
    return out;
}

/**
 * Transitive suppression, in the rule base rather than around it.
 *
 * `absent/1` is asserted for every field that carries no data. A field is
 * suppressed when its prerequisite is absent OR when its prerequisite is
 * itself suppressed -- the second clause is the recursive one, and it is why
 * this is a solver call and not a loop over pairs.
 */
constexpr std::string_view kSuppressionRules = R"PL(
suppressed(F) :- depends(F, P), absent(P).
suppressed(F) :- depends(F, P), suppressed(P).
)PL";

/** The prose. The ONLY place English lives, and it can do nothing to a figure
 *  but place it and name its unit. */
[[nodiscard]] auto say_field(const ResultField& f) -> std::string {
    const std::string v = tidy(f.value);
    switch (f.unit) {
        case Unit::Money:
            return is_negative(v)
                       ? f.label + ": a shortfall of $" + std::string{std::string_view{v}.substr(1)} + "."
                       : f.label + ": $" + v + ".";
        case Unit::Rate:   return f.label + ": " + v + " a year.";
        case Unit::Share:  return f.label + ": " + v + " of the total.";
        case Unit::Months: return f.label + ": " + v + " months.";
        case Unit::Years:  return f.label + ": " + v + " years.";
        case Unit::Count:  return f.label + ": " + v + ".";
        case Unit::Plain:  return f.label + ": " + v + ".";
    }
    return {};
}

}  // namespace detail

auto carrying_data(const ResultView& view) -> std::vector<std::string> {
    // Stage 1: presence. A field with no data cannot be a prerequisite for
    // anything, so this has to settle before suppression is asked about.
    std::vector<std::string> absent;
    for (const auto& f : view.fields) {
        if (!detail::has_data(f)) { absent.push_back(detail::atomise(f.name)); }
    }

    // Stage 2: suppression, through the solver. Skipped entirely when the
    // caller declared no dependencies -- an empty relation can suppress
    // nothing, and building a program to prove that wastes the call.
    std::vector<std::string> suppressed;
    if (!view.depends_on.empty()) {
        std::string program;
        for (const auto& a : absent) { program += "absent(" + a + ").\n"; }
        for (const auto& [dep, pre] : view.depends_on) {
            program += "depends(" + detail::atomise(dep) + ", " + detail::atomise(pre) + ").\n";
        }
        auto facts = sensen::logic::read_program(program);
        auto rules = sensen::logic::read_program(std::string{detail::kSuppressionRules});
        if (facts && rules) {
            sensen::logic::Program prog = std::move(*facts);
            prog.insert(prog.end(), rules->begin(), rules->end());

            // The trailing period is REQUIRED -- `read_term` parses a clause,
            // and without it the goal does not parse, `solve` is never
            // reached, and suppression silently does nothing. That failure is
            // invisible from the outside: "nothing was suppressed" and "the
            // goal would not parse" produce the identical empty list.
            auto goal_term = sensen::logic::read_term("suppressed(F).");
            if (goal_term) {
                if (auto sols = sensen::logic::solve(prog, {*goal_term}, 1024)) {
                    for (const auto& sub : *sols) {
                        const std::string r = sensen::logic::to_string(
                            sensen::logic::apply_substitution(sub, *goal_term));
                        const auto open = r.find('(');
                        if (open == std::string::npos || r.back() != ')') { continue; }
                        suppressed.push_back(r.substr(open + 1, r.size() - open - 2));
                    }
                }
            }
        }
        // A rule base that will not parse suppresses NOTHING. It does not fall
        // back to explaining everything quietly -- it explains everything
        // loudly, which is the direction a reader can see.
    }

    std::vector<std::string> out;
    for (const auto& f : view.fields) {
        if (!detail::has_data(f)) { continue; }
        const std::string atom = detail::atomise(f.name);
        if (std::ranges::find(suppressed, atom) != suppressed.end()) { continue; }
        out.push_back(f.name);
    }
    return out;
}

auto explain_result(const ResultView& view) -> std::vector<ExplanationPoint> {
    const auto keep = carrying_data(view);

    std::vector<ExplanationPoint> out;
    out.reserve(keep.size());
    for (const auto& f : view.fields) {
        if (std::ranges::find(keep, f.name) == keep.end()) { continue; }
        std::string text = detail::say_field(f);
        if (text.empty()) { continue; }
        out.push_back(ExplanationPoint{
            .topic = view.operation.empty() ? f.name : view.operation + "." + f.name,
            .text = std::move(text),
            .item = static_cast<int>(out.size()) + 1,
            .field = f.name,
            .value = f.value});
    }
    return out;
}

}  // namespace mortgage_calculator::assistant::explain
