/**
 * workflow_check -- consistency queries over the mortgagefvcalculator.com
 * monetization workflow, answered by sensen's Horn-clause engine.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY THIS EXISTS. The workflow is a rule system spread across TypeScript
 * modules, route files and SQL: tiers, prices, seat caps, per-tier entitlement
 * maps, endpoint gates, webhook events. Every defect this project has recorded
 * in such a system has been a COVERAGE defect rather than a logic defect -- a
 * rule applied correctly to one member of a family and never carried to the
 * rest. "Three sweep-the-class misses in one day" is the note in the sibling
 * repository, and the fifth copy of a label space living in ANOTHER repository
 * is the same shape.
 *
 * You cannot query 2,900 lines of C++ or a dozen TypeScript modules for
 * "which tier declares a price at one interval and not the other". You can
 * query a rule base for exactly that, and the query is one line.
 *
 * The facts are GENERATED from the source (see emit_facts.mjs), never retyped.
 * A rule base maintained by hand beside the code it describes has the identical
 * drift problem it was built to detect.
 */

// `import std;` exports no MACROS: `stderr` comes from <cstdio> textually.
// <new> is the ODR anchor this tree requires per translation unit -- a <new>
// reached only transitively lands in the global module and never merges.
#include <cstdio>
#include <new>

import std;
import sensen.logic;
import sensen.logic_parser;

using sensen::logic::Program;
using sensen::logic::read_program;
using sensen::logic::read_term;
using sensen::logic::solve;
using sensen::logic::Substitution;
using sensen::logic::Term;
using sensen::logic::apply_substitution;
using sensen::logic::to_string;

namespace {

int g_violations = 0;
int g_checks = 0;

/// One consistency query. `expect_empty` is the whole point: these queries are
/// written to find VIOLATIONS, so a healthy system answers each with no
/// solutions. A query that returns rows is a defect report.
struct Check {
    std::string name;
    std::string goal;      ///< Prolog goal text
    std::string why;       ///< what a solution MEANS
};

auto run_check(const Program& prog, const Check& c) -> void {
    ++g_checks;
    auto parsed = read_term(c.goal + ".");
    if (!parsed) {
        std::println("[ERROR] {}: could not parse goal: {}", c.name, c.goal);
        ++g_violations;
        return;
    }
    auto res = solve(prog, {*parsed}, 64);
    if (!res) {
        std::println("[ERROR] {}: solver refused the query", c.name);
        ++g_violations;
        return;
    }
    const auto& sols = *res;
    if (sols.empty()) {
        std::println("  [OK]   {}", c.name);
        return;
    }
    std::println("  [FAIL] {} -- {} ({} case(s))", c.name, c.why, sols.size());
    std::size_t shown = 0;
    for (const auto& s : sols) {
        if (shown++ >= 8) { std::println("           ... and {} more", sols.size() - 8); break; }
        std::println("           {}", to_string(apply_substitution(s, *parsed)));
    }
    g_violations += static_cast<int>(sols.size());
}

/// Rules that DEFINE what a violation is. Facts come from the generator; these
/// are the questions asked of them.
constexpr std::string_view kRules = R"PL(
% --- a tier must be priced at BOTH intervals -----------------------------
% A tier sold monthly but not annually (or the reverse) is a checkout that
% silently falls back to another plan.
unpriced(Tier, Interval) :-
    tier(Tier, _),
    interval(Interval),
    \+ paid_tier_excluded(Tier),
    \+ price_at(Tier, Interval).

% --- an annual price must beat twelve monthlies -------------------------
% Guards the typo that makes the annual plan cost MORE, which no test that
% only checks "a price exists" can see.
annual_not_cheaper(Tier) :-
    price_cents(Tier, year, Y),
    price_cents(Tier, month, M),
    Twelve is M * 12,
    Y >= Twelve.

% --- entitlements must be MONOTONIC in tier rank ------------------------
% A higher-ranked tier granting LESS than a lower one. This is the
% sweep-the-class defect: a new tier inserted in the middle and one map
% updated out of five.
entitlement_inversion(Map, Lower, Higher) :-
    tier(Lower, RL),
    tier(Higher, RH),
    RH > RL,
    limit(Map, Lower, VL),
    limit(Map, Higher, VH),
    VH < VL.

% --- every tier must appear in EVERY entitlement map --------------------
% NOTE ON THE HELPERS BELOW. Each exists to make the negated goal GROUND at
% the moment it is called. This engine REFUSES a negation over an unbound
% variable rather than answering unsoundly -- standard Prolog would silently
% return a wrong answer here. Wrapping the existential in its own predicate
% means `\+ has_limit(scenario, pro)` is a ground question, which is the only
% arrangement under which negation as failure is sound.
has_limit(Map, Tier) :- limit(Map, Tier, _).
gated(E) :- endpoint_tier(E, _).
known_tier(T) :- tier(T, _).

missing_from_map(Map, Tier) :-
    tier(Tier, _),
    declared_map(Map),
    \+ has_limit(Map, Tier).

% --- an endpoint must declare a gate ------------------------------------
ungated_endpoint(E) :-
    endpoint(E),
    \+ gated(E).

% --- an endpoint may not require a tier that does not exist -------------
endpoint_unknown_tier(E, T) :-
    endpoint_tier(E, T),
    \+ known_tier(T).

% --- a webhook event must be both emitted and declared ------------------
event_emitted_undeclared(E) :- emits_event(E), \+ declared_event(E).
event_declared_unemitted(E) :- declared_event(E), \+ emits_event(E).
)PL";

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::println(stderr, "usage: workflow_check <facts.pl>");
        return 2;
    }
    std::ifstream in{argv[1]};
    if (!in) { std::println(stderr, "cannot open {}", argv[1]); return 2; }
    std::stringstream ss; ss << in.rdbuf();

    std::string text = ss.str();
    text += std::string{kRules};

    auto prog = read_program(text);
    if (!prog) {
        std::println(stderr, "parse error: {}", sensen::logic::to_string(prog.error()));
        return 2;
    }

    std::println("workflow_check -- {} clauses loaded", prog->size());
    std::println("");

    const std::vector<Check> checks{
        {"every tier priced at both intervals", "unpriced(T, I)",
         "a tier is missing a price at one interval"},
        {"annual is cheaper than 12 monthlies", "annual_not_cheaper(T)",
         "an annual price is not below twelve monthly payments"},
        {"entitlements monotonic in tier rank", "entitlement_inversion(M, L, H)",
         "a higher tier grants LESS than a lower tier"},
        {"every tier in every entitlement map", "missing_from_map(M, T)",
         "a tier is absent from an entitlement map"},
        {"every endpoint declares a gate", "ungated_endpoint(E)",
         "an endpoint has no minimum tier"},
        {"endpoint gates name real tiers", "endpoint_unknown_tier(E, T)",
         "an endpoint requires a tier that does not exist"},
        {"emitted events are declared", "event_emitted_undeclared(E)",
         "code emits an event the webhook layer does not declare"},
        {"declared events are emitted", "event_declared_unemitted(E)",
         "the webhook layer offers an event nothing emits"},
    };

    for (const auto& c : checks) run_check(*prog, c);

    std::println("");
    std::println("=== {} checks, {} violation(s) ===", g_checks, g_violations);
    return g_violations == 0 ? 0 : 1;
}
