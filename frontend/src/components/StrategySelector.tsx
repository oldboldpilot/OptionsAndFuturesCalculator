'use client';

import React, { useEffect, useMemo, useState } from 'react';
import { useCalculatorStore, type ChainStrike } from '../store/useCalculatorStore';
import { useAssistantStore } from '../store/useAssistantStore';
import { useProStatus } from '../lib/useProStatus';
import { ProPanel } from './ProPanel';

type Category = 'Bullish' | 'Bearish' | 'Neutral' | 'Volatility' | 'Income & Hedge' | 'Futures';

interface LegTemplate {
  action: 'BUY' | 'SELL';
  type: 'CALL' | 'PUT' | 'STOCK' | 'FUTURE';
  /** Strike as a fraction of spot. 1.0 = at the money. Null for non-strike legs. */
  moneyness: number | null;
  quantity?: number;
}

interface StrategyDef {
  id: string;
  name: string;
  category: Category;
  description: string;
  legs: LegTemplate[];
  /*
   * There was a `sensen?: string` here naming the internal C++ builder preset
   * behind each structure. It is gone because it was DATA, not a comment: it
   * shipped in the JS bundle and was readable in view-source on every page,
   * which is the same leak as the chip that used to render it. Comments are
   * stripped by the bundler; object fields are not.
   *
   * The engine mapping still exists on the server, which is the only place it
   * is needed — the client sends a strategy id and the engine resolves it.
   */
  /** Needs legs at two different expiries; the single-chain builder can't apply it. */
  multiExpiry?: boolean;
  /**
   * Hidden from the picker, deliberately and temporarily.
   *
   * This is NOT "the calculator cannot price it". The engine takes explicit
   * legs and never sees a strategy id, so anything listed here prices
   * correctly the moment it is shown. What it means is that the rest of the
   * product does not yet agree that this structure exists: the assistant's
   * training data and the backend catalogue are both generated from
   * agent/dataset/strategies.json, which does not contain it, so asking the
   * assistant for one in words gets an honest refusal for a structure the UI
   * is visibly offering. Hiding it keeps those two surfaces telling the same
   * story until the dataset catches up.
   *
   * To restore: delete the flag from the entry. Nothing else is required, and
   * nothing about the pricing path changed while it was set.
   */
  gated?: boolean;
}

/**
 * Strategy catalogue.
 *
 * The previous list carried twelve entries, which badly undersold the engine:
 * `backend/sensen/src/financial.cppm` ships thirteen named presets on
 * `OptionStrategyBuilder` alone (covered_call, bull/bear call and put spreads,
 * straddle, strangle, butterfly_spread, condor, iron_condor, iron_butterfly,
 * collar, protective_put), an arbitrary `add_leg` path that expresses any
 * combination on top of that, and a separate family of futures spreads
 * (calendar, 3-2-1 crack, spark, crush, minimum-variance hedge).
 */
const STRATEGIES: StrategyDef[] = [
  /* ---------------------------------- Bullish --------------------------- */
  { id: 'long_call', name: 'Long Call', category: 'Bullish',
    description: 'Unlimited upside, premium at risk.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }] },
  { id: 'bull_call_spread', name: 'Bull Call Spread', category: 'Bullish',
    description: 'Debit spread. Capped risk and reward.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'bull_put_spread', name: 'Bull Put Spread', category: 'Bullish',
    description: 'Credit spread. Profits if price holds above the short put.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 1.0 }, { action: 'BUY', type: 'PUT', moneyness: 0.95 }] },
  { id: 'call_backspread', name: 'Call Ratio Backspread', category: 'Bullish',
    description: 'Short one near call against two further calls. Long convexity.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'CALL', moneyness: 1.05, quantity: 2 }] },
  { id: 'risk_reversal', name: 'Risk Reversal', category: 'Bullish',
    description: 'Short put funds a long call. Synthetic long with a gap.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 0.95 }, { action: 'BUY', type: 'CALL', moneyness: 1.05 }] },
  { id: 'synthetic_long', name: 'Synthetic Long Stock', category: 'Bullish',
    description: 'Long call plus short put at one strike replicates the shares.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'SELL', type: 'PUT', moneyness: 1.0 }] },
  { id: 'call_ratio_spread', name: 'Call Ratio Spread', category: 'Bullish',
    description: 'One long call against two short. Naked risk above the wing.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'SELL', type: 'CALL', moneyness: 1.05, quantity: 2 }] },
  { id: 'bull_call_ladder', name: 'Bull Call Ladder', category: 'Bullish',
    description: 'Spread financed by a second short call further out.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'SELL', type: 'CALL', moneyness: 1.05 },
           { action: 'SELL', type: 'CALL', moneyness: 1.10 }] },

  /* ---------------------------------- Bearish --------------------------- */
  { id: 'long_put', name: 'Long Put', category: 'Bearish',
    description: 'Defined risk downside exposure.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 1.0 }] },
  { id: 'bear_put_spread', name: 'Bear Put Spread', category: 'Bearish',
    description: 'Debit spread. Capped risk and reward.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 1.0 }, { action: 'SELL', type: 'PUT', moneyness: 0.95 }] },
  { id: 'bear_call_spread', name: 'Bear Call Spread', category: 'Bearish',
    description: 'Credit spread. Profits if price stays below the short call.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'CALL', moneyness: 1.05 }] },
  { id: 'put_backspread', name: 'Put Ratio Backspread', category: 'Bearish',
    description: 'Short one near put against two lower puts. Long crash risk.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 1.0 }, { action: 'BUY', type: 'PUT', moneyness: 0.95, quantity: 2 }] },
  { id: 'synthetic_short', name: 'Synthetic Short Stock', category: 'Bearish',
    description: 'Short call plus long put replicates a short position.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'PUT', moneyness: 1.0 }] },
  { id: 'put_ratio_spread', name: 'Put Ratio Spread', category: 'Bearish',
    description: 'One long put against two short. Naked risk below the wing.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 1.0 }, { action: 'SELL', type: 'PUT', moneyness: 0.95, quantity: 2 }] },
  { id: 'covered_put', name: 'Covered Put', category: 'Bearish',
    description: 'Short stock with a short put written against it.',
    legs: [{ action: 'SELL', type: 'STOCK', moneyness: null }, { action: 'SELL', type: 'PUT', moneyness: 0.95 }] },

  /* ---------------------------------- Neutral --------------------------- */
  { id: 'iron_condor', name: 'Iron Condor', category: 'Neutral',
    description: 'Two credit spreads. Profits from low realised volatility.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 0.90 }, { action: 'SELL', type: 'PUT', moneyness: 0.95 },
           { action: 'SELL', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'CALL', moneyness: 1.10 }] },
  { id: 'condor', name: 'Condor (all calls)', category: 'Neutral',
    description: 'Four-strike call condor with a flat profit plateau.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.92 }, { action: 'SELL', type: 'CALL', moneyness: 0.97 },
           { action: 'SELL', type: 'CALL', moneyness: 1.03 }, { action: 'BUY', type: 'CALL', moneyness: 1.08 }] },
  { id: 'call_butterfly', name: 'Call Butterfly', category: 'Neutral',
    description: 'Targets a price pin at expiry. Cheap, low probability.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.95 }, { action: 'SELL', type: 'CALL', moneyness: 1.0, quantity: 2 },
           { action: 'BUY', type: 'CALL', moneyness: 1.05 }] },
  { id: 'put_butterfly', name: 'Put Butterfly', category: 'Neutral',
    description: 'Same payoff shape as the call butterfly, built from puts.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 0.95 }, { action: 'SELL', type: 'PUT', moneyness: 1.0, quantity: 2 },
           { action: 'BUY', type: 'PUT', moneyness: 1.05 }] },
  { id: 'iron_butterfly', name: 'Iron Butterfly', category: 'Neutral',
    description: 'Short straddle with protective wings. Higher credit than a condor.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 0.95 }, { action: 'SELL', type: 'PUT', moneyness: 1.0 },
           { action: 'SELL', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'CALL', moneyness: 1.05 }] },
  { id: 'broken_wing_butterfly', name: 'Broken-Wing Butterfly', category: 'Neutral',
    description: 'Asymmetric wings remove risk on one side, add it on the other.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.97 }, { action: 'SELL', type: 'CALL', moneyness: 1.02, quantity: 2 },
           { action: 'BUY', type: 'CALL', moneyness: 1.12 }] },
  { id: 'short_straddle', name: 'Short Straddle', category: 'Neutral',
    description: 'Maximum premium, unlimited risk both ways.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.0 }, { action: 'SELL', type: 'PUT', moneyness: 1.0 }] },
  { id: 'short_strangle', name: 'Short Strangle', category: 'Neutral',
    description: 'Wider profit zone than a short straddle, less credit.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.05 }, { action: 'SELL', type: 'PUT', moneyness: 0.95 }] },
  { id: 'jade_lizard', name: 'Jade Lizard', category: 'Neutral',
    description: 'Short put plus short call spread, sized to have no upside risk.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 0.95 }, { action: 'SELL', type: 'CALL', moneyness: 1.05 },
           { action: 'BUY', type: 'CALL', moneyness: 1.10 }] },
  { id: 'box_spread', name: 'Box Spread', category: 'Neutral',
    description: 'Synthetic long and short combined. A financing trade, not a directional one.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.95 }, { action: 'SELL', type: 'PUT', moneyness: 0.95 },
           { action: 'SELL', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'PUT', moneyness: 1.05 }] },

  /* --------------------------------- Volatility ------------------------- */
  { id: 'long_straddle', name: 'Long Straddle', category: 'Volatility',
    description: 'Profits from a large move either way.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'PUT', moneyness: 1.0 }] },
  { id: 'long_strangle', name: 'Long Strangle', category: 'Volatility',
    description: 'Cheaper than a straddle, needs a larger move.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'PUT', moneyness: 0.95 }] },
  { id: 'reverse_iron_condor', name: 'Reverse Iron Condor', category: 'Volatility',
    description: 'Debit structure that pays on a breakout past either wing.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 0.90 }, { action: 'BUY', type: 'PUT', moneyness: 0.95 },
           { action: 'BUY', type: 'CALL', moneyness: 1.05 }, { action: 'SELL', type: 'CALL', moneyness: 1.10 }] },
  { id: 'long_guts', name: 'Long Guts', category: 'Volatility',
    description: 'Both legs in the money. High debit, high intrinsic value.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.95 }, { action: 'BUY', type: 'PUT', moneyness: 1.05 }] },
  { id: 'strip', name: 'Strip', category: 'Volatility',
    description: 'Straddle weighted to the downside — two puts per call.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'PUT', moneyness: 1.0, quantity: 2 }] },
  { id: 'strap', name: 'Strap', category: 'Volatility',
    description: 'Straddle weighted to the upside — two calls per put.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0, quantity: 2 }, { action: 'BUY', type: 'PUT', moneyness: 1.0 }] },
  { id: 'calendar_spread', name: 'Calendar Spread', category: 'Volatility', multiExpiry: true,
    description: 'Sell the near expiry, buy the far one at the same strike.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'CALL', moneyness: 1.0 }] },
  { id: 'diagonal_spread', name: 'Diagonal Spread', category: 'Volatility', multiExpiry: true,
    description: 'Calendar with different strikes on each expiry.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'CALL', moneyness: 1.0 }] },
  { id: 'double_diagonal', name: 'Double Diagonal', category: 'Volatility', multiExpiry: true,
    description: 'Diagonals on both sides. Long vega, short theta near the money.',
    legs: [{ action: 'SELL', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'CALL', moneyness: 1.10 },
           { action: 'SELL', type: 'PUT', moneyness: 0.95 }, { action: 'BUY', type: 'PUT', moneyness: 0.90 }] },

  /* ------------------------------ Income & Hedge ------------------------ */
  { id: 'covered_call', name: 'Covered Call', category: 'Income & Hedge',
    description: 'Long stock with a short call written against it.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'cash_secured_put', name: 'Cash-Secured Put', category: 'Income & Hedge',
    description: 'Sell a put you are willing to be assigned on.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 0.95 }] },
  { id: 'protective_put', name: 'Protective Put', category: 'Income & Hedge',
    description: 'Long stock with a put as insurance.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'BUY', type: 'PUT', moneyness: 0.95 }] },
  { id: 'collar', name: 'Collar', category: 'Income & Hedge',
    description: 'Protective put financed by a covered call.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'BUY', type: 'PUT', moneyness: 0.95 },
           { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'pmcc', name: "Poor Man's Covered Call", category: 'Income & Hedge', multiExpiry: true,
    description: 'Deep ITM LEAP call stands in for the shares.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.80 }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },

  /* ---------------------------------- Futures --------------------------- */
  { id: 'futures_long', name: 'Futures Outright Long', category: 'Futures',
    description: 'Directional front-month futures position.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }] },
  { id: 'futures_short', name: 'Futures Outright Short', category: 'Futures',
    description: 'Directional short futures position.',
    legs: [{ action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'futures_calendar', name: 'Futures Calendar Spread', category: 'Futures', multiExpiry: true,
    description: 'Inter-month spread along the term structure.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'crack_321', name: '3-2-1 Crack Spread', category: 'Futures', gated: true,
    description: 'Three crude against two gasoline and one heating oil.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null, quantity: 3 },
           { action: 'SELL', type: 'FUTURE', moneyness: null, quantity: 2 },
           { action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'spark_spread', name: 'Spark Spread', category: 'Futures',
    description: 'Power against gas at a given heat rate.',
    legs: [{ action: 'SELL', type: 'FUTURE', moneyness: null }, { action: 'BUY', type: 'FUTURE', moneyness: null }] },
  { id: 'crush_spread', name: 'Soybean Crush Spread', category: 'Futures',
    description: 'Beans against oil and meal.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null },
           { action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'cash_and_carry', name: 'Cash & Carry / Basis', category: 'Futures',
    description: 'Long spot against short futures. Captures the carry.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'covered_futures_call', name: 'Covered Futures Call (FOP)', category: 'Futures',
    description: 'Long futures hedged with a short out-of-the-money future option.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'min_variance_hedge', name: 'Minimum-Variance Hedge', category: 'Futures',
    description: 'Short futures sized by the beta hedge ratio.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null }] },
];

const CATEGORIES: Category[] = [
  'Bullish', 'Bearish', 'Neutral', 'Volatility', 'Income & Hedge', 'Futures',
];

/**
 * The catalogue as the assistant needs to see it: which ids a user can
 * actually select, and which of those need two expiries.
 *
 * Derived from `STRATEGIES` above rather than restated, because the assistant
 * and the picker disagreeing about which structures exist is precisely the
 * failure the `gated` flag was introduced to prevent -- a parse that selects
 * `crack_321` would point at a picker entry that is deliberately not there.
 * Exported as data, not as a component concern, so the assistant's own store
 * can be told what is offered without importing React.
 */
export const SELECTABLE_STRATEGY_IDS: string[] =
  STRATEGIES.filter((s) => !s.gated).map((s) => s.id);

export const MULTI_EXPIRY_STRATEGY_IDS: string[] =
  STRATEGIES.filter((s) => s.multiExpiry).map((s) => s.id);

/** Display name for a catalogue id, or the id itself when this build has none. */
export function strategyDisplayName(id: string): string {
  return STRATEGIES.find((s) => s.id === id)?.name ?? id;
}

/** Closest listed strike to a target. Returns null when the chain is empty. */
function nearestStrike(strikes: ChainStrike[], target: number): ChainStrike | null {
  if (strikes.length === 0) return null;
  return strikes.reduce((best, s) =>
    Math.abs(s.strike - target) < Math.abs(best.strike - target) ? s : best,
  );
}

export const StrategySelector: React.FC = () => {
  const [category, setCategory] = useState<Category>('Bullish');
  const [query, setQuery] = useState('');
  const [selected, setSelected] = useState<string | null>(null);

  const {
    addLeg, clearLegs, spotPrice,
    chainStrikes, chainExpirations, selectedExpiration, chainStatus,
    futuresCurve, assetClass,
  } = useCalculatorStore();

  // Only the flag is needed here -- the panel below owns the rest of the
  // subscription UI. Both read the same store, so activating a licence in one
  // updates the badges in the other.
  const { pro } = useProStatus();

  /*
   * A parse the user applied selects its structure HERE, in the picker they
   * already use, rather than opening a second way to build a position. The
   * assistant deliberately gets no Apply of its own: the legs still come off
   * the live chain through the button below, so an assisted position and a
   * hand-built one are the same position built the same way.
   *
   * Keyed on `applySeq`, not on the id: applying the same parse twice does not
   * change the id, and a user who applied, then picked something else by hand,
   * then pressed Apply again would otherwise watch the button do nothing.
   *
   * Written as a SUBSCRIPTION rather than as an effect over a selected value.
   * The two are not interchangeable here: an effect body that calls setState
   * synchronously re-renders on every apply and is what
   * `react-hooks/set-state-in-effect` refuses. Subscribing to the store and
   * setting state from its callback is the shape that rule points at -- the
   * store is an external system, and this component is reacting to a change in
   * it rather than deriving state it could have computed while rendering.
   */
  useEffect(() =>
    useAssistantStore.subscribe((st, prev) => {
      if (st.applySeq === prev.applySeq || !st.selectedStrategyId) return;
      const target = STRATEGIES.find((s) => s.id === st.selectedStrategyId);
      // The store checks this list before it ever sets an id, so a miss here
      // means the two disagree. Selecting nothing is the safe answer -- the
      // assistant panel is already saying why it could not apply.
      if (!target) return;
      setSelected(st.selectedStrategyId);
      // Clear any search and switch to the owning category, or the newly
      // selected entry sits in a list the user is not looking at.
      setQuery('');
      setCategory(target.category);
    }),
  []);

  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    // A search spans every category — with this many structures, forcing the
    // user to guess which bucket "jade lizard" lives in is hostile.
    // `gated` is checked before anything else so a hidden structure cannot be
    // reached by search either -- filtering it out of the category list alone
    // would still surface it the moment someone typed its name, which is the
    // exact path a user takes when they know the strategy exists.
    return STRATEGIES.filter((s) =>
      !s.gated && (
        q ? s.name.toLowerCase().includes(q) || s.description.toLowerCase().includes(q)
          : s.category === category
      ),
    );
  }, [category, query]);

  const def = STRATEGIES.find((s) => s.id === selected);
  const dte = chainExpirations.find((e) => e.date === selectedExpiration)?.dte ?? 0;

  /**
   * Why Apply is unavailable, or null when it is ready.
   *
   * Futures used to be refused outright with "no provider supplies a term
   * structure yet". That stopped being true when the curve was wired up, but
   * the guard stayed -- so every futures strategy in the catalogue was
   * permanently unavailable while the panel beside it displayed the very curve
   * the message said did not exist. The gate now asks the store.
   */
  const blocker = useMemo(() => {
    if (!def) return 'Select a strategy';

    if (def.category === 'Futures') {
      if (assetClass !== 'FUTURES') {
        return 'Select a futures symbol first — ES, NQ, CL, GC or ZB';
      }
      if (futuresCurve.length === 0) {
        return chainStatus === 'loading' ? 'Loading the term structure' : 'No term structure for this symbol';
      }
      // A calendar or inter-commodity spread needs two contracts to sit
      // between; with only the front month listed there is no spread to take.
      if (def.multiExpiry && futuresCurve.length < 2) {
        return 'Needs two listed contracts — only the front month is quoted';
      }
      return null;
    }

    if (def.multiExpiry) return 'Needs two expiries — add these legs from two chains';
    if (spotPrice <= 0) return 'Awaiting quote';
    if (chainStatus !== 'ready') return 'Awaiting option chain';
    return null;
  }, [def, spotPrice, chainStatus, assetClass, futuresCurve.length]);

  /**
   * Builds the position from live chain quotes: nearest listed strike to the
   * template's moneyness, premium lifted from the ask when buying and hit on
   * the bid when selling, IV taken from that contract. Nothing is synthesised
   * — if the chain is not ready, Apply is disabled instead (spec §3.4).
   */
  function apply() {
    if (!def || blocker) return;
    clearLegs();
    // Walks the curve as futures legs are consumed, so a calendar spread gets
    // two different delivery months rather than the same one twice.
    let futuresLegIndex = 0;

    for (const t of def.legs) {
      if (t.type === 'STOCK' || t.type === 'FUTURE') {
        // A futures leg is priced at the FORWARD for its delivery month, not at
        // spot. Using spot for both legs of a calendar spread would value the
        // spread at exactly zero -- the basis between the two contracts is the
        // entire position. Legs are taken in order from the curve, so the first
        // futures leg is the front month and the second the next listed one.
        const isFuture = t.type === 'FUTURE';
        const contract = isFuture ? futuresCurve[futuresLegIndex] : undefined;
        if (isFuture) futuresLegIndex += 1;

        const price = contract ? contract.futuresPrice : spotPrice;
        if (isFuture && !contract) continue;  // never synthesise a leg (spec 3.4)

        addLeg({
          instrument_type: t.type === 'STOCK' ? 'INSTRUMENT_EQUITY_SPOT' : 'INSTRUMENT_FUTURES_SPOT',
          action: t.action,
          option_type: t.type,
          strike_price: price,
          premium: price,
          quantity: t.quantity ?? 1,
          expiration_days: contract ? contract.daysToExpiry : dte,
        });
        continue;
      }

      const row = nearestStrike(chainStrikes, spotPrice * (t.moneyness ?? 1));
      if (!row) continue;
      const q = t.type === 'CALL' ? row.call : row.put;

      addLeg({
        instrument_type: 'INSTRUMENT_EQUITY_OPTION',
        action: t.action,
        option_type: t.type,
        strike_price: row.strike,
        premium: t.action === 'BUY' ? q.ask : q.bid,
        quantity: t.quantity ?? 1,
        expiration_days: dte,
        implied_volatility: q.iv,
      });
    }
  }

  /** Preview strike for a template leg — the real listed one when we have it. */
  function previewStrike(t: LegTemplate): string {
    if (t.moneyness === null) return '';
    if (chainStatus === 'ready') {
      const row = nearestStrike(chainStrikes, spotPrice * t.moneyness);
      if (row) return ` ${row.strike}`;
    }
    if (spotPrice > 0) return ` ~${(spotPrice * t.moneyness).toFixed(0)}`;
    return '';
  }

  return (
    <div className="panel" style={{ flex: 'none', maxHeight: '58vh' }}>
      <div className="panel-head">
        <span className="panel-title">Strategy</span>
        {/* Counts what the user can actually pick, not what the array holds --
            a gated entry that still incremented this would advertise a
            structure the picker refuses to show. */}
        <span className="chip">{STRATEGIES.filter((s) => !s.gated).length}</span>
      </div>

      <div style={{ padding: '0.4375rem 0.5rem 0', display: 'flex', flexDirection: 'column', gap: '0.375rem' }}>
        <input
          className="input"
          placeholder="Search all strategies…"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          aria-label="Search strategies"
        />
        {!query && (
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.25rem' }}>
            {CATEGORIES.map((c) => (
              <button
                key={c}
                className="btn"
                data-active={category === c}
                onClick={() => { setCategory(c); setSelected(null); }}
                style={{
                  padding: '0.125rem 0.4375rem',
                  background: category === c ? 'var(--color-base-500)' : 'transparent',
                  borderColor: category === c ? 'var(--color-line-strong)' : 'var(--color-line)',
                  color: category === c ? 'var(--color-ink-100)' : 'var(--color-ink-300)',
                }}
              >
                {c}
              </button>
            ))}
          </div>
        )}
      </div>

      <div className="panel-body panel-body--flush" style={{ marginTop: '0.4375rem' }}>
        {visible.length === 0 ? (
          <div className="empty-state">
            <span>No strategy matches “{query}”.</span>
          </div>
        ) : (
          visible.map((s) => {
            const isSel = selected === s.id;
            return (
              <button
                key={s.id}
                onClick={() => setSelected(s.id)}
                className="animate-fade"
                style={{
                  display: 'block',
                  width: '100%',
                  textAlign: 'left',
                  padding: '0.375rem 0.625rem',
                  background: isSel ? 'var(--color-base-600)' : 'transparent',
                  borderLeft: `2px solid ${isSel ? 'var(--color-accent)' : 'transparent'}`,
                  borderTop: 0, borderRight: 0,
                  borderBottom: '1px solid var(--color-line-soft)',
                  cursor: 'pointer',
                  color: 'inherit',
                  font: 'inherit',
                  transition: 'background 0.14s var(--ease-out), border-color 0.14s var(--ease-out)',
                }}
              >
                <div style={{ display: 'flex', alignItems: 'center', gap: '0.3125rem' }}>
                  <span
                    style={{
                      fontSize: 'var(--text-xs)',
                      fontWeight: 600,
                      color: isSel ? 'var(--color-ink-100)' : 'var(--color-ink-200)',
                    }}
                  >
                    {s.name}
                  </span>
                  <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
                    {s.legs.length}L
                  </span>
                  {/*
                    Marked, not disabled. The strategy still selects and still
                    builds its legs -- what the engine refuses is the
                    calculation, and it says so in its own words. Blocking the
                    click here would replace a specific server-side refusal with
                    a vaguer client-side one, and would stop someone seeing the
                    structure they are being asked to pay for.
                  */}
                  {s.legs.length > 1 && !pro && (
                    <span
                      title="Multi-leg strategies need Pro"
                      style={{
                        fontSize: 'var(--text-2xs)',
                        fontWeight: 700,
                        letterSpacing: '0.06em',
                        padding: '0 0.1875rem',
                        color: 'var(--color-accent)',
                        border: '1px solid var(--color-accent)',
                      }}
                    >
                      PRO
                    </span>
                  )}
                  {query && (
                    <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
                      · {s.category}
                    </span>
                  )}
                </div>
                <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
                  {s.description}
                </div>
              </button>
            );
          })
        )}
      </div>

      {def && (
        <div
          className="animate-rise"
          style={{
            padding: '0.5rem 0.625rem',
            borderTop: '1px solid var(--color-line)',
            background: 'var(--color-base-700)',
          }}
        >
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.25rem', marginBottom: '0.4375rem' }}>
            {def.legs.map((t, i) => (
              <span key={i} className="chip">
                <span className={t.action === 'BUY' ? 'profit' : 'loss'}>
                  {t.action === 'BUY' ? '+' : '−'}
                </span>
                {(t.quantity ?? 1) > 1 ? `${t.quantity}× ` : ''}
                {t.type}
                {previewStrike(t)}
              </span>
            ))}
            {/*
              No "sensen preset" chip here any more. It rendered the name of an
              internal C++ builder to the reader -- "sensen preset", with the
              symbol itself in the tooltip -- which tells a trader nothing about
              the position and leaks the shape of the engine behind the site.
              The `sensen` field is kept on the strategy records below because it
              documents which engine preset backs each entry, which is worth
              having in source. It is not worth putting on screen.
            */}
          </div>

          <button
            className="btn btn-primary"
            style={{ width: '100%' }}
            onClick={apply}
            disabled={blocker !== null}
          >
            {blocker ??
              (def?.category === 'Futures'
                // A futures position has no option expiry to name; the label
                // read "Apply at  (0d)" because it was reaching for a chain
                // this symbol does not have. Name the delivery month instead,
                // which is the equivalent fact for a futures leg.
                ? `Apply on ${futuresCurve[0]?.code ?? 'front month'}${
                    def.multiExpiry && futuresCurve[1] ? ` / ${futuresCurve[1].code}` : ''
                  }`
                : `Apply at ${selectedExpiration} (${dte}d)`)}
          </button>
        </div>
      )}

      {/*
        Last in the column, below the strategy detail. Subscription state is
        something you check occasionally, not something you act on while
        building a position, so it sits where it can be found rather than where
        it interrupts.
      */}
      <ProPanel />
    </div>
  );
};

export default StrategySelector;
