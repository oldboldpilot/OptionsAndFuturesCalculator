'use client';

import { useCallback, useEffect, useRef, useState } from 'react';
import { formatAmount, useCurrency } from '../lib/currency';
import { useCalculatorStore, type ChainStrike } from '../store/useCalculatorStore';
import { chainFreshness } from '../lib/chainFreshness';

type SideFilter = 'both' | 'calls' | 'puts';

/**
 * A price column. GROUPED: `toFixed` left an SPX strike reading `5900.00` and
 * a high-priced underlying's premium with no separator at all. `formatAmount`
 * carries no symbol because the column header already says what these are.
 */
const n = (v: number, dp = 2) =>
  Number.isFinite(v) && v !== 0 ? formatAmount(v, dp) : '—';
/** Volume and open interest -- counts, not money, but still grouped. */
const int = (v: number) => (Number.isFinite(v) && v !== 0 ? formatAmount(v, 0) : '—');
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
  // Regroup the chain when the display locale changes.
  useCurrency();
  const {
    chainStrikes, chainExpirations, selectedExpiration, chainStatus, chainError, chainFetchedAt,
    setSelectedExpiration, loadChain, setTicket, ticket, assetClass, symbol,
  } = useCalculatorStore();

  const [side, setSide] = useState<SideFilter>('both');
  // Open interest and volume are reference columns, not decision columns, and
  // they took the ladder from 11 columns to 15 -- 823px in a 378px panel, which
  // is why the STRIKE column was off the right edge. Off until asked for.
  const [showDepth, setShowDepth] = useState(false);
  const bodyRef = useRef<HTMLDivElement | null>(null);
  const atmRef = useRef<HTMLTableRowElement | null>(null);
  const atmStrikeRef = useRef<HTMLTableCellElement | null>(null);
  // Roving tab stop for the B/S buttons: one tab stop for the whole ladder, and
  // arrow keys inside it, instead of 604 consecutive Tab presses.
  const [cursor, setCursor] = useState<{ row: number; col: number } | null>(null);

  /**
   * The clock the freshness chip is measured against.
   *
   * It is state, not `Date.now()` read during render, because age computed at
   * render is frozen at render. The chain cache TTL is 15 minutes, so a chain
   * fetched fresh would otherwise render LIVE and — with nothing else on the
   * page changing — still say LIVE a quarter of an hour later. The ticker is
   * what lets the chip go stale on its own.
   *
   * 15s is chosen against the 60s live window: the label can never be more
   * than one tick behind the truth, and the interval costs one state write a
   * minute-and-a-bit against a panel that already re-renders on every quote.
   */
  const [nowMs, setNowMs] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setNowMs(Date.now()), 15_000);
    return () => clearInterval(id);
  }, []);

  const {
    ageSeconds: chainAgeSeconds, state: chainState, asOfTime, lastQuoteLabel,
  } = chainFreshness(chainFetchedAt, nowMs);

  /**
   * The chip is two words; the tooltip is where the actual number goes. A null
   * age is stated as unknown rather than omitted — "this clock cannot measure
   * it" and "it is current" must not look the same on hover.
   */
  const freshnessTitle = !chainFetchedAt
    ? ''
    : chainAgeSeconds === null
      ? `Backend print: ${chainFetchedAt} (age unknown — this device's clock disagrees with the server)`
        // Clamped at 0. A print fetched moments ago routinely computes a small
        // NEGATIVE age -- the timestamp is the server's and the clock is the
        // viewer's, and sub-second skew between them is normal. Rendering that
        // as "-1s ago" reads as a bug in the very tooltip whose job is to make
        // the freshness claim checkable. Anything beyond the skew tolerance is
        // still not shown as a number at all: chainFreshness returns a null age
        // there, which takes the branch above.
      : `Backend print: ${chainFetchedAt} (${Math.max(0, Math.round(chainAgeSeconds))}s ago)`;

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
  const centre = useCallback(() => {
    const body = bodyRef.current;
    const atm = atmRef.current;
    if (!body || !atm || body.clientHeight === 0) return false;
    body.scrollTop = Math.max(0, atm.offsetTop - body.clientHeight / 2 + atm.offsetHeight / 2);
    // The ladder is wider than its panel, so centre it across as well: the
    // strike you are looking at is the middle of the screen, calls to its left
    // and puts to its right, rather than the far-left edge of the calls.
    const strike = atmStrikeRef.current;
    if (strike) {
      body.scrollLeft = Math.max(
        0,
        strike.offsetLeft - body.clientWidth / 2 + strike.offsetWidth / 2,
      );
    }
    return true;
  }, []);

  useEffect(() => {
    if (chainStatus !== 'ready') return;
    centre();
  }, [chainStatus, selectedExpiration, chainStrikes.length, side, showDepth, centre]);

  // On a phone this panel starts hidden (another tab is showing), so it has no
  // size when the chain arrives and the centring above is a no-op. Centre it
  // again the first time it gets one.
  useEffect(() => {
    const body = bodyRef.current;
    if (!body || typeof ResizeObserver === 'undefined') return;
    let done = false;
    const ro = new ResizeObserver(() => {
      if (!done && body.clientHeight > 0 && centre()) done = true;
    });
    ro.observe(body);
    return () => ro.disconnect();
  }, [centre, chainStatus, selectedExpiration, chainStrikes.length]);

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
   *
   * No `title` on the cell. It read "Select the 779.00 call" on every one of
   * ~600 cells, which told a screen reader and a hover the same nothing 600
   * times; the B and S buttons beside each price carry the accessible names.
   */
  const pick = (row: ChainStrike, type: 'CALL' | 'PUT') => ({
    onClick: () => load(row, type, ticket.action),
    style: { cursor: 'pointer' as const },
  });

  const showCalls = side !== 'puts';
  const showPuts = side !== 'calls';
  const sideCols = showDepth ? 7 : 5;

  const atmIndex = Math.max(0, chainStrikes.findIndex((r) => r.isAtm));
  const cur = cursor ?? { row: atmIndex, col: 0 };
  const lastNavCol = showCalls && showPuts ? 3 : 1;

  /** Arrow keys move between the B/S buttons; the ladder is ONE tab stop. */
  function onGridKey(e: React.KeyboardEvent<HTMLTableSectionElement>) {
    const el = e.target as HTMLElement;
    if (el.dataset.navRow === undefined) return;
    let row = Number(el.dataset.navRow);
    let col = Number(el.dataset.navCol);
    switch (e.key) {
      case 'ArrowDown': row += 1; break;
      case 'ArrowUp': row -= 1; break;
      case 'ArrowRight': col += 1; break;
      case 'ArrowLeft': col -= 1; break;
      case 'Home': col = 0; break;
      case 'End': col = lastNavCol; break;
      case 'PageDown': row += 10; break;
      case 'PageUp': row -= 10; break;
      default: return;
    }
    row = Math.min(chainStrikes.length - 1, Math.max(0, row));
    col = Math.min(lastNavCol, Math.max(0, col));
    e.preventDefault();
    setCursor({ row, col });
    bodyRef.current
      ?.querySelector<HTMLElement>(`[data-nav-row="${row}"][data-nav-col="${col}"]`)
      ?.focus();
  }

  const actions = (row: ChainStrike, rowIndex: number, type: 'CALL' | 'PUT') => {
    const expiry = selectedExpiration ? ` ${selectedExpiration}` : '';
    // Logical column among the buttons that exist under the current side filter.
    const base = showCalls && showPuts && type === 'PUT' ? 2 : 0;
    const btn = (act: 'BUY' | 'SELL', navCol: number) => (
      <button
        className={act === 'BUY' ? 'btn btn-buy' : 'btn btn-sell'}
        // 24px square at least: the buttons were 19x19, under the WCAG 2.2
        // minimum target, and there are four per strike.
        style={{ padding: '0 0.3125rem', minWidth: '1.5rem', minHeight: '1.5rem' }}
        onClick={() => load(row, type, act)}
        title={`${act === 'BUY' ? 'Buy' : 'Sell'} ${type}`}
        aria-label={`${act === 'BUY' ? 'Buy' : 'Sell'} ${n(row.strike)} ${type.toLowerCase()}${expiry}`}
        data-nav-row={rowIndex}
        data-nav-col={navCol}
        tabIndex={cur.row === rowIndex && cur.col === navCol ? 0 : -1}
        onFocus={() => setCursor({ row: rowIndex, col: navCol })}
      >
        {act === 'BUY' ? 'B' : 'S'}
      </button>
    );
    return (
      <td style={{ whiteSpace: 'nowrap', textAlign: 'center' }}>
        {btn('BUY', base)}{' '}
        {btn('SELL', base + 1)}
      </td>
    );
  };

  const isFutures = assetClass === 'FUTURES';

  return (
    <div className="panel" style={{ flex: 1, minWidth: 0 }}>
      {/* Three short rows instead of one that wraps unpredictably: the title and
          the expiry (the two things that identify WHICH chain this is), then
          what state its data is in, then how it is filtered. At a 380px column
          the single wrapping row came out four lines tall, with the date
          selector alone on the last. */}
      <div className="panel-head" style={{ flexDirection: 'column', alignItems: 'stretch', flexWrap: 'nowrap', gap: '0.375rem' }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '0.5rem' }}>
          <h2 className="panel-title">Option Chain</h2>
          {/* flexShrink stays on the SELECT itself: it is the one control that
              must not be squeezed, because a truncated date leaves the trader
              unable to tell which contract they are pricing. */}
          <select
            className="select"
            style={{ width: 'auto', flexShrink: 0, maxWidth: '100%' }}
            value={selectedExpiration}
            onChange={(e) => {
              setSelectedExpiration(e.target.value);
              // Clear the strike too: it belonged to the previous expiry and
              // its price is not the price of the new contract.
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

        {chainStatus === 'ready' && chainStrikes.length > 0 && (
          <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem', flexWrap: 'wrap' }}>
            {chainState === 'live' ? (
              <span className="chip chip-live" title={freshnessTitle}>
                <i className="dot" />LIVE
              </span>
            ) : chainState === 'closed' ? (
              /* Not LIVE, however fresh the print: nothing is trading. The
                 label says when the last quote was instead of implying that
                 the screen is a feed. */
              <span className="chip chip-stale" title={freshnessTitle}>
                MARKET CLOSED · last quote {lastQuoteLabel}
              </span>
            ) : (
              <span className="chip chip-stale" title={freshnessTitle}>
                DELAYED {asOfTime ? `· ${asOfTime}` : ''}
              </span>
            )}
            <span className="chip">{chainStrikes.length} strikes</span>
          </div>
        )}

        <div style={{ display: 'flex', alignItems: 'center', gap: '0.375rem', flexWrap: 'wrap' }}>
          <div className="segment" role="group" aria-label="Chain side">
            <button className="segment-item" data-active={side === 'both'} aria-pressed={side === 'both'} onClick={() => setSide('both')}>Both</button>
            <button className="segment-item" data-active={side === 'calls'} aria-pressed={side === 'calls'} onClick={() => setSide('calls')}>Calls</button>
            <button className="segment-item" data-active={side === 'puts'} aria-pressed={side === 'puts'} onClick={() => setSide('puts')}>Puts</button>
          </div>
          <div className="segment" role="group" aria-label="Chain columns">
            <button
              className="segment-item"
              data-active={showDepth}
              aria-pressed={showDepth}
              onClick={() => setShowDepth((v) => !v)}
            >
              OI / Vol
            </button>
          </div>
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
        ) : chainStrikes.length === 0 ? (
          /* "Ready" with nothing in it. For a futures root that is the correct
             answer, not a fault: the data feed lists no options on it, and the
             position is priced from the forward curve instead. It used to read
             "LIVE · 0 strikes" over an empty table. */
          <div className="empty-state">
            <span className="empty-state-title">
              {isFutures ? `No option chain for ${symbol} futures` : 'No listed options'}
            </span>
            <span>
              {isFutures
                ? 'Futures are priced from the forward curve, not from an option ladder. Pick a futures strategy to build a position on a contract.'
                : `The provider lists no option contracts for ${symbol} at this expiry.`}
            </span>
          </div>
        ) : (
          <table className="grid-table" role="grid" aria-label="Option chain">
            <thead>
              <tr>
                {showCalls && (
                  <th scope="colgroup" colSpan={sideCols} style={{ color: 'var(--color-profit-text)', background: 'var(--color-call-tint)', textAlign: 'center' }}>
                    CALLS
                  </th>
                )}
                <th scope="col" className="chain-strike" style={{ textAlign: 'center' }}>STRIKE</th>
                {showPuts && (
                  <th scope="colgroup" colSpan={sideCols} style={{ color: 'var(--color-loss)', background: 'var(--color-put-tint)', textAlign: 'center' }}>
                    PUTS
                  </th>
                )}
              </tr>
              <tr>
                {showCalls && (
                  <>
                    {showDepth && <><th scope="col">OI</th><th scope="col">Vol</th></>}
                    <th scope="col">IV</th><th scope="col">Δ</th><th scope="col">Bid</th><th scope="col">Ask</th>
                    <th scope="col" style={{ textAlign: 'center' }}>+/−</th>
                  </>
                )}
                <th scope="col" className="chain-strike" />
                {showPuts && (
                  <>
                    <th scope="col" style={{ textAlign: 'center' }}>+/−</th>
                    <th scope="col">Bid</th><th scope="col">Ask</th><th scope="col">Δ</th><th scope="col">IV</th>
                    {showDepth && <><th scope="col">Vol</th><th scope="col">OI</th></>}
                  </>
                )}
              </tr>
            </thead>
            <tbody onKeyDown={onGridKey}>
              {chainStrikes.map((row, rowIndex) => (
                <tr
                  key={row.strike}
                  ref={row.isAtm ? atmRef : undefined}
                  data-atm={row.isAtm ? 'true' : undefined}
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
                      {showDepth && (
                        <>
                          <td {...pick(row, 'CALL')}>{int(row.call.openInterest)}</td>
                          <td {...pick(row, 'CALL')}>{int(row.call.volume)}</td>
                        </>
                      )}
                      <td {...pick(row, 'CALL')}>{pct(row.call.iv)}</td>
                      <td {...pick(row, 'CALL')}>{n(row.call.delta, 3)}</td>
                      <td className="profit" {...pick(row, 'CALL')}>{n(row.call.bid)}</td>
                      <td className="profit" {...pick(row, 'CALL')}>{n(row.call.ask)}</td>
                      {actions(row, rowIndex, 'CALL')}
                    </>
                  )}
                  <td
                    className="chain-strike"
                    ref={row.isAtm ? atmStrikeRef : undefined}
                    style={{
                      textAlign: 'center',
                      fontWeight: 700,
                      color: row.isAtm ? 'var(--color-accent-text)' : 'var(--color-ink-100)',
                      borderLeft: '1px solid var(--color-line)',
                      borderRight: '1px solid var(--color-line)',
                    }}
                  >
                    {n(row.strike)}
                  </td>
                  {showPuts && (
                    <>
                      {actions(row, rowIndex, 'PUT')}
                      <td className="loss" {...pick(row, 'PUT')}>{n(row.put.bid)}</td>
                      <td className="loss" {...pick(row, 'PUT')}>{n(row.put.ask)}</td>
                      <td {...pick(row, 'PUT')}>{n(row.put.delta, 3)}</td>
                      <td {...pick(row, 'PUT')}>{pct(row.put.iv)}</td>
                      {showDepth && (
                        <>
                          <td {...pick(row, 'PUT')}>{int(row.put.volume)}</td>
                          <td {...pick(row, 'PUT')}>{int(row.put.openInterest)}</td>
                        </>
                      )}
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
