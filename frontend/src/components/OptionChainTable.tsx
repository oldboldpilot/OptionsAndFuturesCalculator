import React, { useState } from 'react';
import './OptionChainTable.css';

interface OptionData {
  strike: number;
  call: {
    bid: number;
    ask: number;
    vol: number;
    oi: number;
    delta: number;
    gamma: number;
    iv: number;
  };
  put: {
    bid: number;
    ask: number;
    vol: number;
    oi: number;
    delta: number;
    gamma: number;
    iv: number;
  };
}

const mockData: OptionData[] = [
  {
    strike: 4950,
    call: { bid: 152.1, ask: 153.5, vol: 1204, oi: 5430, delta: 0.65, gamma: 0.02, iv: 0.15 },
    put: { bid: 45.2, ask: 46.1, vol: 320, oi: 1540, delta: -0.35, gamma: 0.02, iv: 0.16 },
  },
  {
    strike: 5000,
    call: { bid: 120.5, ask: 121.8, vol: 3500, oi: 12500, delta: 0.52, gamma: 0.03, iv: 0.14 },
    put: { bid: 65.4, ask: 66.5, vol: 2100, oi: 8900, delta: -0.48, gamma: 0.03, iv: 0.14 },
  },
  {
    strike: 5050,
    call: { bid: 92.4, ask: 93.6, vol: 2100, oi: 6700, delta: 0.38, gamma: 0.03, iv: 0.13 },
    put: { bid: 90.1, ask: 91.5, vol: 1500, oi: 4200, delta: -0.62, gamma: 0.03, iv: 0.13 },
  },
  {
    strike: 5100,
    call: { bid: 68.2, ask: 69.5, vol: 5400, oi: 15800, delta: 0.25, gamma: 0.02, iv: 0.12 },
    put: { bid: 120.3, ask: 122.0, vol: 800, oi: 2100, delta: -0.75, gamma: 0.02, iv: 0.12 },
  },
  {
    strike: 5150,
    call: { bid: 48.5, ask: 49.5, vol: 1100, oi: 3400, delta: 0.15, gamma: 0.01, iv: 0.13 },
    put: { bid: 155.6, ask: 157.5, vol: 400, oi: 950, delta: -0.85, gamma: 0.01, iv: 0.14 },
  }
];

export const OptionChainTable: React.FC = () => {
  const [expiration, setExpiration] = useState('2026-08-21');
  const [underlyingPrice] = useState(5025.50);

  return (
    <div className="option-chain-container">
      <div className="option-chain-header">
        <h2 className="option-chain-title">SPX Options Chain</h2>
        <div className="option-chain-controls">
          <select 
            className="control-select" 
            value={expiration}
            onChange={(e) => setExpiration(e.target.value)}
          >
            <option value="2026-08-21">Aug 21, 2026 (28d)</option>
            <option value="2026-09-18">Sep 18, 2026 (56d)</option>
            <option value="2026-10-16">Oct 16, 2026 (84d)</option>
            <option value="2026-12-18">Dec 18, 2026 (147d)</option>
          </select>
          <select className="control-select">
            <option value="10">10 Strikes</option>
            <option value="20">20 Strikes</option>
            <option value="all">All Strikes</option>
          </select>
        </div>
      </div>
      
      <table className="option-table">
        <thead>
          <tr>
            <th colSpan={7} className="col-group-calls center" style={{ color: 'var(--success)', borderBottom: '2px solid var(--success)' }}>CALLS</th>
            <th className="center">STRIKE</th>
            <th colSpan={7} className="col-group-puts center" style={{ color: 'var(--danger)', borderBottom: '2px solid var(--danger)' }}>PUTS</th>
          </tr>
          <tr>
            {/* Calls */}
            <th className="left">Delta</th>
            <th>Gamma</th>
            <th>IV</th>
            <th>Vol</th>
            <th>OI</th>
            <th>Bid</th>
            <th className="col-group-calls">Ask</th>
            
            {/* Strike */}
            <th className="center">Price</th>
            
            {/* Puts */}
            <th className="col-group-puts">Bid</th>
            <th>Ask</th>
            <th>Vol</th>
            <th>OI</th>
            <th>IV</th>
            <th>Gamma</th>
            <th className="left">Delta</th>
          </tr>
        </thead>
        <tbody>
          {mockData.map((row, i) => {
            const callItm = row.strike < underlyingPrice;
            const putItm = row.strike > underlyingPrice;
            
            return (
              <tr key={i}>
                {/* Calls */}
                <td className={`left ${callItm ? 'itm-call' : ''}`}>{row.call.delta.toFixed(2)}</td>
                <td className={callItm ? 'itm-call' : ''}>{row.call.gamma.toFixed(2)}</td>
                <td className={callItm ? 'itm-call' : ''}>{(row.call.iv * 100).toFixed(1)}%</td>
                <td className={callItm ? 'itm-call' : ''}>{row.call.vol.toLocaleString()}</td>
                <td className={callItm ? 'itm-call' : ''}>{row.call.oi.toLocaleString()}</td>
                <td className={callItm ? 'itm-call' : ''}>{row.call.bid.toFixed(2)}</td>
                <td className={`col-group-calls ${callItm ? 'itm-call' : ''}`}>{row.call.ask.toFixed(2)}</td>
                
                {/* Strike */}
                <td className="center strike-cell">{row.strike}</td>
                
                {/* Puts */}
                <td className={`col-group-puts ${putItm ? 'itm-put' : ''}`}>{row.put.bid.toFixed(2)}</td>
                <td className={putItm ? 'itm-put' : ''}>{row.put.ask.toFixed(2)}</td>
                <td className={putItm ? 'itm-put' : ''}>{row.put.vol.toLocaleString()}</td>
                <td className={putItm ? 'itm-put' : ''}>{row.put.oi.toLocaleString()}</td>
                <td className={putItm ? 'itm-put' : ''}>{(row.put.iv * 100).toFixed(1)}%</td>
                <td className={putItm ? 'itm-put' : ''}>{row.put.gamma.toFixed(2)}</td>
                <td className={`left ${putItm ? 'itm-put' : ''}`}>{row.put.delta.toFixed(2)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
};
