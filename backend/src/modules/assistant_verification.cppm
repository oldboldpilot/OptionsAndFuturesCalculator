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
      .futures_label = "the E-mini S&P 500 futures contract",
      .equity_label = "Eversource Energy, the NYSE utility stock",
      .equity_keyword = "eversource"},
     {.symbol = "CL",
      .futures_label = "the WTI crude oil futures contract",
      .equity_label = "Colgate-Palmolive, the NYSE consumer-goods stock",
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

/** Words that are decisive on their own for the equity side. Matched as
 * substrings, so "share"/"stock"/"equity" alone already cover their plurals
 * ("shares"/"stocks"/"equities"). */
constexpr std::array<std::string_view, 3> kEquityKeywords{"share", "stock", "equity"};

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
 * one-word reply to the clarification ("futures", "the stock", "Eversource")
 * IS itself a fresh utterance containing a decisive keyword, so it resolves
 * exactly like a first-turn utterance that happened to already answer the
 * question would.
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

    bool equity_signal = detail::contains_ci(lower, info->equity_keyword);
    for (const auto keyword : detail::kEquityKeywords) {
        if (detail::contains_ci(lower, keyword)) {
            equity_signal = true;
            break;
        }
    }

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

}  // namespace options_calculator::assistant::verify
