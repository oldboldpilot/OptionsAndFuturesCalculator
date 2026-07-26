'use client';

import React, { useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import './SymbolSelector.css';

interface PresetSymbol {
  symbol: string;
  name: string;
  price: number;
  category: 'EQUITY' | 'FUTURES' | 'CRYPTO';
}

const PRESET_SYMBOLS: PresetSymbol[] = [
  // Equities & ETFs
  { symbol: 'SPY', name: 'S&P 500 ETF', price: 580.0, category: 'EQUITY' },
  { symbol: 'QQQ', name: 'Nasdaq 100 ETF', price: 490.0, category: 'EQUITY' },
  { symbol: 'NVDA', name: 'NVIDIA Corp', price: 125.0, category: 'EQUITY' },
  { symbol: 'AAPL', name: 'Apple Inc', price: 230.0, category: 'EQUITY' },
  { symbol: 'TSLA', name: 'Tesla Inc', price: 240.0, category: 'EQUITY' },
  
  // Futures Contracts
  { symbol: 'ES', name: 'E-mini S&P 500 Futures', price: 5850.0, category: 'FUTURES' },
  { symbol: 'NQ', name: 'E-mini Nasdaq Futures', price: 20400.0, category: 'FUTURES' },
  { symbol: 'CL', name: 'Crude Oil Futures', price: 78.50, category: 'FUTURES' },
  { symbol: 'GC', name: 'Gold Futures', price: 2420.0, category: 'FUTURES' },
  { symbol: 'ZB', name: '30Y T-Bond Futures', price: 118.25, category: 'FUTURES' },
  
  // Crypto Derivatives
  { symbol: 'BTC', name: 'Bitcoin Index', price: 67500.0, category: 'CRYPTO' },
  { symbol: 'ETH', name: 'Ethereum Index', price: 3500.0, category: 'CRYPTO' },
];

export const SymbolSelector: React.FC = () => {
  const { symbol, spotPrice, assetClass, setSymbol, setSpotPrice, calculateStrategy } = useCalculatorStore();
  const [inputVal, setInputVal] = useState(symbol);

  const handleSelect = (item: PresetSymbol) => {
    setInputVal(item.symbol);
    setSymbol(item.symbol, item.price, item.category);
    calculateStrategy();
  };

  const handleCustomSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!inputVal.trim()) return;
    const cleanSym = inputVal.trim().toUpperCase();
    const presetMatch = PRESET_SYMBOLS.find(p => p.symbol === cleanSym);
    
    if (presetMatch) {
      handleSelect(presetMatch);
    } else {
      setSymbol(cleanSym, spotPrice);
      calculateStrategy();
    }
  };

  return (
    <div className="symbol-selector-card glass-panel">
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

      {/* Symbol Search Bar */}
      <form onSubmit={handleCustomSubmit} className="symbol-search-form">
        <div className="input-wrapper">
          <svg className="search-icon" width="18" height="18" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" />
          </svg>
          <input 
            type="text" 
            className="symbol-input" 
            value={inputVal}
            onChange={(e) => setInputVal(e.target.value)}
            placeholder="Type ticker (e.g. SPY, NQ, CL, NVDA)..." 
          />
          <button type="submit" className="symbol-submit-btn">
            Apply
          </button>
        </div>
      </form>

      {/* Spot Price Manual Adjuster */}
      <div className="spot-price-adjuster">
        <label className="text-xs text-slate-400">Custom Underlying Spot Price ($):</label>
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

      {/* Quick Select Presets */}
      <div className="preset-groups">
        <div className="preset-group-title">Fast Select Preset Instruments:</div>
        <div className="preset-pills">
          {PRESET_SYMBOLS.map((item) => (
            <button
              key={item.symbol}
              type="button"
              className={`preset-pill ${symbol === item.symbol ? 'active' : ''}`}
              onClick={() => handleSelect(item)}
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
