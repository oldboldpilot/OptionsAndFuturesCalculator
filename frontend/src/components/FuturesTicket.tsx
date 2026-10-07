'use client';

import { useState } from 'react';
import { formatAmount, formatMoney, useCurrency } from '../lib/currency';
import { MoneyInput } from './MoneyInput';
import { useCalculatorStore } from '../store/useCalculatorStore';
import { FUTURES_ROOTS } from '../config/futures';

/**
 * The order ticket for a futures symbol.
 *
 * A future is not an option with the strike left blank. It has no strike, no
 * premium and no implied volatility in its payoff: it is a contract, a side, a
 * price you entered at and a quantity, and it pays (price - entry) x multiplier
 * per contract. The option ticket asked a futures trader for a strike from a
 * chain that does not exist, an expiry from a list of option dates, and an IV
 * the position does not use -- and a futures position built through the picker
 * then answered "Not priced yet ... enter an IV in the ticket", pointing at a
 * ticket that had no such thing for a future.
 *
 * Volatility is offered, optional, and said to be optional: payoff, maximum
 * profit and loss, break-even and the matrix are exact without it, and it is
 * needed only to draw the probability distribution. Left empty, the engine is
 * sent no volatility and the probability panels say so, rather than a default
 * being invented to fill them.
 */
export function FuturesTicket() {
  useCurrency();
  const { symbol, spotPrice, futuresCurve, chainStatus, addLeg } = useCalculatorStore();

  const [action, setAction] = useState<'BUY' | 'SELL'>('BUY');
  const [code, setCode] = useState('');
  const [quantity, setQuantity] = useState(1);
  // `null` = follow the contract's own forward price; a number = the trader's fill.
  const [price, setPrice] = useState<number | null>(null);
  const [volPct, setVolPct] = useState('');

  const root = FUTURES_ROOTS[symbol];
  const multiplier = root?.multiplier;
  const contract = futuresCurve.find((c) => c.code === code) ?? futuresCurve[0];
  const entry = price ?? contract?.futuresPrice ?? null;

  const vol = Number.parseFloat(volPct);
  const volValid = volPct.trim() !== '' && Number.isFinite(vol) && vol > 0 && vol < 500;

  const ready = Boolean(contract) && entry !== null && entry > 0 && multiplier !== undefined;
  const notional =
    ready && multiplier !== undefined && entry !== null ? entry * multiplier * quantity : null;

  function add() {
    if (!ready || !contract || entry === null || multiplier === undefined) return;
    addLeg({
      instrument_type: 'INSTRUMENT_FUTURES_SPOT',
      action,
      option_type: 'FUTURE',
      strike_price: entry,
      premium: entry,
      quantity,
      expiration_days: contract.daysToExpiry,
      implied_volatility: volValid ? vol / 100 : undefined,
      contract_multiplier: multiplier,
      expiry_label: contract.code,
    });
    // The next leg starts from the forward again, not from this fill.
    setPrice(null);
  }

  return (
    <div className="panel">
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <h2 className="panel-title">Futures ticket</h2>
          <span className="chip chip-accent">{symbol}</span>
        </div>
      </div>

      <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', gap: '0.4375rem' }}>
        <div className="segment" role="group" aria-label="Action">
          <button
            className="segment-item" style={{ flex: 1 }}
            data-active={action === 'BUY'} aria-pressed={action === 'BUY'}
            onClick={() => setAction('BUY')}
          >Buy</button>
          <button
            className="segment-item" style={{ flex: 1 }}
            data-active={action === 'SELL'} aria-pressed={action === 'SELL'}
            onClick={() => setAction('SELL')}
          >Sell</button>
        </div>

        <label style={{ display: 'grid', gap: '0.1875rem' }}>
          <span className="stat-label">Contract</span>
          <select
            className="select"
            value={contract?.code ?? ''}
            onChange={(e) => { setCode(e.target.value); setPrice(null); }}
            disabled={futuresCurve.length === 0}
          >
            {futuresCurve.length === 0 && (
              <option value="">
                {chainStatus === 'error' ? 'No contracts available' : 'Loading contracts…'}
              </option>
            )}
            {futuresCurve.map((c) => (
              <option key={c.code} value={c.code}>
                {c.code} · {c.deliveryMonth} · {c.daysToExpiry}d · {formatAmount(c.futuresPrice, 2)}
              </option>
            ))}
          </select>
        </label>

        <div style={{ display: 'flex', gap: '0.375rem' }}>
          <label style={{ display: 'grid', gap: '0.1875rem', flex: 1 }}>
            <span className="stat-label">Entry price</span>
            <MoneyInput
              value={entry ?? NaN}
              onChange={(v) => setPrice(Number.isNaN(v) ? null : v)}
              decimals={2}
              ariaLabel="Entry price"
              placeholder={contract ? formatAmount(contract.futuresPrice, 2) : '—'}
              className="input num"
            />
          </label>
          <label style={{ display: 'grid', gap: '0.1875rem', width: '5rem' }}>
            <span className="stat-label">Contracts</span>
            <input
              className="input num"
              type="number" step="1" min="1"
              value={quantity}
              onChange={(e) => {
                const v = parseInt(e.target.value, 10);
                setQuantity(Number.isNaN(v) || v < 1 ? 1 : v);
              }}
            />
          </label>
        </div>

        <label style={{ display: 'grid', gap: '0.1875rem' }}>
          <span className="stat-label">Volatility % (optional)</span>
          <input
            className="input num"
            inputMode="decimal"
            placeholder="blank = no probabilities"
            value={volPct}
            aria-describedby="fut-vol-help"
            onChange={(e) => setVolPct(e.target.value)}
          />
          <span id="fut-vol-help" style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)' }}>
            Only draws the probability curve. Payoff, max profit and loss, break-even and the matrix
            are exact without it.
          </span>
        </label>

        <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', gap: '0.5rem' }}>
          <span className="stat-label">
            {multiplier !== undefined
              ? `Notional · ${quantity} × ${formatMoney(multiplier, 0)} per point`
              : 'Notional'}
          </span>
          <span className="stat-value">{notional === null ? '—' : formatMoney(notional, 0)}</span>
        </div>
        {multiplier === undefined && (
          <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-loss)' }}>
            No contract specification for {symbol}, so its dollar value per point is unknown.
          </span>
        )}
        {spotPrice > 0 && (
          <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)' }}>
            Spot {formatAmount(spotPrice, 2)}. Forward prices are derived from spot and the Treasury rate.
          </span>
        )}

        <button className="btn btn-primary" onClick={add} disabled={!ready}>
          Add {action === 'BUY' ? 'long' : 'short'} {contract ? contract.code : symbol}
        </button>
      </div>
    </div>
  );
}

export default FuturesTicket;
