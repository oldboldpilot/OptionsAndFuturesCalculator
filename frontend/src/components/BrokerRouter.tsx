'use client';

import React, { useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';

const BROKERS = [
  { id: 'tastytrade', name: 'tastytrade', color: '#ff4c3b' },
  { id: 'schwab', name: 'Charles Schwab', color: '#006cfa' },
  { id: 'ibkr', name: 'Interactive Brokers', color: '#d0021b' },
];

export const BrokerRouter: React.FC = () => {
  const { legs, spotPrice } = useCalculatorStore();
  const [isRouting, setIsRouting] = useState(false);

  const handleRouteToBroker = async (brokerId: string) => {
    setIsRouting(true);
    
    // Simulate Supabase Edge Function call to log the CPA attribution lead event
    // await supabase.functions.invoke('log_broker_lead', { body: { brokerId, strategy: legs } });
    
    setTimeout(() => {
      setIsRouting(false);
      
      // Mock deep link construction
      let url = `https://${brokerId}.com/trade?underlying=SPY&spot=${spotPrice}`;
      legs.forEach((leg, i) => {
        url += `&leg${i}_action=${leg.action}&leg${i}_type=${leg.option_type}&leg${i}_strike=${leg.strike_price}`;
      });
      
      alert(`Deep-linking to partner broker order ticket (CPA Event Triggered):\n\n${url}`);
    }, 600);
  };

  return (
    <div className="glass-panel" style={{ marginTop: '1.5rem', padding: '1.5rem' }}>
      <h3 style={{ fontSize: '1.1rem', fontWeight: 600, color: 'var(--text-primary)', marginBottom: '1rem' }}>
        Execute via Partner Broker
      </h3>
      <p style={{ fontSize: '0.9rem', color: 'var(--text-secondary)', marginBottom: '1.5rem' }}>
        Directly route this exact strategy into the order ticket of a supported partner broker.
      </p>
      
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(150px, 1fr))', gap: '1rem' }}>
        {BROKERS.map(broker => (
          <button 
            key={broker.id}
            onClick={() => handleRouteToBroker(broker.id)}
            disabled={isRouting || legs.length === 0}
            style={{ 
              background: `linear-gradient(135deg, ${broker.color}33 0%, transparent 100%)`, 
              border: `1px solid ${broker.color}66`,
              padding: '1rem',
              borderRadius: '8px',
              color: 'var(--text-primary)',
              fontWeight: 500,
              cursor: (isRouting || legs.length === 0) ? 'not-allowed' : 'pointer',
              transition: 'all 0.2s',
              opacity: (isRouting || legs.length === 0) ? 0.5 : 1
            }}
            onMouseOver={(e) => {
              if (!isRouting && legs.length > 0) e.currentTarget.style.boxShadow = `0 4px 12px ${broker.color}44`;
            }}
            onMouseOut={(e) => {
              e.currentTarget.style.boxShadow = 'none';
            }}
          >
            {isRouting ? 'Routing...' : `Trade with ${broker.name}`}
          </button>
        ))}
      </div>
    </div>
  );
};
