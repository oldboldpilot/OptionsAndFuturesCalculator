import React, { useState } from 'react';
import './StrategySelector.css';

type StrategyType = 'call_spread' | 'put_spread' | 'straddle' | 'strangle' | 'iron_condor' | 'butterfly';

interface StrategyOption {
  id: StrategyType;
  name: string;
  description: string;
  icon: string;
  legs: { action: 'Buy' | 'Sell'; type: 'Call' | 'Put'; strikeOffset: string }[];
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
  }
];

export const StrategySelector: React.FC = () => {
  const [selectedStrategy, setSelectedStrategy] = useState<StrategyType | null>(null);

  const selectedData = strategies.find(s => s.id === selectedStrategy);

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
        </div>
      )}
    </div>
  );
};
