'use client';

import { useCalculatorStore } from '../store/useCalculatorStore';

const n2 = (v: number) => v.toFixed(2);

/**
 * Futures forward curve — contango or backwardation, by tenor.
 *
 * Every figure here is DERIVED and says so. Forward price is cost-of-carry,
 * F = S·exp(r·T), from a measured spot and the measured Treasury rate; the
 * engine marks each contract MODELLED and this panel repeats it rather than
 * letting a derived level pass for a quote. No vendor wired into this engine
 * publishes a futures curve — spec §2 accepts that the term structure is
 * modelled — and the honest response is to label it, not to hide it.
 *
 * Bid, ask, volume and open interest are absent on purpose. They are
 * order-book facts, no formula produces them, and a plausible-looking number
 * in those columns would be exactly the fabrication spec §3.4 forbids. They
 * are simply not shown.
 *
 * The shape is the point. Basis widening with tenor is contango, the normal
 * state under positive carry; a curve that inverts is telling you something
 * about the front month that the level alone does not.
 */
export function TermStructure() {
  const { futuresCurve, chainStatus, chainError, symbol, spotPrice } = useCalculatorStore();

  if (futuresCurve.length === 0 && chainStatus !== 'error') return null;

  const shape = (() => {
    if (futuresCurve.length < 2) return null;
    const first = futuresCurve[0];
    const last = futuresCurve[futuresCurve.length - 1];
    return last.basis > first.basis ? 'Contango' : 'Backwardation';
  })();

  return (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">Term structure</span>
          {/* The same marker the engine sets. A derived curve must never be
              mistaken for a quoted one. */}
          <span
            className="chip chip-modelled"
            title={
              'Derived, not quoted. Forward = spot × exp(rate × years), from the live ' +
              'spot and the measured Treasury rate. No provider here publishes a futures curve.'
            }
          >
            MODELLED
          </span>
          {shape && <span className="chip">{shape}</span>}
        </div>
        {spotPrice > 0 && (
          <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
            {symbol} spot {n2(spotPrice)}
          </span>
        )}
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1, overflowY: 'auto' }}>
        {futuresCurve.length === 0 ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">No curve</span>
            <span>{chainError}</span>
          </div>
        ) : (
          <table className="grid-table">
            <thead>
              <tr>
                <th style={{ textAlign: 'left' }}>Contract</th>
                <th style={{ textAlign: 'left' }}>Delivery</th>
                <th>Days</th>
                <th>Forward</th>
                <th title="Forward minus spot">Basis</th>
                <th title="Annualised cost of carry — the measured risk-free rate">Carry</th>
              </tr>
            </thead>
            <tbody>
              {futuresCurve.map((c) => (
                <tr key={c.code}>
                  <td style={{ textAlign: 'left', fontWeight: 600 }}>{c.code}</td>
                  <td style={{ textAlign: 'left', color: 'var(--color-ink-300)' }}>
                    {c.deliveryMonth}
                  </td>
                  <td>{c.daysToExpiry}</td>
                  <td>{n2(c.futuresPrice)}</td>
                  <td className={c.basis > 0 ? 'profit' : c.basis < 0 ? 'loss' : 'flat'}>
                    {c.basis >= 0 ? '+' : ''}{n2(c.basis)}
                  </td>
                  <td>{(c.annualizedYield * 100).toFixed(2)}%</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

export default TermStructure;
