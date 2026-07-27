'use client';

import { useEffect, useState } from 'react';
import { useCalculatorStore, type ChainStrike } from '../store/useCalculatorStore';

type SideFilter = 'both' | 'calls' | 'puts';

const n = (v: number, dp = 2) => (Number.isFinite(v) && v !== 0 ? v.toFixed(dp) : '—');
const int = (v: number) => (Number.isFinite(v) && v !== 0 ? v.toLocaleString() : '—');
const pct = (v: number) => (Number.isFinite(v) && v > 0 ? `${(v * 100).toFixed(1)}%` : '—');

/**
 * Live option chain.
 *
 * Every strike, price, delta, IV, volume and open-interest figure comes from
 * the backend chain service via the store. The previous build synthesised
 * strikes from a step function and filled the columns with constants (volume
 * 1200, OI 3400, IV 0.22), which with no quote produced negative strikes and
 * $NaN prices. Per spec §3.4 this renders only provider data and states
 * plainly when there is none.
 */
export function OptionChain() {
  const {
    chainStrikes, chainExpirations, selectedExpiration, chainStatus, chainError,
    setSelectedExpiration, loadChain, addLeg,
  } = useCalculatorStore();

  const [side, setSide] = useState<SideFilter>('both');

  useEffect(() => {
    if (chainStatus === 'idle') loadChain();
  }, [chainStatus, loadChain]);

  const dte = chainExpirations.find((e) => e.date === selectedExpiration)?.dte ?? 0;

  function add(row: ChainStrike, type: 'CALL' | 'PUT', action: 'BUY' | 'SELL') {
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

  const actions = (row: ChainStrike, type: 'CALL' | 'PUT') => (
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
          {chainStatus === 'ready' && (
            <>
              <span className="chip chip-live"><i className="dot" />LIVE</span>
              <span className="chip">{chainStrikes.length} strikes</span>
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
            value={selectedExpiration}
            onChange={(e) => setSelectedExpiration(e.target.value)}
            disabled={chainExpirations.length === 0}
            aria-label="Expiration date"
          >
            {chainExpirations.length === 0 ? (
              <option value="">No expirations</option>
            ) : (
              chainExpirations.map((e) => (
                <option key={e.date} value={e.date}>
                  {e.date} · {e.dte}d
                </option>
              ))
            )}
          </select>
        </div>
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1 }}>
        {chainStatus === 'loading' || chainStatus === 'idle' ? (
          <div style={{ padding: '0.5rem', display: 'flex', flexDirection: 'column', gap: '3px' }}>
            {Array.from({ length: 9 }, (_, i) => (
              <div key={i} className="skeleton" style={{ height: 15, opacity: 1 - i * 0.08 }} />
            ))}
          </div>
        ) : chainStatus === 'error' ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Chain unavailable</span>
            <span>{chainError}</span>
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
              {chainStrikes.map((row) => (
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
