'use client';

import { useMemo, useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';

type ValueMode = 'dollars' | 'percent';

/**
 * The price x date grid — P&L for each underlying price at each future date.
 *
 * `StrategyResponse.matrix` has been computed by the engine all along (every
 * leg re-priced by Black-Scholes at its own remaining maturity, for every cell)
 * and the UI rendered only the one-dimensional expiry curve, so the work was
 * discarded on arrival.
 *
 * It matters most for exactly the positions the flat curve cannot describe. A
 * calendar spread is defined by how its value moves BETWEEN today and the near
 * expiry: the short leg decays faster than the long one, which is the entire
 * trade. On the expiry curve that is a single instant. Here it is the whole
 * left-to-right sweep.
 *
 * Colour encodes P&L against the position's own risk, so the scale means the
 * same thing across strategies. Green is profit, red is loss, and intensity is
 * the fraction of maximum observed magnitude — computed from the rendered cells
 * rather than from max_profit/max_loss, because those come from the expiry
 * curve and the grid legitimately exceeds them at intermediate dates (a
 * calendar spread's peak is mid-life, not at expiry).
 */
export function PnLMatrix() {
  const {
    result, spotPrice, isLoading, error, modelLimit, gateDenied, notReady,
    matrixPriceMin, matrixPriceMax, setMatrixBounds,
  } = useCalculatorStore();
  const [mode, setMode] = useState<ValueMode>('percent');

  const grid = useMemo(() => {
    const cells = result?.matrix ?? [];
    if (cells.length === 0) return null;

    // The engine emits row-major over dates then prices. Rebuild both axes from
    // the data rather than assuming a shape — date_steps and price_steps are
    // request parameters and a caller may change them.
    const dates: string[] = [];
    const dteByDate = new Map<string, number>();
    for (const c of cells) {
      if (!dteByDate.has(c.date)) {
        dteByDate.set(c.date, c.daysToExpiration);
        dates.push(c.date);
      }
    }
    // Ascending calendar order: today on the left. ISO-8601 sorts correctly.
    dates.sort();

    const prices = [...new Set(cells.map((c) => c.price))].sort((a, b) => b - a);

    const byKey = new Map<string, (typeof cells)[number]>();
    for (const c of cells) byKey.set(`${c.price}|${c.date}`, c);

    // A full grid is unreadable at this density; sample prices to a sane count
    // while always keeping the row nearest spot, which is the one being read.
    const MAX_ROWS = 21;
    let rows = prices;
    if (prices.length > MAX_ROWS) {
      const step = Math.ceil(prices.length / MAX_ROWS);
      rows = prices.filter((_, i) => i % step === 0);
    }
    let spotRow = rows[0];
    for (const p of rows) {
      if (Math.abs(p - spotPrice) < Math.abs(spotRow - spotPrice)) spotRow = p;
    }

    let magnitude = 0;
    for (const c of cells) {
      const v = mode === 'percent' ? c.returnOnRiskPercent : c.pnl;
      magnitude = Math.max(magnitude, Math.abs(v));
    }
    if (magnitude === 0) magnitude = 1;

    // The window the engine ACTUALLY used, taken from the cells rather than
    // recomputed from spot and the default percentage. If the two ever
    // disagree the cells are right, and this is the number shown to the user
    // as the thing their bounds override.
    return {
      dates, dteByDate, rows, byKey, spotRow, magnitude,
      windowLo: prices[prices.length - 1],
      windowHi: prices[0],
    };
  }, [result, spotPrice, mode]);

  /**
   * The bound inputs are DRAFTS until committed on blur or Enter.
   *
   * Committing per keystroke would fire a priced round trip at "4", "48" and
   * "480" -- three requests for one number, of which two describe prices the
   * user never asked about. The store's staleness token would discard the
   * losers correctly, so this is not a correctness fix; it is the difference
   * between one request and one per character.
   *
   * The draft is seeded from the store and re-seeded whenever the store's
   * value changes, so the Auto button and a scenario load both show up in the
   * fields rather than leaving them stale.
   */
  const text = (v: number | null) => (v === null ? '' : String(v));
  const [loDraft, setLoDraft] = useState(() => text(matrixPriceMin));
  const [hiDraft, setHiDraft] = useState(() => text(matrixPriceMax));
  const [boundsHint, setBoundsHint] = useState<string | null>(null);

  // Re-seeded by ADJUSTING STATE DURING RENDER against the previous store
  // value, not from an effect. An effect would render once with the stale
  // draft, then again with the fresh one -- and React flags exactly this
  // (react-hooks/set-state-in-effect). Comparing to the last value seen keeps
  // the user's own keystrokes intact while still picking up Auto and a
  // scenario load, which is the whole reason the mirror exists.
  const [seeded, setSeeded] = useState<readonly [number | null, number | null]>(
    () => [matrixPriceMin, matrixPriceMax],
  );
  if (seeded[0] !== matrixPriceMin || seeded[1] !== matrixPriceMax) {
    setSeeded([matrixPriceMin, matrixPriceMax]);
    setLoDraft(text(matrixPriceMin));
    setHiDraft(text(matrixPriceMax));
    setBoundsHint(null);
  }

  function commitBounds() {
    const parse = (t: string): number | null => {
      const trimmed = t.trim();
      if (trimmed === '') return null;
      const n = Number(trimmed);
      return Number.isFinite(n) && n > 0 ? n : null;
    };
    const lo = parse(loDraft);
    const hi = parse(hiDraft);

    // Refused here rather than swapped, and rather than sent. The engine
    // refuses an inverted pair too -- that is the real gate -- but letting it
    // travel would blank the grid behind a red "Unavailable", which is the
    // wrong shape for a typo in the field the user is still looking at.
    if (lo !== null && hi !== null && hi <= lo) {
      setBoundsHint('High must be above low');
      return;
    }
    setBoundsHint(null);
    if (lo === matrixPriceMin && hi === matrixPriceMax) return;
    setMatrixBounds({ min: lo, max: hi });
  }

  const bounded = matrixPriceMin !== null || matrixPriceMax !== null;

  function tint(value: number, magnitude: number): string {
    const frac = Math.min(Math.abs(value) / magnitude, 1);
    // Floor the alpha so a near-zero cell still reads as a cell rather than a
    // hole in the grid.
    const alpha = 0.06 + frac * 0.54;
    if (value > 0) return `color-mix(in srgb, var(--color-profit) ${alpha * 100}%, transparent)`;
    if (value < 0) return `color-mix(in srgb, var(--color-loss) ${alpha * 100}%, transparent)`;
    return 'transparent';
  }

  function label(value: number): string {
    if (mode === 'percent') return `${value > 0 ? '+' : ''}${value.toFixed(0)}`;
    const abs = Math.abs(value);
    const compact = abs >= 1000 ? `${(value / 1000).toFixed(1)}k` : value.toFixed(0);
    return value > 0 ? `+${compact}` : compact;
  }

  const curveDays = result?.inputs.curveDays;
  const horizon = result?.inputs.days;
  const multiExpiry = curveDays !== undefined && horizon !== undefined && curveDays < horizon;

  return (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">P&amp;L matrix</span>
          <span
            className="chip"
            title={
              'Every leg re-priced by Black-Scholes at its own remaining maturity, ' +
              'for each price and date. Legs whose expiry has passed are carried at ' +
              'intrinsic against the price shown.'
            }
          >
            price × date
          </span>
          {multiExpiry && (
            <span
              className="chip chip-accent"
              title={`Near expiry at ${Math.round(curveDays!)} days; the position runs to ${Math.round(horizon!)} days. The columns between are where a calendar spread earns.`}
            >
              near {Math.round(curveDays!)}d
            </span>
          )}
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          {/* Price bounds. Scoped to this panel because they are scoped to
              this panel's data -- the engine keeps the payoff curve, and
              therefore max profit and breakeven, on its own untouched grid. */}
          <div
            style={{ display: 'flex', alignItems: 'center', gap: '0.25rem' }}
            title={
              'Limits the price axis of this grid. The payoff curve, max profit, ' +
              'max loss, breakeven and the probability distribution are unaffected ' +
              '\u2014 they are computed over the full range regardless.'
            }
          >
            <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
              price
            </span>
            <input
              className="input"
              style={{ width: '62px', textAlign: 'right' }}
              type="number"
              min={0}
              step="any"
              inputMode="decimal"
              aria-label="Matrix lower price bound"
              placeholder={grid ? grid.windowLo.toFixed(0) : 'low'}
              value={loDraft}
              onChange={(e) => setLoDraft(e.target.value)}
              onBlur={commitBounds}
              onKeyDown={(e) => {
                if (e.key === 'Enter') e.currentTarget.blur();
              }}
            />
            <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
              &ndash;
            </span>
            <input
              className="input"
              style={{ width: '62px', textAlign: 'right' }}
              type="number"
              min={0}
              step="any"
              inputMode="decimal"
              aria-label="Matrix upper price bound"
              placeholder={grid ? grid.windowHi.toFixed(0) : 'high'}
              value={hiDraft}
              onChange={(e) => setHiDraft(e.target.value)}
              onBlur={commitBounds}
              onKeyDown={(e) => {
                if (e.key === 'Enter') e.currentTarget.blur();
              }}
            />
            {/* Only offered once there is something to reset. A permanently
                visible Auto invites a click that does nothing. */}
            {bounded && (
              <button
                className="chip"
                style={{ cursor: 'pointer' }}
                aria-label="Reset matrix price bounds"
                onClick={() => setMatrixBounds({ min: null, max: null })}
              >
                auto
              </button>
            )}
            {boundsHint && (
              <span
                role="alert"
                style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-loss)' }}
              >
                {boundsHint}
              </span>
            )}
          </div>

          <div className="segment">
            <button
              className="segment-item"
              data-active={mode === 'dollars'}
              onClick={() => setMode('dollars')}
            >
              $
            </button>
            <button
              className="segment-item"
              data-active={mode === 'percent'}
              onClick={() => setMode('percent')}
            >
              % risk
            </button>
          </div>
        </div>
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1, overflow: 'auto' }}>
        {/* A modelling limit, not a fault -- plain empty state, never the
            loss-red error branch. See PayoffLadder for the full reasoning;
            the engine's own sentence is shown verbatim so the client never
            paraphrases a refusal it did not make. */}
        {modelLimit ? (
          <div className="empty-state">
            <span className="empty-state-title">Not modelled</span>
            <span>{modelLimit}</span>
          </div>
        ) : error ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Unavailable</span>
            <span>{error}</span>
          </div>
        ) : isLoading ? (
          <div style={{ padding: '0.5rem', display: 'flex', flexDirection: 'column', gap: '3px' }}>
            {Array.from({ length: 10 }, (_, i) => (
              <div key={i} className="skeleton" style={{ height: 15 }} />
            ))}
          </div>
        ) : grid ? (
          <table className="grid-table" style={{ fontVariantNumeric: 'tabular-nums' }}>
            <thead>
              <tr>
                <th style={{ textAlign: 'left', position: 'sticky', left: 0, background: 'var(--color-base-800)' }}>
                  Price
                </th>
                {grid.dates.map((d) => (
                  <th key={d} title={`${d} · ${grid.dteByDate.get(d)} days to expiry`}>
                    {/* MM-DD: the year is constant across the axis and the
                        columns are narrow. Full date is in the tooltip. */}
                    {d.slice(5)}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {grid.rows.map((price, i) => {
                const isSpot = price === grid.spotRow;
                return (
                  <tr
                    key={price}
                    className="animate-fade"
                    style={{ animationDelay: `${Math.min(i * 0.012, 0.3)}s` }}
                  >
                    <td
                      style={{
                        textAlign: 'left',
                        position: 'sticky',
                        left: 0,
                        background: isSpot ? 'var(--color-atm-tint)' : 'var(--color-base-800)',
                        color: isSpot ? 'var(--color-accent)' : 'var(--color-ink-200)',
                        fontWeight: isSpot ? 600 : 400,
                      }}
                    >
                      {price.toFixed(2)}
                    </td>
                    {grid.dates.map((d) => {
                      const cell = grid.byKey.get(`${price}|${d}`);
                      if (!cell) return <td key={d} />;
                      const value = mode === 'percent' ? cell.returnOnRiskPercent : cell.pnl;
                      return (
                        <td
                          key={d}
                          className={value > 0 ? 'profit' : value < 0 ? 'loss' : 'flat'}
                          style={{ background: tint(value, grid.magnitude) }}
                          title={`${d} · ${price.toFixed(2)} · ${cell.pnl >= 0 ? '+' : ''}${cell.pnl.toFixed(2)} (${cell.returnOnRiskPercent >= 0 ? '+' : ''}${cell.returnOnRiskPercent.toFixed(1)}% of risk)`}
                        >
                          {label(value)}
                        </td>
                      );
                    })}
                  </tr>
                );
              })}
            </tbody>
          </table>
        ) : gateDenied ? (
          <div className="empty-state">
            <span className="empty-state-title">Needs Pro</span>
            <span>{gateDenied}</span>
          </div>
        ) : notReady ? (
          /* Replaces the empty state when set, sitting below the panel's
             rendered-output branch so it cannot replace a drawn chart. */
          /* A precondition, in the NEUTRAL style -- deliberately not the
             --error branch above. "You have not picked a strike yet" is the
             next thing to do, not a failure, and rendering it as "Unavailable"
             in loss red tells a trader the calculator is broken when nothing
             is wrong. The title names the action for the same reason. */
          <div className="empty-state">
            <span className="empty-state-title">Not priced yet</span>
            <span>{notReady}</span>
          </div>
        ) : (
          <div className="empty-state">
            <span className="empty-state-title">No grid yet</span>
            <span>Add priced legs to compute P&amp;L across price and date.</span>
          </div>
        )}
      </div>
    </div>
  );
}

export default PnLMatrix;
