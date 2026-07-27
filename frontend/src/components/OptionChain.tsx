'use client';

import { useEffect, useMemo, useState } from 'react';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import { ChainRequest } from '../grpc/calculator_pb';
import { useCalculatorStore } from '../store/useCalculatorStore';

interface SideQuote {
  bid: number;
  ask: number;
  delta: number;
  iv: number;
  volume: number;
  openInterest: number;
}

interface Strike {
  strike: number;
  isAtm: boolean;
  call: SideQuote;
  put: SideQuote;
}

interface Expiration {
  date: string;
  dte: number;
}

type SideFilter = 'both' | 'calls' | 'puts';

const n = (v: number, dp = 2) => (Number.isFinite(v) && v !== 0 ? v.toFixed(dp) : '—');
const int = (v: number) => (Number.isFinite(v) && v !== 0 ? v.toLocaleString() : '—');
const pct = (v: number) => (Number.isFinite(v) && v > 0 ? `${(v * 100).toFixed(1)}%` : '—');

/**
 * Live option chain.
 *
 * Every strike, price, delta, IV, volume and open-interest figure comes from
 * the backend chain service. The previous build synthesised strikes from a
 * step function and filled the columns with constants (volume 1200, OI 3400,
 * IV 0.22), which with no quote produced negative strikes and $NaN prices.
 * Per spec §3.4 this renders only provider data and states plainly when there
 * is none.
 */
export function OptionChain() {
  const { symbol, assetClass, addLeg } = useCalculatorStore();

  const [strikes, setStrikes] = useState<Strike[]>([]);
  const [expirations, setExpirations] = useState<Expiration[]>([]);
  const [selectedExp, setSelectedExp] = useState<string>('');
  const [side, setSide] = useState<SideFilter>('both');
  const [status, setStatus] = useState<'loading' | 'ready' | 'error'>('loading');
  const [message, setMessage] = useState<string>('');

  useEffect(() => {
    let cancelled = false;
    setStatus('loading');

    const backendUrl =
      process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new OptionsCalculatorClient(backendUrl);
    const req = new ChainRequest();
    req.setSymbol(symbol);
    req.setAssetClass(assetClass);
    if (selectedExp) req.setExpirationDate(selectedExp);

    client.getMarketChain(req, {}, (err, res) => {
      if (cancelled) return;

      if (err || !res) {
        setStrikes([]);
        setExpirations([]);
        setStatus('error');
        setMessage(err?.message || 'Chain service unreachable');
        return;
      }

      setExpirations(
        res.getAvailableExpirationsList().map((e: any) => ({
          date: e.getDateStr(),
          dte: e.getDaysToExpiry(),
        })),
      );

      const rows: Strike[] = res.getOptionStrikesList().map((s: any) => ({
        strike: s.getStrike(),
        isAtm: s.getIsAtm(),
        call: {
          bid: s.getCallBid(), ask: s.getCallAsk(), delta: s.getCallDelta(),
          iv: s.getCallIv(), volume: s.getCallVolume(), openInterest: s.getCallOpenInterest(),
        },
        put: {
          bid: s.getPutBid(), ask: s.getPutAsk(), delta: s.getPutDelta(),
          iv: s.getPutIv(), volume: s.getPutVolume(), openInterest: s.getPutOpenInterest(),
        },
      }));

      setStrikes(rows);
      if (!selectedExp && res.getSelectedExpirationDate()) {
        setSelectedExp(res.getSelectedExpirationDate());
      }
      if (rows.length === 0) {
        setStatus('error');
        setMessage(`No listed contracts returned for ${symbol}`);
      } else {
        setStatus('ready');
      }
    });

    return () => {
      cancelled = true;
    };
  }, [symbol, assetClass, selectedExp]);

  const dte = useMemo(
    () => expirations.find((e) => e.date === selectedExp)?.dte ?? 0,
    [expirations, selectedExp],
  );

  function add(row: Strike, type: 'CALL' | 'PUT', action: 'BUY' | 'SELL') {
    const q = type === 'CALL' ? row.call : row.put;
    // Buying lifts the ask, selling hits the bid — the executable price, not a midpoint.
    addLeg({
      instrument_type: 'INSTRUMENT_EQUITY_OPTION',
      action,
      option_type: type,
      strike_price: row.strike,
      premium: action === 'BUY' ? q.ask : q.bid,
      quantity: 1,
      expiration_days: dte,
      implied_volatility: q.iv,
    });
  }

  const showCalls = side !== 'puts';
  const showPuts = side !== 'calls';

  const actions = (row: Strike, type: 'CALL' | 'PUT') => (
    <td style={{ whiteSpace: 'nowrap', textAlign: 'center' }}>
      <button className="btn btn-buy" style={{ padding: '0 0.3125rem' }} onClick={() => add(row, type, 'BUY')} title={`Buy ${type}`}>B</button>{' '}
      <button className="btn btn-sell" style={{ padding: '0 0.3125rem' }} onClick={() => add(row, type, 'SELL')} title={`Sell ${type}`}>S</button>
    </td>
  );

  return (
    <div className="panel" style={{ flex: 1, minWidth: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">Option Chain</span>
          {status === 'ready' && (
            <>
              <span className="chip chip-live"><i className="dot" />LIVE</span>
              <span className="chip">{strikes.length} strikes</span>
            </>
          )}
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '0.375rem' }}>
          <div className="segment">
            <button className="segment-item" data-active={side === 'both'} onClick={() => setSide('both')}>Both</button>
            <button className="segment-item" data-active={side === 'calls'} onClick={() => setSide('calls')}>Calls</button>
            <button className="segment-item" data-active={side === 'puts'} onClick={() => setSide('puts')}>Puts</button>
          </div>
          <select
            className="select"
            style={{ width: 'auto' }}
            value={selectedExp}
            onChange={(e) => setSelectedExp(e.target.value)}
            disabled={expirations.length === 0}
            aria-label="Expiration date"
          >
            {expirations.length === 0 ? (
              <option value="">No expirations</option>
            ) : (
              expirations.map((e) => (
                <option key={e.date} value={e.date}>
                  {e.date} · {e.dte}d
                </option>
              ))
            )}
          </select>
        </div>
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1 }}>
        {status === 'loading' ? (
          <div className="empty-state">
            <span className="empty-state-title">Loading chain…</span>
          </div>
        ) : status === 'error' ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Chain unavailable</span>
            <span>{message}</span>
            <span style={{ color: 'var(--color-ink-400)' }}>
              Strikes appear only when the provider returns listed contracts.
            </span>
          </div>
        ) : (
          <table className="grid-table">
            <thead>
              <tr>
                {showCalls && (
                  <th colSpan={7} style={{ color: 'var(--color-profit)', background: 'var(--color-call-tint)', textAlign: 'center' }}>
                    CALLS
                  </th>
                )}
                <th style={{ textAlign: 'center' }}>STRIKE</th>
                {showPuts && (
                  <th colSpan={7} style={{ color: 'var(--color-loss)', background: 'var(--color-put-tint)', textAlign: 'center' }}>
                    PUTS
                  </th>
                )}
              </tr>
              <tr>
                {showCalls && (
                  <>
                    <th>OI</th><th>Vol</th><th>IV</th><th>Δ</th><th>Bid</th><th>Ask</th>
                    <th style={{ textAlign: 'center' }}>+/−</th>
                  </>
                )}
                <th />
                {showPuts && (
                  <>
                    <th style={{ textAlign: 'center' }}>+/−</th>
                    <th>Bid</th><th>Ask</th><th>Δ</th><th>IV</th><th>Vol</th><th>OI</th>
                  </>
                )}
              </tr>
            </thead>
            <tbody>
              {strikes.map((row) => (
                <tr key={row.strike} style={row.isAtm ? { background: 'var(--color-atm-tint)' } : undefined}>
                  {showCalls && (
                    <>
                      <td>{int(row.call.openInterest)}</td>
                      <td>{int(row.call.volume)}</td>
                      <td>{pct(row.call.iv)}</td>
                      <td>{n(row.call.delta, 3)}</td>
                      <td className="profit">{n(row.call.bid)}</td>
                      <td className="profit">{n(row.call.ask)}</td>
                      {actions(row, 'CALL')}
                    </>
                  )}
                  <td
                    style={{
                      textAlign: 'center',
                      fontWeight: 700,
                      color: row.isAtm ? 'var(--color-accent)' : 'var(--color-ink-100)',
                      borderLeft: '1px solid var(--color-line)',
                      borderRight: '1px solid var(--color-line)',
                    }}
                  >
                    {row.strike.toFixed(2)}
                  </td>
                  {showPuts && (
                    <>
                      {actions(row, 'PUT')}
                      <td className="loss">{n(row.put.bid)}</td>
                      <td className="loss">{n(row.put.ask)}</td>
                      <td>{n(row.put.delta, 3)}</td>
                      <td>{pct(row.put.iv)}</td>
                      <td>{int(row.put.volume)}</td>
                      <td>{int(row.put.openInterest)}</td>
                    </>
                  )}
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

export default OptionChain;
