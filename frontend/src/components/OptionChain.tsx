'use client';

import React, { useState, useMemo, useEffect } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import styles from './OptionChain.module.css';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import { ChainRequest } from '../grpc/calculator_pb';

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

const EXPIRATIONS_LIST = [
  { days: 0, label: '0 DTE (Same-Day Expiring)' },
  { days: 7, label: '7 DTE (Weekly - Aug 01, 2026)' },
  { days: 14, label: '14 DTE (Weekly - Aug 08, 2026)' },
  { days: 30, label: '30 DTE (Monthly - Aug 25, 2026)' },
  { days: 45, label: '45 DTE (Monthly - Sep 09, 2026)' },
  { days: 60, label: '60 DTE (Quarterly - Sep 24, 2026)' },
  { days: 90, label: '90 DTE (Quarterly - Oct 24, 2026)' },
  { days: 180, label: '180 DTE (Semi-Annual - Jan 22, 2027)' },
  { days: 365, label: '365 DTE (1-Year LEAPs - Jul 26, 2027)' },
  { days: 540, label: '540 DTE (2-Year LEAPs - Jan 21, 2028)' },
  { days: 730, label: '730 DTE (2.5-Year LEAPs - Jul 26, 2028)' },
  { days: 900, label: '900 DTE (3-Year LEAPs - Jan 19, 2029)' }
];

export default function OptionChain() {
  const { symbol, spotPrice, assetClass, addLeg, calculateStrategy } = useCalculatorStore();
  const [activeTab, setActiveTab] = useState<'options' | 'futures'>(assetClass === 'FUTURES' ? 'futures' : 'options');
  const [expiryDays, setExpiryDays] = useState<number>(30);
  const [optionSideFilter, setOptionSideFilter] = useState<'all' | 'calls' | 'puts'>('all');
  const [remoteStrikes, setRemoteStrikes] = useState<OptionStrikeRow[]>([]);
  const [remoteFutures, setRemoteFutures] = useState<FuturesContractRow[]>([]);

  useEffect(() => {
    try {
      const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
      const client = new OptionsCalculatorClient(backendUrl);
      const req = new ChainRequest();
      req.setSymbol(symbol);
      req.setExpirationDays(expiryDays);
      req.setAssetClass(assetClass);

      client.getMarketChain(req, {}, (err, res) => {
        if (!err && res) {
          const strikes = res.getOptionStrikesList().map((s: any) => ({
            strike: s.getStrike(),
            callBid: s.getCallBid(),
            callAsk: s.getCallAsk(),
            callDelta: s.getCallDelta(),
            callVolume: s.getCallVolume(),
            callOI: s.getCallOpenInterest(),
            callIV: s.getCallIv(),
            putBid: s.getPutBid(),
            putAsk: s.getPutAsk(),
            putDelta: s.getPutDelta(),
            putVolume: s.getPutVolume(),
            putOI: s.getPutOpenInterest(),
            putIV: s.getPutIv(),
            isATM: s.getIsAtm()
          }));
          if (strikes.length > 0) setRemoteStrikes(strikes);

          const futures = res.getFuturesContractsList().map((f: any) => ({
            code: f.getCode(),
            month: f.getDeliveryMonth(),
            daysToExpiry: f.getDaysToExpiry(),
            futuresPrice: f.getFuturesPrice(),
            bid: f.getBid(),
            ask: f.getAsk(),
            basis: f.getBasis(),
            annualizedYield: f.getAnnualizedYield(),
            volume: f.getVolume(),
            openInterest: f.getOpenInterest(),
            state: f.getState() as 'Contango' | 'Backwardation'
          }));
          if (futures.length > 0) setRemoteFutures(futures);
        }
      });
    } catch (e) {
      console.warn('Backend chain lookup fallback:', e);
    }
  }, [symbol, expiryDays, assetClass]);

  // Dynamic Options Chain Generator
  const optionChainData = useMemo<OptionStrikeRow[]>(() => {
    if (remoteStrikes.length > 0) return remoteStrikes;

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

      const timeFactor = Math.sqrt((expiryDays > 0 ? expiryDays : 1) / 365);
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
  }, [spotPrice, expiryDays, remoteStrikes]);

  // Dynamic Futures Term Structure Generator
  const futuresChainData = useMemo<FuturesContractRow[]>(() => {
    if (remoteFutures.length > 0) return remoteFutures;

    const monthCodes = [
      { name: 'SEP 2026', code: `${symbol}U26`, days: 45, r: 0.052 },
      { name: 'DEC 2026', code: `${symbol}Z26`, days: 135, r: 0.050 },
      { name: 'MAR 2027', code: `${symbol}H27`, days: 225, r: 0.048 },
      { name: 'JUN 2027', code: `${symbol}M27`, days: 315, r: 0.046 },
      { name: 'SEP 2027', code: `${symbol}U27`, days: 405, r: 0.045 },
      { name: 'DEC 2027', code: `${symbol}Z27`, days: 495, r: 0.044 },
      { name: 'JAN 2028', code: `${symbol}F28`, days: 540, r: 0.043 },
      { name: 'JUN 2028', code: `${symbol}M28`, days: 680, r: 0.042 },
      { name: 'DEC 2028', code: `${symbol}Z28`, days: 870, r: 0.041 },
      { name: 'JAN 2029', code: `${symbol}F29`, days: 900, r: 0.040 },
    ];

    return monthCodes.map((m) => {
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
  }, [symbol, spotPrice, remoteFutures]);

  const handleAddOptionLeg = (action: 'BUY' | 'SELL', type: 'CALL' | 'PUT', strike: number, premium: number) => {
    addLeg({
      instrument_type: 'EQUITY_OPTION',
      action,
      option_type: type,
      strike_price: strike,
      premium,
      quantity: 1,
      expiration_days: expiryDays,
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
      {/* Top Header & Tab Navigation */}
      <div className="flex flex-col md:flex-row md:items-center justify-between gap-4 pb-4 border-b border-slate-800">
        <div>
          <h2 className="text-lg font-bold text-slate-100 flex items-center gap-2">
            <span>{symbol}</span>
            <span className="text-xs px-2 py-0.5 rounded bg-sky-500/20 text-sky-400 border border-sky-500/30">
              ${spotPrice.toFixed(2)} Spot
            </span>
          </h2>
          <div className="flex gap-2 mt-2">
            <button 
              className={`px-3 py-1.5 text-xs rounded-md font-semibold transition ${activeTab === 'options' ? 'bg-sky-500 text-slate-950 font-bold shadow-lg shadow-sky-500/20' : 'bg-slate-800 text-slate-300 hover:bg-slate-700'}`}
              onClick={() => setActiveTab('options')}
            >
              Option Chains
            </button>
            <button 
              className={`px-3 py-1.5 text-xs rounded-md font-semibold transition ${activeTab === 'futures' ? 'bg-sky-500 text-slate-950 font-bold shadow-lg shadow-sky-500/20' : 'bg-slate-800 text-slate-300 hover:bg-slate-700'}`}
              onClick={() => setActiveTab('futures')}
            >
              Futures Term Structure
            </button>
          </div>
        </div>

        {/* Options Controls: Expiration Selector & Call/Put Filter */}
        {activeTab === 'options' && (
          <div className="flex flex-wrap items-center gap-3">
            {/* Call / Put View Filter */}
            <div className="flex items-center bg-slate-900 border border-slate-800 rounded-md p-0.5">
              <button
                className={`px-2.5 py-1 text-xs rounded font-medium transition ${optionSideFilter === 'all' ? 'bg-slate-700 text-white' : 'text-slate-400 hover:text-slate-200'}`}
                onClick={() => setOptionSideFilter('all')}
              >
                All (Calls & Puts)
              </button>
              <button
                className={`px-2.5 py-1 text-xs rounded font-semibold transition ${optionSideFilter === 'calls' ? 'bg-emerald-600 text-white' : 'text-emerald-400 hover:text-emerald-300'}`}
                onClick={() => setOptionSideFilter('calls')}
              >
                Calls Only 🟢
              </button>
              <button
                className={`px-2.5 py-1 text-xs rounded font-semibold transition ${optionSideFilter === 'puts' ? 'bg-rose-600 text-white' : 'text-rose-400 hover:text-rose-300'}`}
                onClick={() => setOptionSideFilter('puts')}
              >
                Puts Only 🔴
              </button>
            </div>

            {/* Individual Option Expiration Selection Dropdown */}
            <div className="flex items-center gap-2">
              <label className="text-xs font-semibold text-slate-400">Expiration Chain:</label>
              <select 
                className="bg-slate-900 border border-slate-700 rounded-md px-3 py-1.5 text-xs text-sky-400 font-semibold focus:outline-none focus:border-sky-500" 
                value={expiryDays} 
                onChange={(e) => setExpiryDays(Number(e.target.value))}
              >
                {EXPIRATIONS_LIST.map((exp) => (
                  <option key={exp.days} value={exp.days}>
                    {exp.label}
                  </option>
                ))}
              </select>
            </div>
          </div>
        )}
      </div>

      {/* OPTIONS CHAIN TABLE */}
      {activeTab === 'options' ? (
        <div className={styles.tableWrapper + " mt-4"}>
          <table className={styles.chainTable}>
            <thead>
              <tr>
                {(optionSideFilter === 'all' || optionSideFilter === 'calls') && (
                  <th colSpan={6} className={styles.callHeader + " bg-emerald-950/40 text-emerald-400"}>
                    CALL OPTIONS
                  </th>
                )}
                <th className={styles.strikeHeader + " bg-slate-900"}>STRIKE</th>
                {(optionSideFilter === 'all' || optionSideFilter === 'puts') && (
                  <th colSpan={6} className={styles.putHeader + " bg-rose-950/40 text-rose-400"}>
                    PUT OPTIONS
                  </th>
                )}
              </tr>
              <tr className={styles.subHeader}>
                {(optionSideFilter === 'all' || optionSideFilter === 'calls') && (
                  <>
                    <th>Delta</th>
                    <th>OI</th>
                    <th>Vol</th>
                    <th>Bid</th>
                    <th>Ask</th>
                    <th>Action</th>
                  </>
                )}
                <th className="bg-slate-950">Strike</th>
                {(optionSideFilter === 'all' || optionSideFilter === 'puts') && (
                  <>
                    <th>Action</th>
                    <th>Bid</th>
                    <th>Ask</th>
                    <th>Vol</th>
                    <th>OI</th>
                    <th>Delta</th>
                  </>
                )}
              </tr>
            </thead>
            <tbody>
              {optionChainData.map((row) => {
                const isITMCall = spotPrice > row.strike;
                const isITMPut = spotPrice < row.strike;

                return (
                  <tr key={row.strike} className={`${styles.row} ${row.isATM ? 'bg-sky-500/10 border-y border-sky-500/30' : ''}`}>
                    {/* CALLS SIDE */}
                    {(optionSideFilter === 'all' || optionSideFilter === 'calls') && (
                      <>
                        <td className={styles.cell} style={{ color: '#38bdf8' }}>{row.callDelta}</td>
                        <td className={styles.cell}>{row.callOI.toLocaleString()}</td>
                        <td className={styles.cell}>{row.callVolume.toLocaleString()}</td>
                        <td className={`${styles.cell} ${styles.bid} ${isITMCall ? 'bg-emerald-950/20' : ''}`}>
                          ${row.callBid.toFixed(2)}
                        </td>
                        <td className={`${styles.cell} ${styles.ask} ${isITMCall ? 'bg-emerald-950/20' : ''}`}>
                          ${row.callAsk.toFixed(2)}
                        </td>
                        <td className={styles.cell}>
                          <div className="flex gap-1 justify-center">
                            <button
                              className="px-1.5 py-0.5 text-[10px] bg-emerald-600/70 hover:bg-emerald-500 text-white rounded font-bold"
                              title={`Buy Call @ $${row.callAsk}`}
                              onClick={() => handleAddOptionLeg('BUY', 'CALL', row.strike, row.callAsk)}
                            >
                              +Buy
                            </button>
                            <button
                              className="px-1.5 py-0.5 text-[10px] bg-slate-700 hover:bg-slate-600 text-slate-200 rounded font-bold"
                              title={`Sell Call @ $${row.callBid}`}
                              onClick={() => handleAddOptionLeg('SELL', 'CALL', row.strike, row.callBid)}
                            >
                              -Sell
                            </button>
                          </div>
                        </td>
                      </>
                    )}

                    {/* CENTER STRIKE COLUMN */}
                    <td className={`${styles.strikeCell} ${row.isATM ? 'text-sky-400 font-extrabold bg-sky-950/40' : 'bg-slate-900/60 font-bold'}`}>
                      ${row.strike} {row.isATM ? ' (ATM)' : ''}
                    </td>

                    {/* PUTS SIDE */}
                    {(optionSideFilter === 'all' || optionSideFilter === 'puts') && (
                      <>
                        <td className={styles.cell}>
                          <div className="flex gap-1 justify-center">
                            <button
                              className="px-1.5 py-0.5 text-[10px] bg-rose-600/70 hover:bg-rose-500 text-white rounded font-bold"
                              title={`Buy Put @ $${row.putAsk}`}
                              onClick={() => handleAddOptionLeg('BUY', 'PUT', row.strike, row.putAsk)}
                            >
                              +Buy
                            </button>
                            <button
                              className="px-1.5 py-0.5 text-[10px] bg-slate-700 hover:bg-slate-600 text-slate-200 rounded font-bold"
                              title={`Sell Put @ $${row.putBid}`}
                              onClick={() => handleAddOptionLeg('SELL', 'PUT', row.strike, row.putBid)}
                            >
                              -Sell
                            </button>
                          </div>
                        </td>
                        <td className={`${styles.cell} ${styles.bid} ${isITMPut ? 'bg-rose-950/20' : ''}`}>
                          ${row.putBid.toFixed(2)}
                        </td>
                        <td className={`${styles.cell} ${styles.ask} ${isITMPut ? 'bg-rose-950/20' : ''}`}>
                          ${row.putAsk.toFixed(2)}
                        </td>
                        <td className={styles.cell}>{row.putVolume.toLocaleString()}</td>
                        <td className={styles.cell}>{row.putOI.toLocaleString()}</td>
                        <td className={styles.cell} style={{ color: '#f43f5e' }}>{row.putDelta}</td>
                      </>
                    )}
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      ) : (
        /* FUTURES TERM STRUCTURE CHAIN TABLE */
        <div className={styles.tableWrapper + " mt-4"}>
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
                        +Buy
                      </button>
                      <button 
                        className="px-2 py-0.5 text-xs bg-rose-600/60 hover:bg-rose-500 text-white rounded font-bold"
                        onClick={() => handleAddFuturesLeg('SELL', row.futuresPrice)}
                      >
                        -Sell
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
