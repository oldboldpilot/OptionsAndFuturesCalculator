// GENERATED FILE -- DO NOT EDIT BY HAND.
//
// Produced by scripts/generate_strategy_catalogue.py from the single upstream
// source agent/dataset/strategies.json (47 entries at generation time).
// Regenerate with:
//
//     python3 scripts/generate_strategy_catalogue.py
//
// and commit the result. Hand-editing this file only until the next
// regeneration silently discards the edit, and hand-editing the array without
// re-running the generator is exactly the drift this file exists to prevent.
//
// THREE PLACES CURRENTLY NAME THE 48 STRATEGY IDENTIFIERS THIS PRODUCT
// SUPPORTS. THEY MUST NOT DRIFT.
//   1. frontend/src/components/StrategySelector.tsx  -- the web UI's own list
//   2. agent/dataset/strategies.json                 -- the LLM fine-tuning
//                                                        dataset, and the
//                                                        SOURCE OF TRUTH for
//                                                        this file
//   3. backend/src/modules/strategy_catalogue.cppm   -- this file, the
//                                                        backend's runtime
//                                                        validator
//
// (2) -> (3) is kept honest mechanically: this file is regenerated from (2)
// by scripts/generate_strategy_catalogue.py, never edited by hand, so it
// cannot independently drift from the dataset.
//
// (1) and (2) have NO such mechanism between them; they are edited by hand,
// separately, by design (one is a UI list, the other a training set). At
// generation time they were verified to disagree: the frontend lists a
// 48th strategy, "crack_321" ("3-2-1 Crack Spread", category Futures), that
// does not appear in agent/dataset/strategies.json. This catalogue therefore
// has 47 entries, not 48, and correctly REFUSES "crack_321" if an LLM
// assistant ever emits it -- which is the intended behaviour (refuse rather
// than pass through an id this catalogue cannot vouch for), but it means a
// user selecting that strategy in the UI today is exercising a strategy the
// dataset and this validator do not know about. Resolve by either adding
// crack_321 to agent/dataset/strategies.json (then regenerating this file)
// or removing it from StrategySelector.tsx; this generator deliberately does
// not guess which.
module;
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

export module strategy_catalogue;

/*
 * The authoritative set of strategy identifiers the pricing engine and any
 * LLM assistant built on top of it are allowed to act on.
 *
 * This exists because an LLM assistant RPC returns a `strategy` string that
 * the backend cannot take on faith -- a model can hallucinate a plausible-
 * sounding id ("bull_call_ladder_wide") that was never defined anywhere, and
 * silently accepting it would mean pricing a strategy nobody specified the
 * legs for. is_known() is the gate: anything not in this table must be
 * refused, not guessed at or coerced to the nearest match.
 */
namespace options_calculator::strategy {

/** One catalogued strategy. Every field here is safe to echo back to a caller. */
export struct StrategyInfo {
    std::string_view id;           // e.g. "iron_condor" -- the wire identifier
    std::string_view name;         // e.g. "Iron Condor" -- for clarifications
    std::string_view category;     // e.g. "Neutral" -- for clarifications
    std::string_view description;  // one line, from the dataset
    int leg_count;
    bool multi_expiry;
};

inline constexpr std::array<StrategyInfo, 47> kCatalogue{{
    {"long_call", "Long Call", "Bullish", "Unlimited upside, premium at risk.", 1, false},
    {"bull_call_spread", "Bull Call Spread", "Bullish", "Debit spread. Capped risk and reward.", 2, false},
    {"bull_put_spread", "Bull Put Spread", "Bullish", "Credit spread. Profits if price holds above the short put.", 2, false},
    {"call_backspread", "Call Ratio Backspread", "Bullish", "Short one near call against two further calls. Long convexity.", 2, false},
    {"risk_reversal", "Risk Reversal", "Bullish", "Short put funds a long call. Synthetic long with a gap.", 2, false},
    {"synthetic_long", "Synthetic Long Stock", "Bullish", "Long call plus short put at one strike replicates the shares.", 2, false},
    {"call_ratio_spread", "Call Ratio Spread", "Bullish", "One long call against two short. Naked risk above the wing.", 2, false},
    {"bull_call_ladder", "Bull Call Ladder", "Bullish", "Spread financed by a second short call further out.", 3, false},
    {"long_put", "Long Put", "Bearish", "Defined risk downside exposure.", 1, false},
    {"bear_put_spread", "Bear Put Spread", "Bearish", "Debit spread. Capped risk and reward.", 2, false},
    {"bear_call_spread", "Bear Call Spread", "Bearish", "Credit spread. Profits if price stays below the short call.", 2, false},
    {"put_backspread", "Put Ratio Backspread", "Bearish", "Short one near put against two lower puts. Long crash risk.", 2, false},
    {"synthetic_short", "Synthetic Short Stock", "Bearish", "Short call plus long put replicates a short position.", 2, false},
    {"put_ratio_spread", "Put Ratio Spread", "Bearish", "One long put against two short. Naked risk below the wing.", 2, false},
    {"covered_put", "Covered Put", "Bearish", "Short stock with a short put written against it.", 2, false},
    {"iron_condor", "Iron Condor", "Neutral", "Two credit spreads. Profits from low realised volatility.", 4, false},
    {"condor", "Condor (all calls)", "Neutral", "Four-strike call condor with a flat profit plateau.", 4, false},
    {"call_butterfly", "Call Butterfly", "Neutral", "Targets a price pin at expiry. Cheap, low probability.", 3, false},
    {"put_butterfly", "Put Butterfly", "Neutral", "Same payoff shape as the call butterfly, built from puts.", 3, false},
    {"iron_butterfly", "Iron Butterfly", "Neutral", "Short straddle with protective wings. Higher credit than a condor.", 4, false},
    {"broken_wing_butterfly", "Broken-Wing Butterfly", "Neutral", "Asymmetric wings remove risk on one side, add it on the other.", 3, false},
    {"short_straddle", "Short Straddle", "Neutral", "Maximum premium, unlimited risk both ways.", 2, false},
    {"short_strangle", "Short Strangle", "Neutral", "Wider profit zone than a short straddle, less credit.", 2, false},
    {"jade_lizard", "Jade Lizard", "Neutral", "Short put plus short call spread, sized to have no upside risk.", 3, false},
    {"box_spread", "Box Spread", "Neutral", "Synthetic long and short combined. A financing trade, not a directional one.", 4, false},
    {"long_straddle", "Long Straddle", "Volatility", "Profits from a large move either way.", 2, false},
    {"long_strangle", "Long Strangle", "Volatility", "Cheaper than a straddle, needs a larger move.", 2, false},
    {"reverse_iron_condor", "Reverse Iron Condor", "Volatility", "Debit structure that pays on a breakout past either wing.", 4, false},
    {"long_guts", "Long Guts", "Volatility", "Both legs in the money. High debit, high intrinsic value.", 2, false},
    {"strip", "Strip", "Volatility", "Straddle weighted to the downside — two puts per call.", 2, false},
    {"strap", "Strap", "Volatility", "Straddle weighted to the upside — two calls per put.", 2, false},
    {"calendar_spread", "Calendar Spread", "Volatility", "Sell the near expiry, buy the far one at the same strike.", 2, true},
    {"diagonal_spread", "Diagonal Spread", "Volatility", "Calendar with different strikes on each expiry.", 2, true},
    {"double_diagonal", "Double Diagonal", "Volatility", "Diagonals on both sides. Long vega, short theta near the money.", 4, true},
    {"covered_call", "Covered Call", "Income & Hedge", "Long stock with a short call written against it.", 2, false},
    {"cash_secured_put", "Cash-Secured Put", "Income & Hedge", "Sell a put you are willing to be assigned on.", 1, false},
    {"protective_put", "Protective Put", "Income & Hedge", "Long stock with a put as insurance.", 2, false},
    {"collar", "Collar", "Income & Hedge", "Protective put financed by a covered call.", 3, false},
    {"pmcc", "Poor Man's Covered Call", "Income & Hedge", "Deep ITM LEAP call stands in for the shares.", 2, true},
    {"futures_long", "Futures Outright Long", "Futures", "Directional front-month futures position.", 1, false},
    {"futures_short", "Futures Outright Short", "Futures", "Directional short futures position.", 1, false},
    {"futures_calendar", "Futures Calendar Spread", "Futures", "Inter-month spread along the term structure.", 2, true},
    {"spark_spread", "Spark Spread", "Futures", "Power against gas at a given heat rate.", 2, false},
    {"crush_spread", "Soybean Crush Spread", "Futures", "Beans against oil and meal.", 3, false},
    {"cash_and_carry", "Cash & Carry / Basis", "Futures", "Long spot against short futures. Captures the carry.", 2, false},
    {"covered_futures_call", "Covered Futures Call (FOP)", "Futures", "Long futures hedged with a short out-of-the-money future option.", 2, false},
    {"min_variance_hedge", "Minimum-Variance Hedge", "Futures", "Short futures sized by the beta hedge ratio.", 2, false},
}};

/** The full catalogue, for building refusal/clarification messages. */
export [[nodiscard]] constexpr auto all() noexcept -> std::span<const StrategyInfo> {
    return kCatalogue;
}

/** How many strategies are known. Expected to be 47; see the header comment above. */
export [[nodiscard]] constexpr auto count() noexcept -> std::size_t {
    return kCatalogue.size();
}

/** Looks up a strategy by its exact wire id. Returns nullptr if unknown. */
export [[nodiscard]] constexpr auto find(std::string_view id) noexcept -> const StrategyInfo* {
    const auto it = std::ranges::find_if(
        kCatalogue, [id](const StrategyInfo& s) noexcept { return s.id == id; });
    return it == kCatalogue.end() ? nullptr : &*it;
}

/**
 * Whether `id` is one of the strategies this backend actually knows how to
 * price. The gate an LLM assistant RPC must apply to its own output before
 * returning it -- ANYTHING outside this set must be refused, never passed
 * through and never approximated to the closest known id.
 */
export [[nodiscard]] constexpr auto is_known(std::string_view id) noexcept -> bool {
    return find(id) != nullptr;
}

}  // namespace options_calculator::strategy
