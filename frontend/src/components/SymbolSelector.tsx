'use client';

import React, { useState, useMemo } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import './SymbolSelector.css';

/**
 * A selectable instrument.
 *
 * Symbol, display name and asset class are reference data — facts about *what
 * the instrument is* — so carrying them here is legitimate. Price deliberately
 * is not a field. This table previously held one (SPY at 580.0 against a real
 * 738.93) and passed it to setSymbol as an explicit override, which made the
 * store skip the quote fetch entirely (see useCalculatorStore setSymbol: it
 * returns early when customPrice is defined). The fabricated number therefore
 * did not merely flash before the real one arrived — it *was* the spot price
 * for the rest of the session, and every payoff, Greek and probability
 * computed off it. Prices come from the backend quote service only.
 * See spec §3.4, real data only.
 */
interface SymbolPreset {
  symbol: string;
  name: string;
  category: 'EQUITY' | 'FUTURES' | 'CRYPTO';
}

const ALL_SYMBOLS: SymbolPreset[] = [
  // Equities & ETFs
  { symbol: 'SPY', name: 'S&P 500 ETF Trust', category: 'EQUITY' },
  { symbol: 'QQQ', name: 'Invesco QQQ Trust', category: 'EQUITY' },
  { symbol: 'IWM', name: 'iShares Russell 2000 ETF', category: 'EQUITY' },
  { symbol: 'NVDA', name: 'NVIDIA Corp', category: 'EQUITY' },
  { symbol: 'AAPL', name: 'Apple Inc', category: 'EQUITY' },
  { symbol: 'MSFT', name: 'Microsoft Corp', category: 'EQUITY' },
  { symbol: 'AMZN', name: 'Amazon.com Inc', category: 'EQUITY' },
  { symbol: 'GOOGL', name: 'Alphabet Inc', category: 'EQUITY' },
  { symbol: 'META', name: 'Meta Platforms Inc', category: 'EQUITY' },
  { symbol: 'TSLA', name: 'Tesla Inc', category: 'EQUITY' },
  { symbol: 'AMD', name: 'Advanced Micro Devices', category: 'EQUITY' },
  { symbol: 'PLTR', name: 'Palantir Technologies', category: 'EQUITY' },
  { symbol: 'COIN', name: 'Coinbase Global', category: 'EQUITY' },

  // Futures Contracts
  { symbol: 'ES', name: 'E-mini S&P 500 Futures', category: 'FUTURES' },
  { symbol: 'NQ', name: 'E-mini Nasdaq 100 Futures', category: 'FUTURES' },
  { symbol: 'RTY', name: 'E-mini Russell 2000 Futures', category: 'FUTURES' },
  { symbol: 'CL', name: 'Crude Oil Futures (WTI)', category: 'FUTURES' },
  { symbol: 'NG', name: 'Natural Gas Futures', category: 'FUTURES' },
  { symbol: 'GC', name: 'Gold Futures', category: 'FUTURES' },
  { symbol: 'ZB', name: '30Y T-Bond Futures', category: 'FUTURES' },

  // Crypto Derivatives
  { symbol: 'BTC', name: 'Bitcoin Index', category: 'CRYPTO' },
  { symbol: 'ETH', name: 'Ethereum Index', category: 'CRYPTO' },
];

export const SymbolSelector: React.FC = () => {
  const { symbol, spotPrice, assetClass, setSymbol, setSpotPrice, calculateStrategy } = useCalculatorStore();
  const [query, setQuery] = useState(symbol);
  const [isOpen, setIsOpen] = useState(false);

  // Filter suggestions dynamically as user types
  const suggestions = useMemo(() => {
    if (!query.trim()) return ALL_SYMBOLS.slice(0, 6);
    const q = query.trim().toUpperCase();
    return ALL_SYMBOLS.filter(s => s.symbol.includes(q) || s.name.toUpperCase().includes(q));
  }, [query]);

  const applySymbol = (symItem?: SymbolPreset | string) => {
    let targetSym = typeof symItem === 'string' ? symItem : symItem?.symbol || query;
    if (!targetSym.trim()) return;

    targetSym = targetSym.trim().toUpperCase();
    const match = ALL_SYMBOLS.find(s => s.symbol === targetSym);

    if (match) {
      setQuery(match.symbol);
      // No price argument: setSymbol treats a defined customPrice as a user
      // simulation override and skips the quote fetch. Passing undefined is
      // what makes it ask the backend for the real spot.
      setSymbol(match.symbol, undefined, match.category);
    } else {
      setQuery(targetSym);
      setSymbol(targetSym);
    }

    setIsOpen(false);
    calculateStrategy();
  };

  const handleFormSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    applySymbol();
  };

  return (
    <div className="symbol-selector-card glass-panel relative">
      <div className="symbol-selector-header">
        <div>
          <span className="symbol-category-badge">{assetClass}</span>
          <h2 className="symbol-title">{symbol} Target Instrument</h2>
        </div>
        <div className="spot-price-badge">
          <span>Spot:</span>
          <strong>${spotPrice.toLocaleString('en-US', { minimumFractionDigits: 2 })}</strong>
        </div>
      </div>

      {/* Symbol Search Bar with Live Suggestions */}
      <div className="relative">
        <form onSubmit={handleFormSubmit} className="symbol-search-form">
          <div className="input-wrapper">
            <svg className="search-icon" width="18" height="18" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
            </svg>
            <input 
              type="text" 
              className="symbol-input" 
              value={query}
              onChange={(e) => {
                setQuery(e.target.value);
                setIsOpen(true);
              }}
              onFocus={() => setIsOpen(true)}
              placeholder="Search ticker (e.g. SPY, NQ, CL, NVDA, BTC)..." 
            />
            <button type="submit" className="symbol-submit-btn">
              Apply
            </button>
          </div>
        </form>

        {/* Dynamic Dropdown Search Results */}
        {isOpen && suggestions.length > 0 && (
          <div className="absolute top-full left-0 right-0 mt-1 bg-slate-900/95 border border-slate-700/80 rounded-lg shadow-2xl z-50 overflow-hidden backdrop-blur-md">
            {suggestions.map((item) => (
              <div 
                key={item.symbol}
                className="px-4 py-2.5 hover:bg-sky-500/20 cursor-pointer flex justify-between items-center transition border-b border-slate-800/50 last:border-0"
                onClick={() => applySymbol(item)}
              >
                <div>
                  <span className="font-bold text-sky-400 mr-2">{item.symbol}</span>
                  <span className="text-xs text-slate-300">{item.name}</span>
                </div>
                <div className="text-right">
                  <span className="text-[10px] uppercase font-bold text-sky-500/80 bg-sky-500/10 px-1.5 py-0.5 rounded">
                    {item.category}
                  </span>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Spot Price Manual Adjuster */}
      <div className="spot-price-adjuster">
        <label className="text-xs text-slate-400 font-semibold">Custom Spot Price ($):</label>
        <div className="flex gap-2 mt-1">
          <input 
            type="number" 
            step="0.1"
            className="spot-num-input"
            value={spotPrice}
            onChange={(e) => {
              const val = parseFloat(e.target.value);
              if (!isNaN(val)) setSpotPrice(val);
            }}
          />
          <button 
            type="button" 
            className="btn btn-sm bg-blue-600/30 hover:bg-blue-600/60"
            onClick={() => calculateStrategy()}
          >
            Update Model
          </button>
        </div>
      </div>

      {/* Fast Select Preset Pills */}
      <div className="preset-groups">
        <div className="preset-group-title">Fast Select Presets:</div>
        <div className="preset-pills">
          {ALL_SYMBOLS.slice(0, 10).map((item) => (
            <button
              key={item.symbol}
              type="button"
              className={`preset-pill ${symbol === item.symbol ? 'active' : ''}`}
              onClick={() => applySymbol(item)}
            >
              <span className="pill-sym">{item.symbol}</span>
              <span className="pill-class">{item.category}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  );
};
