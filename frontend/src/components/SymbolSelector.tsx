'use client';

import React, { useState, useMemo } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import './SymbolSelector.css';

interface SymbolPreset {
  symbol: string;
  name: string;
  price: number;
  category: 'EQUITY' | 'FUTURES' | 'CRYPTO';
}

const ALL_SYMBOLS: SymbolPreset[] = [
  // Equities & ETFs
  { symbol: 'SPY', name: 'S&P 500 ETF Trust', price: 580.0, category: 'EQUITY' },
  { symbol: 'QQQ', name: 'Invesco QQQ Trust', price: 490.0, category: 'EQUITY' },
  { symbol: 'IWM', name: 'iShares Russell 2000 ETF', price: 220.0, category: 'EQUITY' },
  { symbol: 'NVDA', name: 'NVIDIA Corp', price: 125.0, category: 'EQUITY' },
  { symbol: 'AAPL', name: 'Apple Inc', price: 230.0, category: 'EQUITY' },
  { symbol: 'MSFT', name: 'Microsoft Corp', price: 440.0, category: 'EQUITY' },
  { symbol: 'AMZN', name: 'Amazon.com Inc', price: 185.0, category: 'EQUITY' },
  { symbol: 'GOOGL', name: 'Alphabet Inc', price: 175.0, category: 'EQUITY' },
  { symbol: 'META', name: 'Meta Platforms Inc', price: 500.0, category: 'EQUITY' },
  { symbol: 'TSLA', name: 'Tesla Inc', price: 240.0, category: 'EQUITY' },
  { symbol: 'AMD', name: 'Advanced Micro Devices', price: 150.0, category: 'EQUITY' },
  { symbol: 'PLTR', name: 'Palantir Technologies', price: 28.50, category: 'EQUITY' },
  { symbol: 'COIN', name: 'Coinbase Global', price: 220.0, category: 'EQUITY' },

  // Futures Contracts
  { symbol: 'ES', name: 'E-mini S&P 500 Futures', price: 5850.0, category: 'FUTURES' },
  { symbol: 'NQ', name: 'E-mini Nasdaq 100 Futures', price: 20400.0, category: 'FUTURES' },
  { symbol: 'RTY', name: 'E-mini Russell 2000 Futures', price: 2220.0, category: 'FUTURES' },
  { symbol: 'CL', name: 'Crude Oil Futures (WTI)', price: 78.50, category: 'FUTURES' },
  { symbol: 'NG', name: 'Natural Gas Futures', price: 2.45, category: 'FUTURES' },
  { symbol: 'GC', name: 'Gold Futures', price: 2420.0, category: 'FUTURES' },
  { symbol: 'ZB', name: '30Y T-Bond Futures', price: 118.25, category: 'FUTURES' },

  // Crypto Derivatives
  { symbol: 'BTC', name: 'Bitcoin Index', price: 67500.0, category: 'CRYPTO' },
  { symbol: 'ETH', name: 'Ethereum Index', price: 3500.0, category: 'CRYPTO' },
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
      setSymbol(match.symbol, match.price, match.category);
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
                  <span className="text-xs font-semibold text-slate-200">${item.price.toLocaleString()}</span>
                  <span className="ml-2 text-[10px] uppercase font-bold text-sky-500/80 bg-sky-500/10 px-1.5 py-0.5 rounded">
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
              <span className="pill-price">${item.price > 1000 ? Math.round(item.price) : item.price}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  );
};
