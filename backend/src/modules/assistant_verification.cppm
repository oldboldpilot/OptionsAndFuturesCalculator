// @author Olumuyiwa Oluwasanmi
//
// Mandatory, fail-closed cross-field verification of the strategy assistant's
// output, built on GP-ARA's DomainPolicy/ReasonerPolicy concepts
// (backend/sensen/src/gp_ara_interfaces.cppm).
//
// -----------------------------------------------------------------------
// WHY THIS EXISTS
//
// `assistant_service.cpp`'s per-field checks (symbol shape, asset_class
// membership, strategy catalogue membership, numeric bounds) each look at
// exactly one field in isolation. A hallucinating model produces exactly the
// failure mode that structure cannot see: fields that are each individually
// well-formed but CONTRADICT each other -- a crude-oil futures root
// ("CL") tagged asset_class=EQUITY, a futures-only strategy paired with a
// crypto symbol, a calendar spread asked to live inside a single
// expiration_days field. This module is the mandatory gate that catches
// those contradictions before a single field of the model's output reaches
// `calculator.assistant.StrategyParams`.
//
// -----------------------------------------------------------------------
// WHY THERE IS NO Z3 HERE
//
// `backend/CMakeLists.txt` (search "sensen_slim -- the part of sensen this
// engine actually uses") is explicit: the only consumer of z3++ anywhere in
// sensen is gp_ara_agent's Z3Reasoner, sensen_slim excludes it, and `ldd` on
// the built engine confirms no libz3 is linked into this binary. Z3 is
// therefore genuinely unavailable here, not merely unused by convention.
//
// `RuleBasedReasoner` below is the reasoner-agnostic answer: it satisfies
// `sensen::gp_ara::ReasonerPolicy` exactly like Z3Reasoner would (same
// `ContextType`, same `prove_safety`/`prove_goal` signatures returning
// `std::expected<bool, ReasonerError>`), but it decides each of the boolean
// facts `AssistantParamsDomain::translate()` computes by direct evaluation
// rather than by discharging an SMT formula. Nothing here needs a solver:
// every constraint is a closed-form comparison over five short strings/ints,
// not a system requiring search. Should a real SMT backend become available
// to this binary later, `Z3Reasoner` is a drop-in replacement for
// `RuleBasedReasoner` against the exact same `AssistantParamsDomain` -- nothing
// about the domain models an SMT-LIB string, so nothing here needs to change.
//
// -----------------------------------------------------------------------
// THE TRI-STATE
//
// `sensen::gp_ara::ReasonerErrorCode::Indeterminate` is preserved end to end,
// never collapsed to a boolean:
//   - Proven    (`std::expected<bool,...>` holding `true`)  -> the caller may
//     populate params.
//   - Unsafe    (`std::expected<bool,...>` holding `false`) -> refuse, with
//     the specific `VerificationFacts::detail` naming which rule fired.
//   - Indeterminate (`std::unexpected{.code = Indeterminate, ...}`) -> refuse.
//     The one case this module puts into that bucket is a `strategy_catalogue`
//     entry whose `category` this file's rule table has never seen (see
//     `translate()`'s final `else` branch below) -- a genuine "the reasoner
//     was never taught this," as opposed to a definite "no." Refusal, not a
//     clarification, because the gap is between this file and
//     `strategy_catalogue.cppm`, not something the trader said that a
//     follow-up question could resolve. `verify_assistant_params()` never
//     forwards an Indeterminate verdict as anything other than a refusal --
//     see its own comment for why that is the correct default and not merely
//     the convenient one.
//
// A symbol root that is genuinely ambiguous across asset classes (checked,
// not assumed -- see `kAmbiguousRootDetails` below for which of the five
// supported futures roots actually collide with a real, currently-listed
// equity ticker today: "ES"/Eversource Energy and "CL"/Colgate-Palmolive
// both do; "NQ", "GC" and "ZB" do not) is deliberately NOT modelled as this
// module's own Indeterminate. Resolving which asset class a trader meant
// needs information this static, five-field reasoner has no access to --
// either live market data (`assistant_service.cpp`'s `probe_symbol()`,
// Resolved / Unknown / AssetClassMismatch -> Clarification) or the trader's
// own utterance (`detect_asset_class_signal()`/`build_ambiguity_clarification()`
// below, called by `assistant_service.cpp` BEFORE this module ever runs, so
// that a decisive answer overrides `asset_class` before it reaches
// `translate()` and an indecisive one short-circuits to a Clarification
// without paying for GP-ARA or a network round trip). Both live outside this
// module for the same reason: neither the utterance's free text nor a live
// quote is a "closed-form comparison over five short strings/ints" this
// reasoner's own file banner promises to be. This module proving an
// ambiguous-root input Proven says only "nothing here contradicts itself on
// paper" -- it does not skip either check that runs around it.
module;
#include <array>
#include <cctype>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module assistant_verification;

import sensen.gp_ara_interfaces;
import strategy_catalogue;

namespace options_calculator::assistant::verify {

// ---------------------------------------------------------------------------
// The five output fields, as GP-ARA's InputDataType.
// ---------------------------------------------------------------------------

/** Mirrors `calculator.assistant.StrategyParams` field for field. Kept as a
 * plain struct (not a proto type) so this module carries no protobuf
 * dependency -- mapping to/from the wire message is `assistant_service.cpp`'s
 * job, one layer up. */
export struct AssistantParamsInput {
    std::string symbol;
    std::string asset_class;
    std::string strategy;
    std::int64_t expiration_days = 0;
    std::int64_t quantity = 0;
};

/** Which of the existing `Refusal::Reason` values (see assistant.proto) a
 * verification failure maps to. `assistant_service.cpp` owns the actual
 * proto enum; this module only ever picks among reasons that already exist
 * there, per the task's mapping constraint -- nothing here invents a new
 * wire-level reason code. */
export enum class ReasonCode {
    None,                // only valid alongside Outcome::Proven
    UnsupportedStrategy,  // -> Refusal::UNSUPPORTED_STRATEGY
    UnknownSymbol,        // -> Refusal::UNKNOWN_SYMBOL
    OutOfScope,           // -> Refusal::OUT_OF_SCOPE
};

/** GP-ARA's LogicalConstraintType for this domain: not an SMT-LIB formula
 * string (see the file banner for why), just the outcome of evaluating every
 * cross-field rule against one `AssistantParamsInput`. */
export struct VerificationFacts {
    bool violated = false;    // a rule found a definite contradiction -> Unsafe
    bool incomplete = false;  // a rule could not be evaluated -> Indeterminate
    ReasonCode reason = ReasonCode::None;
    std::string detail;
};

namespace detail {

/** Futures roots this product's own catalogue recognizes -- mirrors
 * `kKnownFuturesRoots` in assistant_service.cpp (root README's Interactive
 * Symbol Selector: "Futures (ES, NQ, CL, GC, ZB)"). Duplicated rather than
 * shared because this module is deliberately proto- and
 * assistant_service.cpp-independent; see the file banner. */
constexpr std::array<std::string_view, 5> kKnownFuturesRoots{"ES", "NQ", "CL", "GC", "ZB"};

/** Roots genuinely ambiguous across asset classes -- CHECKED against a real
 * equity-ticker lookup, not assumed from the project memory note's own
 * headline example. Of the five supported futures roots (`kKnownFuturesRoots`
 * above): "ES" collides with Eversource Energy (NYSE: ES, a live listing
 * today) and "CL" collides with Colgate-Palmolive (NYSE: CL, also live
 * today). "NQ" (NQ Mobile Inc traded under NYSE: NQ but delisted/renamed
 * years ago), "GC" (no current NYSE/NASDAQ equity ticker "GC" -- "GC" as a
 * plain symbol today means the COMEX gold futures continuation, not a
 * stock), and "ZB" (no plain equity ticker "ZB" -- "ZB-A" is a Zions
 * Bancorporation PREFERRED-share class suffix, a different ticker) have no
 * live equity twin, so a root with no twin is NOT ambiguous and must not be
 * added here on the strength of a superficial resemblance alone.
 *
 * Excluded from the static symbol/asset_class contradiction rule below for
 * both entries (this array previously covered only "ES", silently treating
 * "CL claimed as EQUITY" as a definite hallucination even though CL really
 * is a live equity too -- that was itself a latent over-refusal bug this
 * correction closes). Neither live market data nor this static reasoner can
 * disambiguate these; see `detect_asset_class_signal()`/
 * `build_ambiguity_clarification()` below for the utterance-based resolution
 * `assistant_service.cpp` runs instead, and that file's `probe_symbol()` for
 * the live-market-based one. Kept as a plain symbol list, separate from the
 * more detailed `kAmbiguousRootDetails` table below, only so the static
 * contradiction rule's `contains()` check stays a one-line array lookup;
 * the two lists are the same two roots and must be kept in sync by hand --
 * the same accepted duplication tradeoff `kKnownFuturesRoots`'s own doc
 * comment already makes for cross-file independence. */
constexpr std::array<std::string_view, 2> kAmbiguousRoots{"ES", "CL"};

/** Crypto symbols this product's catalogue recognizes ("Crypto (BTC, ETH)"). */
constexpr std::array<std::string_view, 2> kKnownCryptoSymbols{"BTC", "ETH"};

/** Strategies the calculator can price but the fine-tuned assistant was never
 * trained on. Checked independently of `strategy_catalogue`'s own membership
 * table (see the file banner: "gated out of the UI... the calculator prices
 * it correctly but the assistant was never taught it") -- if a future
 * regeneration of that catalogue ever adds one of these ids (as its own
 * header comment says a resolution of the frontend/dataset disagreement
 * might), this list keeps refusing it from the assistant side regardless. */
constexpr std::array<std::string_view, 1> kAssistantForbiddenStrategies{"crack_321"};

constexpr std::size_t kMaxSymbolLength = 15;

/** Stricter than `assistant_service.cpp`'s own `kMinExpirationDays` (0): a
 * strategy priced with T=0 has no time value left to price, so this
 * mandatory layer floors at 1 regardless of what the cheaper per-field check
 * upstream allows. Tightening a bound this way can only refuse MORE than the
 * existing gate, never less -- it cannot turn a previously-refused input into
 * an accepted one. */
constexpr std::int64_t kMinExpirationDays = 1;
constexpr std::int64_t kMaxExpirationDays = 3650;
constexpr std::int64_t kMinQuantity = 1;
constexpr std::int64_t kMaxQuantity = 100'000;

template <std::size_t N>
[[nodiscard]] constexpr auto contains(const std::array<std::string_view, N>& haystack,
                                       std::string_view needle) noexcept -> bool {
    for (const auto entry : haystack) {
        if (entry == needle) return true;
    }
    return false;
}

[[nodiscard]] constexpr auto looks_like_ticker(std::string_view s) noexcept -> bool {
    if (s.empty() || s.size() > kMaxSymbolLength) return false;
    for (const char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Ambiguous-root disambiguation.
//
// `assistant_service.cpp` calls this BEFORE it ever builds an
// `AssistantParamsInput` for the two roots in `kAmbiguousRoots`: a decisive
// signal overrides the model's `asset_class` guess so it can never leak a
// stale/wrong value through, and an indecisive one short-circuits straight
// to a `Clarification`, before either GP-ARA or the live market-data probe
// run. See the `kAmbiguousRoots` doc comment above for why this lives
// outside `translate()`'s own five-field, network-free contract: resolving
// which asset class a trader meant needs the trader's own free-text words,
// which is exactly the kind of per-request, non-categorical information that
// reasoner is deliberately built without.
// ---------------------------------------------------------------------------

/** Which concrete asset class, if any, a trader's own words settle an
 * ambiguous root to. `None` covers both "said nothing relevant" and "said
 * something that reads as both at once" (e.g. contradictory or nonsensical
 * text) -- either way, this layer cannot honestly pick a side, so both map
 * to the same "ask" outcome. */
export enum class AssetClassSignal { None, Futures, Equity };

/** One ambiguous root's pair of concrete readings, for both the signal
 * detector below and the clarification question it produces when it cannot
 * decide. `futures_label`/`equity_label` are what actually gets shown to the
 * trader -- concrete enough that "the answer is one word" (per the task
 * brief), not "please clarify." `equity_keyword` is the company name a
 * trader might use INSTEAD of "shares"/"stock"/"equity" to mean the equity
 * side (e.g. naming Eversource or Colgate directly) -- checked as a
 * lowercase substring of the utterance. */
export struct AmbiguousRootInfo {
    std::string_view symbol;
    std::string_view futures_label;
    std::string_view equity_label;
    std::string_view equity_keyword;
};

namespace detail {

constexpr std::array<AmbiguousRootInfo, 2> kAmbiguousRootDetails{
    {{.symbol = "ES",
      .futures_label = "futures on the E-mini S&P 500",
      .equity_label = "options on Eversource Energy, the NYSE utility",
      .equity_keyword = "eversource"},
     {.symbol = "CL",
      .futures_label = "futures on WTI crude oil",
      .equity_label = "options on Colgate-Palmolive, the NYSE consumer-goods company",
      .equity_keyword = "colgate"}}};

[[nodiscard]] constexpr auto to_lower_char(char c) noexcept -> char {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] auto to_lower_copy(std::string_view s) -> std::string {
    std::string out(s.size(), '\0');
    for (std::size_t i = 0; i < s.size(); ++i) out[i] = to_lower_char(s[i]);
    return out;
}

[[nodiscard]] auto to_upper_copy(std::string_view s) -> std::string {
    std::string out(s.size(), '\0');
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        out[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    return out;
}

[[nodiscard]] auto contains_ci(std::string_view lowercased_haystack, std::string_view lowercase_needle) noexcept
    -> bool {
    return lowercased_haystack.find(lowercase_needle) != std::string_view::npos;
}

constexpr std::string_view kMonthCodeLetters = "FGHJKMNQUVXZ";

/** True iff `context` contains a real futures contract code for `root` as
 * its own token -- `root` immediately followed by one of the twelve
 * standard delivery-month letters and exactly two digits (e.g. "ESU26",
 * "clz26"), not embedded inside a longer alphanumeric run. Matched
 * case-insensitively since a trader may type either case; decisive on its
 * own per the task brief ("a contract code like ESU26"). */
[[nodiscard]] auto contains_contract_code(std::string_view context, std::string_view root) noexcept -> bool {
    if (root.size() != 2) return false;  // both ambiguous roots today are 2 characters
    const std::string upper = to_upper_copy(context);
    const std::string upper_root = to_upper_copy(root);

    std::size_t pos = 0;
    while (true) {
        pos = upper.find(upper_root, pos);
        if (pos == std::string::npos) return false;

        const std::size_t after = pos + upper_root.size();
        const bool left_boundary_ok =
            (pos == 0) || (std::isalnum(static_cast<unsigned char>(upper[pos - 1])) == 0);
        if (left_boundary_ok && after + 3 <= upper.size()) {
            const bool is_month_letter = kMonthCodeLetters.find(upper[after]) != std::string_view::npos;
            const bool digit1 = upper[after + 1] >= '0' && upper[after + 1] <= '9';
            const bool digit2 = upper[after + 2] >= '0' && upper[after + 2] <= '9';
            const bool right_boundary_ok =
                (after + 3 == upper.size()) || (std::isalnum(static_cast<unsigned char>(upper[after + 3])) == 0);
            if (is_month_letter && digit1 && digit2 && right_boundary_ok) return true;
        }
        ++pos;
    }
}

/** Words that are decisive on their own for the futures side. Deliberately
 * does NOT include the bare word "contract": traders use "contract" loosely
 * for options on equities too (the task brief's own example -- "Long ES,
 * 30 days, 1 contract" -- must still ask, and it says "contract"), so it is
 * suggestive at best, never sufficient by itself to skip the question. */
constexpr std::array<std::string_view, 5> kFuturesKeywords{"futures", "e-mini", "emini", "front month",
                                                            "front-month"};

/** Words that are decisive on their own for actually OWNING the equity --
 * as opposed to an option on it. Matched as substrings, so
 * "share"/"stock"/"equity" alone already cover their plurals
 * ("shares"/"stocks"/"equities"). Kept separate from `kOptionsKeyword`
 * below: the two answer different questions ("is the underlying an
 * equity" versus "is this trade an option"), and an option can be written
 * on either an equity OR a future -- see `detect_asset_class_signal`'s own
 * comment for the precedence rule that split makes possible. */
constexpr std::array<std::string_view, 3> kEquityOwnershipKeywords{"share", "stock", "equity"};

/** The word this file's own clarification question now offers as the
 * equity-side answer ("options on Eversource Energy...", see
 * `kAmbiguousRootDetails` above). Matched as a substring, so "option"
 * already covers "options". Deliberately NOT folded into
 * `kEquityOwnershipKeywords`: options on futures are real
 * (`covered_futures_call`, category "Futures" in strategy_catalogue.cppm,
 * is an option written on a future, an FOP) -- so this word alone is never
 * unconditionally equity, only equity when no futures signal accompanies
 * it. See `detect_asset_class_signal`'s own comment for exactly how that
 * precedence is applied. */
constexpr std::string_view kOptionsKeyword = "option";

}  // namespace detail

/** Looks up the ambiguous-root detail entry for `symbol`, or `nullptr` if
 * `symbol` is not one of the roots this file has verified genuinely
 * collides with a real equity (see `kAmbiguousRoots`'s doc comment). This is
 * the one check `assistant_service.cpp` needs to know whether ANY of this
 * section's machinery applies to a given symbol at all -- a plain,
 * unambiguous ticker (SPY, NVDA, QQQ, ...) always gets `nullptr` here and is
 * therefore never routed through disambiguation or asked a question. */
export [[nodiscard]] auto find_ambiguous_root_info(std::string_view symbol) -> const AmbiguousRootInfo* {
    for (const auto& info : detail::kAmbiguousRootDetails) {
        if (info.symbol == symbol) return &info;
    }
    return nullptr;
}

/**
 * Decides, from the trader's own words alone, which concrete asset class an
 * ambiguous `symbol` refers to -- `None` if `symbol` is not ambiguous in the
 * first place, or if `context` settles neither side, or both.
 *
 * `context` is expected to be the trader's current utterance and (on a
 * second turn) the prior clarification question concatenated -- see
 * `assistant_service.cpp`'s call site. Using the SAME keyword scan on both
 * turns is what makes the round trip work without special-casing it: a
 * one-word reply to the clarification ("futures", "options", "Eversource")
 * IS itself a fresh utterance containing a decisive keyword, so it resolves
 * exactly like a first-turn utterance that happened to already answer the
 * question would.
 *
 * PRECEDENCE: futures beats a bare options signal. `kAmbiguousRootDetails`
 * now offers "options on <the equity>" as the equity-side answer (see that
 * table's own doc comment for why this file's earlier "the NYSE utility
 * stock" wording was wrong for a calculator that never prices stock
 * outright). That means "options" is no longer unconditionally an EQUITY
 * word: this catalogue's `asset_class` names the UNDERLYING, and an option
 * on a FUTURE is still FUTURES -- `covered_futures_call`
 * (strategy_catalogue.cppm, category "Futures") is exactly that, an FOP.
 * So "options on ES futures" carries both a futures signal and the options
 * word at once, and under the old symmetric rule (both signals present ->
 * None) it would ask forever, since it can never say only one side. Futures
 * wins that specific conflict. This precedence is deliberately narrow,
 * though: it fires only against the bare options word, not against an
 * OWNERSHIP word ("shares"/"stock"/"equity", or the company name itself)
 * contradicting a futures signal -- "shares of a futures contract" is not a
 * real instrument either way, so that combination is left as a genuine,
 * unresolvable contradiction and still falls through to the ask-again path
 * below, exactly as it did before this change.
 */
export [[nodiscard]] auto detect_asset_class_signal(std::string_view symbol, std::string_view context)
    -> AssetClassSignal {
    const auto* info = find_ambiguous_root_info(symbol);
    if (info == nullptr) return AssetClassSignal::None;

    const std::string lower = detail::to_lower_copy(context);

    bool futures_signal = detail::contains_contract_code(context, info->symbol);
    for (const auto keyword : detail::kFuturesKeywords) {
        if (detail::contains_ci(lower, keyword)) {
            futures_signal = true;
            break;
        }
    }

    const bool options_signal = detail::contains_ci(lower, detail::kOptionsKeyword);

    bool ownership_signal = detail::contains_ci(lower, info->equity_keyword);
    for (const auto keyword : detail::kEquityOwnershipKeywords) {
        if (detail::contains_ci(lower, keyword)) {
            ownership_signal = true;
            break;
        }
    }

    // The precedence rule (see the doc comment above): futures plus a bare
    // options word, with no ownership word contradicting it, is FUTURES --
    // an option on a future, not equity.
    if (futures_signal && options_signal && !ownership_signal) {
        return AssetClassSignal::Futures;
    }

    const bool equity_signal = options_signal || ownership_signal;

    // Both signals (contradictory) and neither signal (silent) are the same
    // "cannot honestly decide" outcome -- see AssetClassSignal::None's own
    // doc comment.
    if (futures_signal == equity_signal) return AssetClassSignal::None;
    return futures_signal ? AssetClassSignal::Futures : AssetClassSignal::Equity;
}

/**
 * Builds the clarification question for an ambiguous root neither
 * `detect_asset_class_signal` nor (upstream of it) anything else could
 * resolve -- `std::nullopt` iff `symbol` is not ambiguous at all, so a
 * caller can tell "not applicable" apart from "applicable, and here is the
 * question."
 *
 * Names both concrete readings (never "please clarify") so the answer is
 * one word, per the task brief. Also surfaces the strategy the model
 * inferred: `long_put` from an utterance that never said "put" is a known,
 * separate defect (out of scope here -- see the task brief) in the SAME
 * model call that produced this ambiguous symbol, so a trader reading this
 * question gets a chance to catch that mistake too, in the same round trip,
 * rather than only after a second wrong answer downstream.
 */
export [[nodiscard]] auto build_ambiguity_clarification(std::string_view symbol, std::string_view strategy)
    -> std::optional<std::string> {
    const auto* info = find_ambiguous_root_info(symbol);
    if (info == nullptr) return std::nullopt;

    std::string question = "\"";
    question += symbol;
    question += "\" could mean ";
    question += info->futures_label;
    question += ", or ";
    question += info->equity_label;
    question += " -- which did you mean?";
    if (!strategy.empty()) {
        question += " (I read this as attempting \"";
        question += strategy;
        question += "\"; let me know if that is not right either.)";
    }
    return question;
}

// ---------------------------------------------------------------------------
// Near-miss strategy name normalisation.
//
// Observed against the live service: given a clarification round trip ("Long
// ES, 30 days, 1 contract." then answered "futures"), the model emits
// `long_futures`. The catalogue's real id is `futures_long` -- a
// transposition of the same two tokens. This is not the model failing to
// know the strategy: the identical trade phrased with explicit futures
// wording ("Buy an E-mini ES futures outright...") gets `futures_long`
// right, so the transposition is a token-order slip on THIS input, not a
// missing capability. A 0.6B fine-tune is going to do this again for some
// other two-token id under some other input; this is the general guard, not
// a special case for one string.
//
// Derived MECHANICALLY from `strategy_catalogue::all()`, never hand-authored
// -- see the task brief this shipped against: an invented alias list is
// exactly the kind of drift `strategy_catalogue.cppm` itself exists to
// prevent (that file's own banner: three places name these ids, and two of
// them must never independently guess). For every catalogue id with EXACTLY
// two underscore-separated tokens, the single transposition of those two
// tokens is a candidate alias, UNLESS:
//
//   - that transposed string is ITSELF a different catalogue entry (then the
//     model saying it verbatim already means that other, real strategy --
//     there is nothing to alias, and silently redirecting it would be
//     exactly the wrong-priced-position failure this whole mechanism exists
//     to avoid), or
//   - more than one catalogue id transposes to the same candidate string
//     (then which one the model meant is genuinely ambiguous; per the task
//     brief, "silently 'correcting' a name to the wrong strategy would be
//     far worse than refusing", so NEITHER candidate becomes an alias and
//     both stay a refusal).
//
// Ids with anything other than exactly one underscore are skipped outright:
// a transposition of three or more tokens has more than one possible swap
// (is "bull_call_spread" misheard as "call_bull_spread" or
// "spread_call_bull" or ...?), which is exactly the kind of guess this
// function refuses to make. Checked against the current 47-entry catalogue:
// every two-token id's transposition is unique and collides with no other
// entry, so today this excludes nothing -- but the check runs against
// whatever `strategy_catalogue.cppm` regenerates to, not against today's
// count, so a future catalogue addition that DOES collide is still caught.
// ---------------------------------------------------------------------------

namespace detail {

/** One derived alias: `alias` is the token-order near-miss a model might
 * emit; `canonical` is the one real catalogue id it unambiguously means. */
struct StrategyAlias {
    std::string alias;
    std::string canonical;
};

[[nodiscard]] auto build_strategy_aliases() -> std::vector<StrategyAlias> {
    std::vector<std::pair<std::string, std::string>> candidates;
    for (const auto& info : ::options_calculator::strategy::all()) {
        const std::string id{info.id};
        const auto sep = id.find('_');
        if (sep == std::string::npos || id.find('_', sep + 1) != std::string::npos) {
            continue;  // not exactly two tokens -- see banner above.
        }
        std::string transposed = id.substr(sep + 1) + "_" + id.substr(0, sep);
        if (::options_calculator::strategy::is_known(transposed)) {
            continue;  // already means a different, real strategy verbatim.
        }
        candidates.emplace_back(std::move(transposed), id);
    }

    std::vector<StrategyAlias> result;
    result.reserve(candidates.size());
    for (const auto& [alias, canonical] : candidates) {
        std::size_t collisions = 0;
        for (const auto& other : candidates) {
            if (other.first == alias) ++collisions;
        }
        if (collisions == 1) {
            result.push_back(StrategyAlias{.alias = alias, .canonical = canonical});
        }
        // collisions > 1: two different catalogue ids transpose to the same
        // string -- genuinely ambiguous, so neither becomes an alias (both
        // stay refused, per this section's own banner).
    }
    return result;
}

}  // namespace detail

/**
 * Maps a strategy id the model emitted but the catalogue does not recognise
 * to the one catalogue entry it unambiguously means, or `std::nullopt` if
 * `raw` is not a known token-order transposition of exactly one catalogue
 * entry.
 *
 * This is a NAME-LEVEL fix only. Callers MUST still run whatever this
 * returns through the catalogue and through `verify_assistant_params` --
 * this function only answers "is `raw` a plausible token-order slip of a
 * real strategy id," never "is this strategy compatible with the rest of
 * this request."
 */
export [[nodiscard]] auto normalize_strategy_alias(std::string_view raw) -> std::optional<std::string> {
    static const std::vector<detail::StrategyAlias> aliases = detail::build_strategy_aliases();
    for (const auto& entry : aliases) {
        if (entry.alias == raw) return entry.canonical;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The domain.
// ---------------------------------------------------------------------------

/**
 * GP-ARA `DomainPolicy` for the assistant's five output fields.
 *
 * `translate()` is total and explicit over the space this module can reach:
 * every branch either returns a definite violation, a definite pass, or
 * (exactly one case, see the file banner) an explicit "incomplete" -- there
 * is no branch that silently falls through to "assume fine." An unmapped
 * asset_class or an unrecognised strategy category is refused, never
 * defaulted.
 */
export class AssistantParamsDomain {
  public:
    using InputDataType = AssistantParamsInput;
    using LogicalConstraintType = VerificationFacts;

    [[nodiscard]] auto translate(const AssistantParamsInput& in) const -> VerificationFacts {
        VerificationFacts f;

        // ---- symbol: shape ----
        if (!detail::looks_like_ticker(in.symbol)) {
            f.violated = true;
            f.reason = ReasonCode::UnknownSymbol;
            f.detail = "\"" + in.symbol + "\" is not shaped like a real ticker.";
            return f;
        }

        // ---- asset_class: total membership check ----
        const bool is_equity = in.asset_class == "EQUITY";
        const bool is_futures = in.asset_class == "FUTURES";
        const bool is_crypto = in.asset_class == "CRYPTO";
        if (!is_equity && !is_futures && !is_crypto) {
            f.violated = true;
            f.reason = ReasonCode::UnknownSymbol;
            f.detail = "\"" + in.asset_class + "\" is not a supported asset class.";
            return f;
        }

        // ---- strategy: assistant-side blocklist, independent of catalogue ----
        if (detail::contains(detail::kAssistantForbiddenStrategies, in.strategy)) {
            f.violated = true;
            f.reason = ReasonCode::UnsupportedStrategy;
            f.detail = "\"" + in.strategy +
                       "\" is gated out of the assistant: the calculator prices it, but the "
                       "assistant model was never trained on it.";
            return f;
        }

        // ---- strategy: catalogue membership ----
        const auto* info = ::options_calculator::strategy::find(in.strategy);
        if (info == nullptr) {
            f.violated = true;
            f.reason = ReasonCode::UnsupportedStrategy;
            f.detail =
                "\"" + in.strategy + "\" is not one of the strategies this calculator prices.";
            return f;
        }

        // ---- strategy: needs two distinct expiries this schema cannot carry ----
        //
        // `StrategyParams` (assistant.proto) has exactly one `expiration_days`
        // field. `strategy_catalogue::StrategyInfo::multi_expiry` marks the
        // strategies that structurally need two (calendar/diagonal spreads,
        // double diagonal, PMCC, futures calendar spread) -- and the
        // calculator's own UI already refuses to build one of these from a
        // single chain ("Needs two expiries -- add these legs from two
        // chains", frontend/src/components/StrategySelector.tsx). A single
        // expiration_days value can never resolve that regardless of what
        // number the model emits, so this is a definite contradiction, not a
        // bounds question -- checked before, and independent of, the
        // expiration_days range check below.
        if (info->multi_expiry) {
            f.violated = true;
            f.reason = ReasonCode::UnsupportedStrategy;
            f.detail = "\"" + in.strategy +
                       "\" needs two distinct expiries; a single expiration_days field cannot "
                       "express that.";
            return f;
        }

        // ---- strategy category vs asset_class: the cross-field rule a
        //      per-field check cannot express ----
        if (info->category == "Futures") {
            if (!is_futures) {
                f.violated = true;
                f.reason = ReasonCode::UnsupportedStrategy;
                f.detail = "\"" + in.strategy +
                           "\" is a futures-only strategy, but asset_class is \"" +
                           in.asset_class + "\".";
                return f;
            }
        } else if (info->category == "Bullish" || info->category == "Bearish" ||
                   info->category == "Neutral" || info->category == "Volatility" ||
                   info->category == "Income & Hedge") {
            if (is_futures) {
                f.violated = true;
                f.reason = ReasonCode::UnsupportedStrategy;
                f.detail = "\"" + in.strategy +
                           "\" is an options strategy, but asset_class is FUTURES.";
                return f;
            }
        } else {
            // A category `strategy_catalogue.cppm` regenerated with, that this
            // rule table has never been taught a compatibility rule for. Every
            // decidable rule above this point still ran and passed; this one
            // genuinely cannot be evaluated -- the honest answer is "cannot
            // decide," not "assume compatible." See the file banner for why
            // this is the one case this module reports Indeterminate rather
            // than a definite Unsafe.
            f.incomplete = true;
            f.reason = ReasonCode::OutOfScope;
            f.detail = "\"" + in.strategy + "\" has category \"" + std::string{info->category} +
                       "\", which this verifier has no asset_class compatibility rule for.";
            return f;
        }

        // ---- symbol vs asset_class: static contradiction for well-known,
        //      non-ambiguous roots ----
        //
        // Deliberately skipped for `kAmbiguousRoots` (documented case: "ES").
        // Whether a specific (symbol, asset_class) pair naming one of those
        // roots is real is a live-market question outside this reasoner's
        // authority -- see the file banner and `probe_symbol()` in
        // assistant_service.cpp, which runs immediately after this
        // verification and is the actual disambiguator.
        if (!detail::contains(detail::kAmbiguousRoots, in.symbol)) {
            if (detail::contains(detail::kKnownFuturesRoots, in.symbol) && !is_futures) {
                f.violated = true;
                f.reason = ReasonCode::UnknownSymbol;
                f.detail = "\"" + in.symbol + "\" is a known futures root, not a real " +
                           in.asset_class + " ticker.";
                return f;
            }
            if (detail::contains(detail::kKnownCryptoSymbols, in.symbol) && !is_crypto) {
                f.violated = true;
                f.reason = ReasonCode::UnknownSymbol;
                f.detail = "\"" + in.symbol + "\" is a known crypto symbol, not a real " +
                           in.asset_class + " ticker.";
                return f;
            }
        }

        // ---- quantity: bounds ----
        if (in.quantity < detail::kMinQuantity || in.quantity > detail::kMaxQuantity) {
            f.violated = true;
            f.reason = ReasonCode::OutOfScope;
            f.detail = "quantity " + std::to_string(in.quantity) + " is out of a sane range.";
            return f;
        }

        // ---- expiration_days: bounds ----
        if (in.expiration_days < detail::kMinExpirationDays ||
            in.expiration_days > detail::kMaxExpirationDays) {
            f.violated = true;
            f.reason = ReasonCode::OutOfScope;
            f.detail = "expiration_days " + std::to_string(in.expiration_days) +
                       " is out of a sane range for a strategy with time value.";
            return f;
        }

        return f;  // violated == false, incomplete == false -> Proven.
    }
};

// ---------------------------------------------------------------------------
// The reasoner.
// ---------------------------------------------------------------------------

/**
 * GP-ARA `ReasonerPolicy` for `AssistantParamsDomain`, discharging each
 * `VerificationFacts` by direct evaluation rather than an SMT solver -- see
 * the file banner for why Z3 is not available to this binary and why that is
 * sound here. Every path is default-deny: the only way `prove_safety`
 * returns `true` is the explicit `!violated && !incomplete` fallthrough.
 */
export class RuleBasedReasoner {
  public:
    struct Context {
        std::uint64_t queries_processed = 0;
    };
    using ContextType = Context;

    [[nodiscard]] auto prove_safety(Context& ctx, const VerificationFacts& formula)
        -> std::expected<bool, sensen::gp_ara::ReasonerError> {
        ++ctx.queries_processed;
        if (formula.incomplete) {
            return std::unexpected(sensen::gp_ara::ReasonerError{
                .code = sensen::gp_ara::ReasonerErrorCode::Indeterminate,
                .message = formula.detail});
        }
        if (formula.violated) {
            return false;
        }
        return true;
    }

    /**
     * Implemented only so `RuleBasedReasoner` satisfies `ReasonerPolicy` in
     * full (the concept requires both methods); nothing in this domain's own
     * verification flow calls it, since there is no separate goal state
     * distinct from the constraint itself -- proving the constraint safe IS
     * the goal. Still fully default-deny for any future caller that does
     * reach it: a goal is proven only if BOTH the constraint and the goal
     * independently pass `prove_safety`, and an Indeterminate on either side
     * propagates rather than being discarded.
     */
    [[nodiscard]] auto prove_goal(Context& ctx, const VerificationFacts& constraint,
                                   const VerificationFacts& goal)
        -> std::expected<bool, sensen::gp_ara::ReasonerError> {
        auto c = prove_safety(ctx, constraint);
        if (!c.has_value() || !*c) return c;
        return prove_safety(ctx, goal);
    }
};

static_assert(sensen::gp_ara::DomainPolicy<AssistantParamsDomain>,
              "AssistantParamsDomain must satisfy sensen::gp_ara::DomainPolicy");
static_assert(sensen::gp_ara::ReasonerPolicy<RuleBasedReasoner, AssistantParamsDomain>,
              "RuleBasedReasoner must satisfy sensen::gp_ara::ReasonerPolicy<AssistantParamsDomain>");

// ---------------------------------------------------------------------------
// The verdict, and the single entry point assistant_service.cpp calls.
// ---------------------------------------------------------------------------

export enum class Outcome { Proven, Unsafe, Indeterminate };

export struct VerificationVerdict {
    Outcome outcome = Outcome::Indeterminate;  // fail-closed default construction
    ReasonCode reason = ReasonCode::None;
    std::string message;
};

/**
 * The mandatory verification stage. Runs `AssistantParamsDomain::translate()`
 * then `RuleBasedReasoner::prove_safety()` and returns a verdict the caller
 * cannot mistake for a plain boolean.
 *
 * `assistant_service.cpp` is required to treat every `Outcome` other than
 * `Proven` as "do not populate params" -- see that file's own call site for
 * how `Unsafe` and `Indeterminate` are each mapped to a `Refusal`. Nothing
 * in this function ever returns `Proven` on an error path: the
 * default-constructed `VerificationVerdict` above is `Indeterminate`, so a
 * hypothetical future bug that returned early without setting `outcome`
 * would fail closed, not open.
 */
export [[nodiscard]] auto verify_assistant_params(const AssistantParamsInput& input)
    -> VerificationVerdict {
    const AssistantParamsDomain domain;
    RuleBasedReasoner reasoner;
    RuleBasedReasoner::ContextType ctx;

    const auto facts = domain.translate(input);
    const auto result = reasoner.prove_safety(ctx, facts);

    VerificationVerdict verdict;
    if (!result.has_value()) {
        verdict.outcome = Outcome::Indeterminate;
        verdict.reason = facts.reason;
        verdict.message = result.error().message;
        return verdict;
    }
    if (!*result) {
        verdict.outcome = Outcome::Unsafe;
        verdict.reason = facts.reason;
        verdict.message = facts.detail;
        return verdict;
    }
    verdict.outcome = Outcome::Proven;
    verdict.reason = ReasonCode::None;
    return verdict;
}

// ---------------------------------------------------------------------------
// Lexical support: does the trader's own text back up the strategy the model
// named at all?
//
// WHY THIS EXISTS: `translate()` above is deliberately blind to the
// utterance -- its whole contract is "closed-form comparison over five short
// strings/ints" (file banner), and it must stay that way, because
// `verify_assistant_params` already has callers/tests (this file's own
// `test_assistant_verification.cpp`, e.g. "CL claimed as EQUITY... deferred
// to live probe") that rely on it answering "internally consistent" without
// any opinion on wording. But "internally consistent" is not the same
// question as "did the trader actually say anything supporting this
// strategy" -- and a 0.6B fine-tune's most common failure mode is a
// STRUCTURALLY fine strategy id that has no basis in what was typed at all:
//
//   - "Long ES, 30 days, 1 contract." -> long_put. Nothing says "put".
//   - "Buy 100 shares of ES stock, Eversource, 30 days." -> long_call,
//     quantity=100. Buying shares is not a long call, and this calculator
//     prices no equity outright -- a share-purchase utterance has no
//     strategy in this catalogue at all, so "call" is not merely a weak
//     match, it is absent from a request that never mentioned an option.
//
// Both defects share one shape: the id passes `translate()` (every field is
// individually well-formed and mutually consistent) while the one word that
// actually distinguishes the id from its siblings never appears in the
// trader's own words. That is a real, checkable fact -- just not one
// `AssistantParamsDomain`'s five-field contract can see -- so it lives here,
// as its own function, taking the utterance as an explicit argument rather
// than smuggling it into the domain.
// ---------------------------------------------------------------------------

namespace detail {

/** Tokens inside a catalogue id that carry no discriminating meaning of
 * their own: a bare direction word a trader states about nearly every trade
 * regardless of which concrete structure they meant ("long" appears in
 * "Long ES, 30 days" just as readily for a future, a call, a put, or the
 * stock itself). Their presence in an utterance is not evidence for any
 * PARTICULAR strategy, so they are excluded before checking support --
 * otherwise "long_put" would count "Long ES..." as support for itself
 * purely because the trader said "Long", which is exactly the failure mode
 * this check exists to catch, not repeat under a different name. */
constexpr std::array<std::string_view, 2> kGenericStrategyTokens{"long", "short"};

/** Splits `strategy_id` on `_` into its lower-cased tokens, dropping any
 * `kGenericStrategyTokens` entry. For every id in `strategy_catalogue.cppm`
 * today this leaves at least one token ("futures_long" -> {"futures"},
 * "long_put" -> {"put"}, "bull_call_spread" -> {"bull", "call", "spread"});
 * an id that were SOMEHOW made of nothing but generic tokens would leave an
 * empty list, and `strategy_has_lexical_support` below treats that as
 * "nothing to check", never as "support missing" -- silence about a rule
 * this table cannot express must not turn into a refusal the same way an
 * unmapped category above does not. */
[[nodiscard]] auto strategy_distinguishing_tokens(std::string_view strategy_id) -> std::vector<std::string> {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start <= strategy_id.size()) {
        const auto sep = strategy_id.find('_', start);
        const std::string_view token = (sep == std::string_view::npos)
                                            ? strategy_id.substr(start)
                                            : strategy_id.substr(start, sep - start);
        if (!token.empty() && !contains(kGenericStrategyTokens, token)) {
            tokens.push_back(to_lower_copy(token));
        }
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }
    return tokens;
}

}  // namespace detail

/**
 * Whether anything in `context` (expected to be the trader's utterance,
 * optionally concatenated with a prior clarification exchange -- the same
 * shape `detect_asset_class_signal` above takes) lexically backs up
 * `strategy_id` at all.
 *
 * Matches loosely ON PURPOSE: any ONE of the id's distinguishing tokens
 * appearing anywhere in `context`, case-insensitively, counts as support.
 * This is not trying to verify the model parsed correctly -- only that it
 * did not invent a structure out of nothing. A stricter all-tokens-must-
 * match rule would refuse genuine requests over incidental phrasing (a
 * trader who writes "cash and carry on CL" for `cash_and_carry` should not
 * be second-guessed over the word "and"); a looser zero-tokens rule would
 * let exactly the two observed defects back through. One matching token is
 * the deliberate midpoint.
 *
 * Returns `true` (never blocks) when `strategy_id` has no distinguishing
 * token to check at all (see `strategy_distinguishing_tokens`'s own doc
 * comment) -- absence of a rule is not evidence of absence of support.
 */
export [[nodiscard]] auto strategy_has_lexical_support(std::string_view strategy_id, std::string_view context)
    -> bool {
    const auto tokens = detail::strategy_distinguishing_tokens(strategy_id);
    if (tokens.empty()) return true;
    const std::string lower_context = detail::to_lower_copy(context);
    for (const auto& token : tokens) {
        if (detail::contains_ci(lower_context, token)) return true;
    }
    return false;
}

/**
 * Whether `strategy` is one of the two "bare direction" ids the fine-tune
 * reaches for when it cannot actually tell what structure the trader
 * described, per this file's own trust-bar reasoning below (an options/
 * equity-category strategy is "the default the model falls back to when
 * confused") narrowed to the two ids BOTH live-observed defects actually
 * named: `long_call` and `long_put`. Deliberately not generalised to every
 * catalogue id -- see `is_unsupported_bare_direction_guess`'s own doc
 * comment for why a blanket lexical-support requirement across all 47
 * entries is a real regression risk this function does not take.
 *
 * `assistant_service.cpp` calls this for EVERY request, regardless of
 * symbol -- unlike the ambiguous-root machinery above, the shares ->
 * long_call conflation is not specific to ES/CL; the identical mistake is
 * equally wrong for "buy 100 shares of NVDA".
 *
 * A BARE "OPTION"/"OPTIONS" WORD IS ITS OWN, SEPARATE ESCAPE HATCH -- checked
 * IN ADDITION to `strategy_has_lexical_support`, not instead of it, and this
 * is load-bearing, not a convenience: the round trip this file's own
 * clarification produces answers "which asset class" without ever picking a
 * side between call and put. Trader says "Long ES, 30 days, 1 contract.",
 * gets asked futures-vs-equity, answers "options" -- the model (reasonably)
 * still has to commit to a direction and may say `long_put`, with the word
 * "put" appearing nowhere across EITHER turn. Demanding the specific word
 * "put" here would re-ask a trader who already answered once, which the
 * task's own gate 3 forbids ("answering that clarification still resolves in
 * one trip, both 'futures' and 'options'"). A bare options word is still
 * real evidence of SOME options intent -- just not which side -- which is a
 * different, narrower kind of ambiguity than the two live defects this
 * function exists to catch (both of which have NO options-adjacent word at
 * all: one says nothing about the trade type, the other says "shares").
 */
export [[nodiscard]] auto is_unsupported_bare_direction_guess(std::string_view strategy, std::string_view context)
    -> bool {
    if (strategy != "long_call" && strategy != "long_put") return false;
    if (strategy_has_lexical_support(strategy, context)) return false;
    return !detail::contains_ci(detail::to_lower_copy(context), detail::kOptionsKeyword);
}

// ---------------------------------------------------------------------------
// GP-ARA constraint propagation: settling an ambiguous root's asset class by
// REASONING about the strategy, when `detect_asset_class_signal` above could
// not settle it from the trader's own words.
//
// THE CONSTRAINT ALREADY EXISTS AND WAS ONLY EVER USED TO REJECT:
// `AssistantParamsDomain::translate()`'s category-vs-asset_class rule already
// encodes "a Futures-category strategy requires asset_class == FUTURES, and
// every other known category forbids it". Given an ambiguous root and a
// strategy whose category is "Futures", that constraint pins asset_class to
// exactly one value -- FUTURES is not merely consistent with the rest of the
// request, it is the ONLY asset_class category "Futures" permits at all
// (every other known category is UNSAFE with is_futures true, and an
// unmapped category is INDETERMINATE, never PROVEN). So testing the
// candidate `{symbol, asset_class="FUTURES", strategy, expiration_days,
// quantity}` through the exact same mandatory `verify_assistant_params` used
// everywhere else IS the inference: a `Proven` verdict on that candidate
// could only happen if `strategy`'s category is "Futures" and every other
// field is independently sound (catalogue membership, the assistant
// blocklist, `multi_expiry`, quantity and expiration bounds) -- nothing new
// is invented here, this reuses the one rule table this domain already has.
//
// THE TRAP THIS DELIBERATELY DOES NOT WALK INTO:
// The mirror image -- inferring EQUITY when the strategy's category forbids
// FUTURES -- is NOT attempted, ever, by this function. The production case
// this whole task shipped against is exactly that direction: "Long ES, 30
// days, 1 contract." produces `long_put`, an options-category strategy, with
// no "put" anywhere in the utterance. `long_put`'s category forbidding
// FUTURES is real and would "logically" leave EQUITY by elimination between
// the two readings this ambiguous-root mechanism ever offers -- but per this
// file's own earlier reasoning (see `AmbiguousRootInfo`'s neighbourhood),
// an options/equity-category guess is the model's DEFAULT when it cannot
// actually tell, not evidence it did. Trusting that elimination would
// propagate a hallucination into a confident, wrong-priced answer -- strictly
// worse than asking. A Futures-category guess carries no equivalent
// suspicion: the fine-tune does not emit `futures_long` (or any other
// Futures-category id) as a generic fallback, so category=="Futures" is
// asymmetrically strong evidence FUTURES-category is real, in a way
// category!="Futures" is not evidence EQUITY is.
//
// THE SECOND GATE, ON TOP OF THE CONSTRAINT: LEXICAL SUPPORT.
// Even in the trustworthy direction, this function additionally requires
// `strategy_has_lexical_support` before inferring -- so a Futures-category
// id the model produced with literally nothing in the utterance backing it
// (say, `min_variance_hedge` on a request that never mentioned a hedge or a
// beta ratio) does not get to settle the ambiguity either. This is a second,
// independent reason to decline to infer, layered on top of the category
// constraint rather than replacing it: either one failing is enough to fall
// through to asking.
// ---------------------------------------------------------------------------

/**
 * Attempts to settle an ambiguous root's asset class by reasoning about
 * `strategy`, when `detect_asset_class_signal` could not settle it from the
 * trader's own words. Returns `"FUTURES"` when both the category constraint
 * (via `verify_assistant_params` on the FUTURES candidate) and lexical
 * support are satisfied; `std::nullopt` otherwise -- covering a definite
 * `Unsafe` (this strategy could never be FUTURES: wrong category, a
 * multi-expiry shape a single `expiration_days` cannot carry, the assistant
 * blocklist, or bad bounds), an `Indeterminate` (an uncategorised strategy),
 * AND missing lexical support, all three collapsed to the same "cannot
 * soundly infer" answer on purpose: `assistant_service.cpp`'s call site
 * falls through to the existing ambiguity Clarification on `std::nullopt`
 * regardless of which of the three it was, which is the correct behaviour
 * for every one of them -- none is a case a follow-up question cannot
 * resolve, in contrast to the OTHER path's Indeterminate (see
 * `verify_assistant_params`'s own doc comment on why that one stays a flat
 * refusal). Deliberately never returns `"EQUITY"` -- see the section banner
 * above for why that direction is not attempted at all.
 */
export [[nodiscard]] auto infer_ambiguous_root_asset_class(std::string_view symbol, std::string_view strategy,
                                                            std::int64_t expiration_days, std::int64_t quantity,
                                                            std::string_view context)
    -> std::optional<std::string> {
    const AssistantParamsInput futures_candidate{.symbol = std::string{symbol},
                                                  .asset_class = "FUTURES",
                                                  .strategy = std::string{strategy},
                                                  .expiration_days = expiration_days,
                                                  .quantity = quantity};
    if (verify_assistant_params(futures_candidate).outcome != Outcome::Proven) return std::nullopt;
    if (!strategy_has_lexical_support(strategy, context)) return std::nullopt;
    return std::string{"FUTURES"};
}

}  // namespace options_calculator::assistant::verify
