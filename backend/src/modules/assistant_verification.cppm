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
// A symbol root that is genuinely ambiguous across asset classes (documented
// case: "ES", both the E-mini S&P futures root and Eversource Energy's
// NYSE ticker) is deliberately NOT modelled as this module's own
// Indeterminate. Resolving it needs live market data this static reasoner
// has no access to; `assistant_service.cpp`'s `probe_symbol()` already does
// exactly that resolution (Resolved / Unknown / AssetClassMismatch ->
// Clarification), never guessing either way. This module proving an
// ambiguous-root input Proven says only "nothing here contradicts itself on
// paper" -- it does not skip the live check that runs immediately after.
module;
#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

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

/** Roots documented as genuinely ambiguous across asset classes -- see the
 * project memory note "Futures root ticker collision". Excluded from the
 * static symbol/asset_class contradiction rule below; live market data (in
 * `assistant_service.cpp::probe_symbol`) is the only thing that can
 * disambiguate these, so this module deliberately expresses no opinion on
 * them. */
constexpr std::array<std::string_view, 1> kAmbiguousRoots{"ES"};

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
