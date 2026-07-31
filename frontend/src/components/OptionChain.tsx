'use client';

import { useEffect, useRef, useState } from 'react';
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
    setSelectedExpiration, loadChain, setTicket, ticket,
  } = useCalculatorStore();

  const [side, setSide] = useState<SideFilter>('both');
  const bodyRef = useRef<HTMLDivElement | null>(null);
  const atmRef = useRef<HTMLTableRowElement | null>(null);

  useEffect(() => {
    if (chainStatus === 'idle') loadChain();
  }, [chainStatus, loadChain]);

  /**
   * Open the chain at the money.
   *
   * The ladder runs 116 strikes deep and the at-the-money row sits near the
   * middle of it — measured at row 63, 1715px down, while the scroll position
   * stayed at 0. So the chain opened on the lowest strikes: deep in-the-money
   * calls and puts worth nothing, which is the part of the ladder nobody
   * trades. Every strike was present and none of the useful ones were on
   * screen.
   *
   * Centring rather than scrolling-into-view keeps a band of strikes either
   * side visible, which is how the ladder is actually read.
   */
  useEffect(() => {
    if (chainStatus !== 'ready') return;
    const body = bodyRef.current;
    const atm = atmRef.current;
    if (!body || !atm) return;
    body.scrollTop = Math.max(0, atm.offsetTop - body.clientHeight / 2 + atm.offsetHeight / 2);
  }, [chainStatus, selectedExpiration, chainStrikes.length, side]);

  /**
   * Load a quote into the ticket. Deliberately not an append.
   *
   * Clicking used to add the leg outright, fixing strike, premium, quantity and
   * IV at whatever the row held — so a row with no quote contributed a free
   * option, and none of the four could be adjusted afterwards. Landing in the
   * ticket keeps the click as fast while leaving every field editable.
   */
  function load(row: ChainStrike, type: 'CALL' | 'PUT', action: 'BUY' | 'SELL') {
    const q = type === 'CALL' ? row.call : row.put;
    // Buying lifts the ask, selling hits the bid — the executable price, not a midpoint.
    const px = action === 'BUY' ? q.ask : q.bid;
    setTicket({
      action,
      optionType: type,
      expiration: selectedExpiration,
      strike: row.strike,
      premium: px > 0 ? px : null,
      impliedVolatility: q.iv > 0 ? q.iv : null,
    });
  }

  /**
   * Clicking a quote selects it. Previously only the 12px B/S buttons did
   * anything, so the obvious gesture — click the price you want — did nothing
   * at all, and the ladder looked interactive without being it. The buttons
   * still set the direction explicitly; clicking a cell keeps whatever
   * direction the ticket already carries.
   */
  const pick = (row: ChainStrike, type: 'CALL' | 'PUT') => ({
    onClick: () => load(row, type, ticket.action),
    style: { cursor: 'pointer' as const },
    title: `Select the ${row.strike.toFixed(2)} ${type.toLowerCase()}`,
  });

  const showCalls = side !== 'puts';
  const showPuts = side !== 'calls';

  const actions = (row: ChainStrike, type: 'CALL' | 'PUT') => (
    <td style={{ whiteSpace: 'nowrap', textAlign: 'center' }}>
      <button className="btn btn-buy" style={{ padding: '0 0.3125rem' }} onClick={() => load(row, type, 'BUY')} title={`Buy ${type}`}>B</button>{' '}
      <button className="btn btn-sell" style={{ padding: '0 0.3125rem' }} onClick={() => load(row, type, 'SELL')} title={`Sell ${type}`}>S</button>
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
            onChange={(e) => {
              setSelectedExpiration(e.target.value);
              // Clear the strike too: it belonged to the previous expiry and
              // its price is not this contract's price.
              setTicket({ expiration: e.target.value, strike: null, premium: null, impliedVolatility: null });
            }}
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

      <div className="panel-body panel-body--flush" style={{ flex: 1, overflowY: 'auto' }} ref={bodyRef}>
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
                <tr
                  key={row.strike}
                  ref={row.isAtm ? atmRef : undefined}
                  style={
                    row.isAtm
                      ? {
                          background: 'var(--color-atm-tint)',
                          // The spot line: the money is a boundary in the
                          // ladder, so draw it as one.
                          boxShadow: 'inset 0 1px 0 var(--color-accent), inset 0 -1px 0 var(--color-accent)',
                        }
                      : undefined
                  }
                >
                  {showCalls && (
                    <>
                      <td {...pick(row, 'CALL')}>{int(row.call.openInterest)}</td>
                      <td {...pick(row, 'CALL')}>{int(row.call.volume)}</td>
                      <td {...pick(row, 'CALL')}>{pct(row.call.iv)}</td>
                      <td {...pick(row, 'CALL')}>{n(row.call.delta, 3)}</td>
                      <td className="profit" {...pick(row, 'CALL')}>{n(row.call.bid)}</td>
                      <td className="profit" {...pick(row, 'CALL')}>{n(row.call.ask)}</td>
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
                      <td className="loss" {...pick(row, 'PUT')}>{n(row.put.bid)}</td>
                      <td className="loss" {...pick(row, 'PUT')}>{n(row.put.ask)}</td>
                      <td {...pick(row, 'PUT')}>{n(row.put.delta, 3)}</td>
                      <td {...pick(row, 'PUT')}>{pct(row.put.iv)}</td>
                      <td {...pick(row, 'PUT')}>{int(row.put.volume)}</td>
                      <td {...pick(row, 'PUT')}>{int(row.put.openInterest)}</td>
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
