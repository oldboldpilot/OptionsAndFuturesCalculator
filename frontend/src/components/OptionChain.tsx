'use client';

import React, { useState, useMemo } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import styles from './OptionChain.module.css';

interface OptionStrikeRow {
  strike: number;
  callBid: number;
  callAsk: number;
  callDelta: number;
  callVolume: number;
  callOI: number;
  callIV: number;
  putBid: number;
  putAsk: number;
  putDelta: number;
  putVolume: number;
  putOI: number;
  putIV: number;
  isATM?: boolean;
}

interface FuturesContractRow {
  code: string;
  month: string;
  daysToExpiry: number;
  futuresPrice: number;
  bid: number;
  ask: number;
  basis: number;
  annualizedYield: number;
  volume: number;
  openInterest: number;
  state: 'Contango' | 'Backwardation';
}

export default function OptionChain() {
  const { symbol, spotPrice, assetClass, addLeg, calculateStrategy } = useCalculatorStore();
  const [activeTab, setActiveTab] = useState<'options' | 'futures'>(assetClass === 'FUTURES' ? 'futures' : 'options');
  const [expiryDays, setExpiryDays] = useState<number>(30);

  // 1. Dynamic Options Chain Generator based on spotPrice
  const optionChainData = useMemo<OptionStrikeRow[]>(() => {
    const base = spotPrice;
    let step = 5;
    if (base >= 2000) step = 50;
    else if (base >= 500) step = 10;
    else if (base >= 100) step = 5;
    else if (base >= 20) step = 2.5;
    else step = 1;

    const atmStrike = Math.round(base / step) * step;
    const strikes: number[] = [];

    for (let i = -7; i <= 7; i++) {
      strikes.push(parseFloat((atmStrike + i * step).toFixed(2)));
    }

    return strikes.map((strike) => {
      const isATM = strike === atmStrike;
      const diff = strike - base;
      const distPct = diff / base;

      // Approximate options pricing for visualization
      const timeFactor = Math.sqrt(expiryDays / 365);
      const iv = 0.22 + Math.abs(distPct) * 0.1;
      const intrinsicCall = Math.max(0, base - strike);
      const intrinsicPut = Math.max(0, strike - base);
      const timeVal = base * iv * timeFactor * 0.4 * Math.exp(-Math.pow(distPct * 3, 2));

      const callEst = intrinsicCall + timeVal;
      const putEst = intrinsicPut + timeVal;

      const callBid = Math.max(0.05, parseFloat((callEst * 0.98).toFixed(2)));
      const callAsk = Math.max(0.10, parseFloat((callEst * 1.02).toFixed(2)));
      const putBid = Math.max(0.05, parseFloat((putEst * 0.98).toFixed(2)));
      const putAsk = Math.max(0.10, parseFloat((putEst * 1.02).toFixed(2)));

      // Delta approximation
      const callDelta = parseFloat((0.5 - distPct * 3).toFixed(2));
      const clampedCallDelta = Math.min(0.99, Math.max(0.01, callDelta));
      const putDelta = parseFloat((clampedCallDelta - 1).toFixed(2));

      const callVolume = Math.floor(Math.max(50, 5000 * Math.exp(-Math.abs(distPct) * 5)));
      const callOI = callVolume * 3 + 450;
      const putVolume = Math.floor(Math.max(40, 4500 * Math.exp(-Math.abs(distPct) * 5)));
      const putOI = putVolume * 3 + 320;

      return {
        strike,
        callBid,
        callAsk,
        callDelta: clampedCallDelta,
        callVolume,
        callOI,
        callIV: parseFloat((iv * 100).toFixed(1)),
        putBid,
        putAsk,
        putDelta,
        putVolume,
        putOI,
        putIV: parseFloat((iv * 100).toFixed(1)),
        isATM
      };
    });
  }, [spotPrice, expiryDays]);

  // 2. Dynamic Futures Contract Term Structure Generator
  const futuresChainData = useMemo<FuturesContractRow[]>(() => {
    const monthCodes = [
      { name: 'SEP 2026', code: `${symbol}U26`, days: 45, r: 0.052 },
      { name: 'DEC 2026', code: `${symbol}Z26`, days: 135, r: 0.050 },
      { name: 'MAR 2027', code: `${symbol}H27`, days: 225, r: 0.048 },
      { name: 'JUN 2027', code: `${symbol}M27`, days: 315, r: 0.046 },
      { name: 'SEP 2027', code: `${symbol}U27`, days: 405, r: 0.045 },
      { name: 'DEC 2027', code: `${symbol}Z27`, days: 495, r: 0.044 },
    ];

    return monthCodes.map((m) => {
      // Cost of carry model: F = S * e^(r * t)
      const t = m.days / 365;
      const forwardPrice = spotPrice * Math.exp(m.r * t);
      const basis = forwardPrice - spotPrice;
      const annualizedYield = (basis / spotPrice / t) * 100;
      const spread = spotPrice > 1000 ? 0.5 : spotPrice > 100 ? 0.1 : 0.02;

      return {
        code: m.code,
        month: m.name,
        daysToExpiry: m.days,
        futuresPrice: parseFloat(forwardPrice.toFixed(2)),
        bid: parseFloat((forwardPrice - spread).toFixed(2)),
        ask: parseFloat((forwardPrice + spread).toFixed(2)),
        basis: parseFloat(basis.toFixed(2)),
        annualizedYield: parseFloat(annualizedYield.toFixed(2)),
        volume: Math.floor(120000 / (t * 2 + 1)),
        openInterest: Math.floor(350000 / (t * 1.5 + 1)),
        state: basis >= 0 ? 'Contango' : 'Backwardation'
      };
    });
  }, [symbol, spotPrice]);

  const handleAddOptionLeg = (action: 'BUY' | 'SELL', type: 'CALL' | 'PUT', strike: number, premium: number) => {
    addLeg({
      instrument_type: 'EQUITY_OPTION',
      action,
      option_type: type,
      strike_price: strike,
      premium,
      quantity: 1,
      implied_volatility: 0.22
    });
    calculateStrategy();
  };

  const handleAddFuturesLeg = (action: 'BUY' | 'SELL', price: number) => {
    addLeg({
      instrument_type: 'INSTRUMENT_FUTURES_SPOT',
      action,
      option_type: 'FUTURE',
      strike_price: price,
      premium: price,
      quantity: 1,
      implied_volatility: 0.20
    });
    calculateStrategy();
  };

  return (
    <div className={styles.container}>
      <div className={styles.header}>
        <div>
          <h2 className={styles.title}>{symbol} Market Chains</h2>
          <div className="flex gap-2 mt-1">
            <button 
              className={`px-3 py-1 text-xs rounded-md font-semibold transition ${activeTab === 'options' ? 'bg-sky-500 text-slate-950 font-bold' : 'bg-slate-800 text-slate-300 hover:bg-slate-700'}`}
              onClick={() => setActiveTab('options')}
            >
              Options Chain (Calls & Puts)
            </button>
            <button 
              className={`px-3 py-1 text-xs rounded-md font-semibold transition ${activeTab === 'futures' ? 'bg-sky-500 text-slate-950 font-bold' : 'bg-slate-800 text-slate-300 hover:bg-slate-700'}`}
              onClick={() => setActiveTab('futures')}
            >
              Futures Term Structure
            </button>
          </div>
        </div>

        {activeTab === 'options' && (
          <select className={styles.select} value={expiryDays} onChange={(e) => setExpiryDays(Number(e.target.value))}>
            <option value={7}>7 Days (Weekly)</option>
            <option value={14}>14 Days</option>
            <option value={30}>30 Days (Monthly)</option>
            <option value={60}>60 Days</option>
            <option value={90}>90 Days (Quarterly)</option>
            <option value={180}>180 Days</option>
          </select>
        )}
      </div>

      {/* OPTIONS CHAIN TABLE */}
      {activeTab === 'options' ? (
        <div className={styles.tableWrapper}>
          <table className={styles.chainTable}>
            <thead>
              <tr>
                <th colSpan={5} className={styles.callHeader}>CALLS</th>
                <th className={styles.strikeHeader}>STRIKE</th>
                <th colSpan={5} className={styles.putHeader}>PUTS</th>
              </tr>
              <tr className={styles.subHeader}>
                <th>Delta</th>
                <th>OI</th>
                <th>Vol</th>
                <th>Bid</th>
                <th>Ask</th>
                <th></th>
                <th>Bid</th>
                <th>Ask</th>
                <th>Vol</th>
                <th>OI</th>
                <th>Delta</th>
              </tr>
            </thead>
            <tbody>
              {optionChainData.map((row) => (
                <tr key={row.strike} className={`${styles.row} ${row.isATM ? 'bg-sky-500/10 border-y border-sky-500/30' : ''}`}>
                  <td className={styles.cell} style={{ color: '#38bdf8' }}>{row.callDelta}</td>
                  <td className={styles.cell}>{row.callOI.toLocaleString()}</td>
                  <td className={styles.cell}>{row.callVolume.toLocaleString()}</td>
                  <td 
                    className={`${styles.cell} ${styles.bid} cursor-pointer hover:bg-emerald-500/20`}
                    title="Click to Sell Call"
                    onClick={() => handleAddOptionLeg('SELL', 'CALL', row.strike, row.callBid)}
                  >
                    ${row.callBid.toFixed(2)}
                  </td>
                  <td 
                    className={`${styles.cell} ${styles.ask} cursor-pointer hover:bg-emerald-500/20`}
                    title="Click to Buy Call"
                    onClick={() => handleAddOptionLeg('BUY', 'CALL', row.strike, row.callAsk)}
                  >
                    ${row.callAsk.toFixed(2)}
                  </td>

                  <td className={`${styles.strikeCell} ${row.isATM ? 'text-sky-400 font-extrabold' : ''}`}>
                    {row.strike} {row.isATM ? ' (ATM)' : ''}
                  </td>

                  <td 
                    className={`${styles.cell} ${styles.bid} cursor-pointer hover:bg-rose-500/20`}
                    title="Click to Buy Put"
                    onClick={() => handleAddOptionLeg('BUY', 'PUT', row.strike, row.putAsk)}
                  >
                    ${row.putAsk.toFixed(2)}
                  </td>
                  <td 
                    className={`${styles.cell} ${styles.ask} cursor-pointer hover:bg-rose-500/20`}
                    title="Click to Sell Put"
                    onClick={() => handleAddOptionLeg('SELL', 'PUT', row.strike, row.putBid)}
                  >
                    ${row.putBid.toFixed(2)}
                  </td>
                  <td className={styles.cell}>{row.putVolume.toLocaleString()}</td>
                  <td className={styles.cell}>{row.putOI.toLocaleString()}</td>
                  <td className={styles.cell} style={{ color: '#f43f5e' }}>{row.putDelta}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : (
        /* FUTURES TERM STRUCTURE CHAIN TABLE */
        <div className={styles.tableWrapper}>
          <table className={styles.chainTable}>
            <thead>
              <tr className={styles.subHeader}>
                <th style={{ textAlign: 'left' }}>Contract Code</th>
                <th style={{ textAlign: 'left' }}>Delivery Month</th>
                <th>Days to Expiry</th>
                <th>Futures Price</th>
                <th>Bid</th>
                <th>Ask</th>
                <th>Basis (vs Spot)</th>
                <th>Cost of Carry (p.a)</th>
                <th>Volume</th>
                <th>Open Interest</th>
                <th>Action</th>
              </tr>
            </thead>
            <tbody>
              {futuresChainData.map((row) => (
                <tr key={row.code} className={styles.row}>
                  <td className={styles.cell} style={{ textAlign: 'left', fontWeight: 'bold', color: '#38bdf8' }}>
                    {row.code}
                  </td>
                  <td className={styles.cell} style={{ textAlign: 'left', fontWeight: '600' }}>
                    {row.month}
                  </td>
                  <td className={styles.cell}>{row.daysToExpiry}d</td>
                  <td className={styles.cell} style={{ fontWeight: 'bold', color: '#f8fafc' }}>
                    ${row.futuresPrice.toLocaleString('en-US', { minimumFractionDigits: 2 })}
                  </td>
                  <td className={`${styles.cell} ${styles.bid}`}>${row.bid.toFixed(2)}</td>
                  <td className={`${styles.cell} ${styles.ask}`}>${row.ask.toFixed(2)}</td>
                  <td className={styles.cell} style={{ color: row.basis >= 0 ? '#4ade80' : '#f87171' }}>
                    {row.basis >= 0 ? `+${row.basis.toFixed(2)}` : row.basis.toFixed(2)}
                  </td>
                  <td className={styles.cell}>{row.annualizedYield}%</td>
                  <td className={styles.cell}>{row.volume.toLocaleString()}</td>
                  <td className={styles.cell}>{row.openInterest.toLocaleString()}</td>
                  <td className={styles.cell}>
                    <div className="flex gap-1 justify-end">
                      <button 
                        className="px-2 py-0.5 text-xs bg-emerald-600/60 hover:bg-emerald-500 text-white rounded font-bold"
                        onClick={() => handleAddFuturesLeg('BUY', row.futuresPrice)}
                      >
                        Buy
                      </button>
                      <button 
                        className="px-2 py-0.5 text-xs bg-rose-600/60 hover:bg-rose-500 text-white rounded font-bold"
                        onClick={() => handleAddFuturesLeg('SELL', row.futuresPrice)}
                      >
                        Sell
                      </button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}

