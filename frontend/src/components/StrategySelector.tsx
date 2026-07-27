'use client';

import React, { useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';

type Category = 'Options' | 'Futures' | 'Hybrid';

interface LegTemplate {
  action: 'BUY' | 'SELL';
  type: 'CALL' | 'PUT' | 'STOCK' | 'FUTURE';
  /** Strike as a fraction of spot. 1.0 = at the money. Null for non-strike legs. */
  moneyness: number | null;
  quantity?: number;
}

interface StrategyOption {
  id: string;
  name: string;
  category: Category;
  description: string;
  legs: LegTemplate[];
}

const STRATEGIES: StrategyOption[] = [
  { id: 'call_spread', name: 'Bull Call Spread', category: 'Options', description: 'Bullish, limited risk and reward.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'put_spread', name: 'Bear Put Spread', category: 'Options', description: 'Bearish, limited risk and reward.',
    legs: [{ action: 'BUY', type: 'PUT', moneyness: 1.0 }, { action: 'SELL', type: 'PUT', moneyness: 0.95 }] },
  { id: 'straddle', name: 'Long Straddle', category: 'Options', description: 'Neutral, profits from large moves either way.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.0 }, { action: 'BUY', type: 'PUT', moneyness: 1.0 }] },
  { id: 'strangle', name: 'Long Strangle', category: 'Options', description: 'Cheaper than a straddle, needs a larger move.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'PUT', moneyness: 0.95 }] },
  { id: 'iron_condor', name: 'Iron Condor', category: 'Options', description: 'Neutral, profits from low realised volatility.',
    legs: [{ action: 'SELL', type: 'PUT', moneyness: 0.95 }, { action: 'BUY', type: 'PUT', moneyness: 0.90 },
           { action: 'SELL', type: 'CALL', moneyness: 1.05 }, { action: 'BUY', type: 'CALL', moneyness: 1.10 }] },
  { id: 'butterfly', name: 'Call Butterfly', category: 'Options', description: 'Targets a specific price pin at expiry.',
    legs: [{ action: 'BUY', type: 'CALL', moneyness: 0.95 }, { action: 'SELL', type: 'CALL', moneyness: 1.0, quantity: 2 },
           { action: 'BUY', type: 'CALL', moneyness: 1.05 }] },
  { id: 'covered_call', name: 'Covered Call', category: 'Options', description: 'Long stock with a short call written against it.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'futures_outright', name: 'Futures Outright Long', category: 'Futures', description: 'Directional front-month futures position.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }] },
  { id: 'futures_calendar_spread', name: 'Futures Calendar Spread', category: 'Futures', description: 'Inter-month spread on the term structure.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'futures_intercommodity_spread', name: 'Crack / Inter-Commodity Spread', category: 'Futures', description: 'Relative value between related products.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null }] },
  { id: 'covered_futures_call', name: 'Covered Futures Call (FOP)', category: 'Hybrid', description: 'Long futures hedged with a short OTM future option.',
    legs: [{ action: 'BUY', type: 'FUTURE', moneyness: null }, { action: 'SELL', type: 'CALL', moneyness: 1.05 }] },
  { id: 'futures_basis_arbitrage', name: 'Cash & Carry / Basis', category: 'Hybrid', description: 'Long spot against a short futures contract.',
    legs: [{ action: 'BUY', type: 'STOCK', moneyness: null }, { action: 'SELL', type: 'FUTURE', moneyness: null }] },
];

const CATEGORIES: Category[] = ['Options', 'Futures', 'Hybrid'];

export const StrategySelector: React.FC = () => {
  const [category, setCategory] = useState<Category>('Options');
  const [selected, setSelected] = useState<string | null>(null);
  const { addLeg, clearLegs, spotPrice } = useCalculatorStore();

  const visible = STRATEGIES.filter((s) => s.category === category);
  const selectedData = STRATEGIES.find((s) => s.id === selected);

  /**
   * Strikes are derived from the live spot rather than the hardcoded 150.0 the
   * previous build used. Premium is deliberately left at 0: a fill price is
   * market data, and inventing one would violate spec §3.4. It is populated
   * when the leg is priced against the option chain.
   */
  function apply() {
    if (!selectedData || spotPrice <= 0) return;
    clearLegs();
    for (const t of selectedData.legs) {
      const instrument =
        t.type === 'STOCK' ? 'INSTRUMENT_EQUITY_SPOT'
        : t.type === 'FUTURE' ? 'INSTRUMENT_FUTURES_SPOT'
        : 'INSTRUMENT_EQUITY_OPTION';
      addLeg({
        instrument_type: instrument,
        action: t.action,
        option_type: t.type,
        quantity: t.quantity ?? 1,
        strike_price: t.moneyness === null ? spotPrice : roundStrike(spotPrice * t.moneyness),
        premium: 0,
      });
    }
  }

  /** Snap to a plausible listed increment for the price level. */
  function roundStrike(v: number) {
    const inc = v >= 1000 ? 25 : v >= 200 ? 5 : v >= 50 ? 1 : 0.5;
    return Math.round(v / inc) * inc;
  }

  return (
    <div className="panel" style={{ flex: 'none' }}>
      <div className="panel-head">
        <span className="panel-title">Strategy</span>
        <div className="segment">
          {CATEGORIES.map((c) => (
            <button
              key={c}
              className="segment-item"
              data-active={category === c}
              onClick={() => { setCategory(c); setSelected(null); }}
            >
              {c}
            </button>
          ))}
        </div>
      </div>

      <div className="panel-body panel-body--flush">
        {visible.map((s) => {
          const isSel = selected === s.id;
          return (
            <button
              key={s.id}
              onClick={() => setSelected(s.id)}
              style={{
                display: 'block',
                width: '100%',
                textAlign: 'left',
                padding: '0.4375rem 0.625rem',
                background: isSel ? 'var(--color-base-600)' : 'transparent',
                borderLeft: `2px solid ${isSel ? 'var(--color-accent)' : 'transparent'}`,
                borderTop: 0, borderRight: 0,
                borderBottom: '1px solid var(--color-line-soft)',
                cursor: 'pointer',
                color: 'inherit',
                font: 'inherit',
              }}
            >
              <div
                style={{
                  fontSize: 'var(--text-xs)',
                  fontWeight: 600,
                  color: isSel ? 'var(--color-ink-100)' : 'var(--color-ink-200)',
                }}
              >
                {s.name}
              </div>
              <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
                {s.description}
              </div>
            </button>
          );
        })}
      </div>

      {selectedData && (
        <div
          style={{
            padding: '0.5rem 0.625rem',
            borderTop: '1px solid var(--color-line)',
            background: 'var(--color-base-700)',
            borderRadius: '0 0 var(--radius) var(--radius)',
          }}
        >
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.25rem', marginBottom: '0.4375rem' }}>
            {selectedData.legs.map((t, i) => (
              <span key={i} className="chip">
                <span className={t.action === 'BUY' ? 'profit' : 'loss'}>
                  {t.action === 'BUY' ? '+' : '−'}
                </span>
                {(t.quantity ?? 1) > 1 ? `${t.quantity}× ` : ''}
                {t.type}
                {t.moneyness !== null && spotPrice > 0
                  ? ` ${roundStrike(spotPrice * t.moneyness)}`
                  : ''}
              </span>
            ))}
          </div>
          <button
            className="btn btn-primary"
            style={{ width: '100%' }}
            onClick={apply}
            disabled={spotPrice <= 0}
          >
            {spotPrice > 0 ? 'Apply to position' : 'Awaiting quote'}
          </button>
        </div>
      )}
    </div>
  );
};

export default StrategySelector;
