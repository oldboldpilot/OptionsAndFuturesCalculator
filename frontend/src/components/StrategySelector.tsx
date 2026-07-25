'use client';

import React, { useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import './StrategySelector.css';

type StrategyType = 'call_spread' | 'put_spread' | 'straddle' | 'strangle' | 'iron_condor' | 'butterfly' | 'covered_call' | 'futures_spread';

interface StrategyOption {
  id: StrategyType;
  name: string;
  description: string;
  icon: string;
  legs: { action: 'Buy' | 'Sell'; type: 'Call' | 'Put' | 'Stock' | 'Future'; strikeOffset: string }[];
}

const strategies: StrategyOption[] = [
  {
    id: 'call_spread',
    name: 'Bull Call Spread',
    description: 'Bullish strategy with limited risk and reward.',
    icon: '📈',
    legs: [
      { action: 'Buy', type: 'Call', strikeOffset: 'ATM' },
      { action: 'Sell', type: 'Call', strikeOffset: 'OTM' }
    ]
  },
  {
    id: 'put_spread',
    name: 'Bear Put Spread',
    description: 'Bearish strategy with limited risk and reward.',
    icon: '📉',
    legs: [
      { action: 'Buy', type: 'Put', strikeOffset: 'ATM' },
      { action: 'Sell', type: 'Put', strikeOffset: 'OTM' }
    ]
  },
  {
    id: 'straddle',
    name: 'Long Straddle',
    description: 'Neutral strategy profiting from high volatility.',
    icon: '⚡',
    legs: [
      { action: 'Buy', type: 'Call', strikeOffset: 'ATM' },
      { action: 'Buy', type: 'Put', strikeOffset: 'ATM' }
    ]
  },
  {
    id: 'strangle',
    name: 'Long Strangle',
    description: 'Cheaper neutral strategy needing larger moves.',
    icon: '🌊',
    legs: [
      { action: 'Buy', type: 'Call', strikeOffset: 'OTM' },
      { action: 'Buy', type: 'Put', strikeOffset: 'OTM' }
    ]
  },
  {
    id: 'iron_condor',
    name: 'Iron Condor',
    description: 'Neutral strategy profiting from low volatility.',
    icon: '🦅',
    legs: [
      { action: 'Sell', type: 'Call', strikeOffset: 'OTM' },
      { action: 'Buy', type: 'Call', strikeOffset: 'Far OTM' },
      { action: 'Sell', type: 'Put', strikeOffset: 'OTM' },
      { action: 'Buy', type: 'Put', strikeOffset: 'Far OTM' }
    ]
  },
  {
    id: 'butterfly',
    name: 'Call Butterfly',
    description: 'Targeted strategy for a specific price pin.',
    icon: '🦋',
    legs: [
      { action: 'Buy', type: 'Call', strikeOffset: 'ITM' },
      { action: 'Sell', type: 'Call', strikeOffset: 'ATM (x2)' },
      { action: 'Buy', type: 'Call', strikeOffset: 'OTM' }
    ]
  },
  {
    id: 'covered_call',
    name: 'Covered Call',
    description: 'Holding long stock and selling a call against it.',
    icon: '🛡️',
    legs: [
      { action: 'Buy', type: 'Stock', strikeOffset: 'Current Price' },
      { action: 'Sell', type: 'Call', strikeOffset: 'OTM' }
    ]
  },
  {
    id: 'futures_spread',
    name: 'Futures Spread',
    description: 'Multi-leg spread using futures contracts.',
    icon: '🚀',
    legs: [
      { action: 'Buy', type: 'Future', strikeOffset: 'Near Term' },
      { action: 'Sell', type: 'Future', strikeOffset: 'Far Term' }
    ]
  }
];

export const StrategySelector: React.FC = () => {
  const [selectedStrategy, setSelectedStrategy] = useState<StrategyType | null>(null);
  const [saveName, setSaveName] = useState('');
  const { addLeg, clearLegs, calculateStrategy, saveStrategy, legs } = useCalculatorStore();

  const selectedData = strategies.find(s => s.id === selectedStrategy);

  const applyStrategy = () => {
    if (!selectedData) return;
    
    clearLegs();
    
    // Add new legs based on template
    selectedData.legs.forEach(legDef => {
      // mapping 'Stock' to INSTRUMENT_EQUITY_SPOT, 'Future' to INSTRUMENT_FUTURES_SPOT
      let instrumentType = 'INSTRUMENT_EQUITY_OPTION';
      if (legDef.type === 'Stock') instrumentType = 'INSTRUMENT_EQUITY_SPOT';
      if (legDef.type === 'Future') instrumentType = 'INSTRUMENT_FUTURES_SPOT';
      
      addLeg({
        instrument_type: instrumentType,
        action: legDef.action.toUpperCase(),
        quantity: 1,
        strike_price: legDef.strikeOffset === 'ATM' ? 150.0 : legDef.strikeOffset === 'Current Price' ? 150.0 : legDef.strikeOffset.includes('ITM') ? 145.0 : 155.0,
        option_type: legDef.type.toUpperCase(),
        premium: legDef.type === 'Stock' ? 150.0 : 5.0,
        implied_volatility: 0.20
      });
    });
    
    // Automatically recalculate
    calculateStrategy();
  };

  return (
    <div className="glass-panel strategy-selector">
      <h2 className="strategy-header">Build Strategy</h2>
      
      <div className="strategy-grid">
        {strategies.map((strategy) => (
          <div 
            key={strategy.id} 
            className={`strategy-card ${selectedStrategy === strategy.id ? 'selected' : ''}`}
            onClick={() => setSelectedStrategy(strategy.id)}
          >
            <div className="strategy-icon">{strategy.icon}</div>
            <div className="strategy-title">{strategy.name}</div>
            <div className="strategy-desc">{strategy.description}</div>
          </div>
        ))}
      </div>

      {selectedData && (
        <div className="strategy-details">
          <h3 className="details-title">
            <span>{selectedData.icon}</span>
            {selectedData.name} Setup
          </h3>
          <div className="legs-list">
            {selectedData.legs.map((leg, i) => (
              <div key={i} className="leg-item">
                <div className="leg-main-info">
                  <span className={`leg-action ${leg.action.toLowerCase()}`}>{leg.action}</span>
                  <span>1 {leg.type}</span>
                </div>
                <div className="leg-strike-info">Strike: {leg.strikeOffset}</div>
              </div>
            ))}
          </div>
          <div className="flex gap-2" style={{ marginTop: '1rem', width: '100%' }}>
            <button className="btn flex-1" onClick={applyStrategy}>
              Apply Strategy
            </button>
            {legs.length > 0 && (
              <button 
                className="btn flex-1 bg-purple-600/50 hover:bg-purple-600/80" 
                onClick={() => saveStrategy(saveName || selectedData.name, 'SPY')}
              >
                Save Portfolio
              </button>
            )}
          </div>
          {legs.length > 0 && (
             <input
               type="text"
               placeholder="Strategy Name (e.g. My SPY Hedge)"
               className="mt-2 w-full p-2 rounded bg-white/5 border border-white/10 text-sm"
               value={saveName}
               onChange={(e) => setSaveName(e.target.value)}
             />
          )}
        </div>
      )}
    </div>
  );
};
