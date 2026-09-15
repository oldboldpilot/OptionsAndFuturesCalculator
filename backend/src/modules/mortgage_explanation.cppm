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
    for (auto& [r, p] : ranked) { out.push_back(std::move(p)); }
    return out;
}

}  // namespace mortgage_calculator::assistant::explain
