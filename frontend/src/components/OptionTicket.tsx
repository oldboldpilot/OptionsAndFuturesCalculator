'use client';

import { useMemo } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import type { AsianStyle } from '../store/useTreePricerStore';

/**
 * The averaging segment's vocabulary, character for character the same as
 * `ExerciseStylePanel`'s.
 *
 * The two panels sit in adjacent columns and describe the same axis of the
 * same contract, so a different word here -- "Average price" against
 * "Avg price", or "None" against "Vanilla" -- would read as two different
 * settings rather than one. Kept as a literal array rather than imported from
 * that panel because it is copy, not contract: the CONTRACT is `AsianStyle`,
 * which both files take from the same module.
 */
const AVERAGING_CHOICES: [AsianStyle, string][] = [
  ['NOT_ASIAN', 'Vanilla'],
  ['AVERAGE_PRICE', 'Avg price'],
  ['AVERAGE_STRIKE', 'Avg strike'],
];

/**
 * The order ticket: compose one leg, then add it.
 *
 * This exists because the option was not previously expressible as a value.
 * The only way to add a leg was to find a row in a 116-strike ladder and hit a
 * 12-pixel B/S button, which fixed strike, premium, quantity and implied
 * volatility at whatever that row held. Three of those four are things a trader
 * routinely wants to change — a different fill price, ten contracts instead of
 * one, a vol assumption of their own.
 *
 * So the option becomes a form. Expiry and strike are selectors, price and
 * quantity are inputs, and the chain fills them in rather than acting as the
 * only way through. Clicking a bid or an ask over there lands here, editable,
 * instead of silently appending a position.
 *
 * Every quoted figure still comes from the chain. Nothing is defaulted: a
 * contract with no quote leaves the price field empty and says so, because a
 * premium of zero is an option that costs nothing, and that single substitution
 * makes every payoff number downstream wrong.
 */
export function OptionTicket() {
  const {
    ticket, setTicket, commitTicket,
    chainStrikes, chainExpirations, selectedExpiration, setSelectedExpiration,
    chainStatus, spotPrice,
  } = useCalculatorStore();

  // Keep the ticket's expiry in step with the chain the user is looking at.
  const expiry = ticket.expiration || selectedExpiration;

  const row = useMemo(
    () => chainStrikes.find((s) => s.strike === ticket.strike) ?? null,
    [chainStrikes, ticket.strike],
  );

  const quote = row ? (ticket.optionType === 'CALL' ? row.call : row.put) : null;

  // Buying lifts the ask, selling hits the bid — the executable side, not a
  // midpoint that no one can trade.
  const executable = quote ? (ticket.action === 'BUY' ? quote.ask : quote.bid) : 0;

  const moneyness = useMemo(() => {
    if (ticket.strike === null || spotPrice <= 0) return null;
    const pct = ((ticket.strike - spotPrice) / spotPrice) * 100;
    // Within a twentieth of a percent, call it at the money. Strictly a call
    // struck a cent above spot is out of the money, but labelling the ATM strike
    // "OTM +0.0%" reads as a rounding bug rather than as precision.
    if (Math.abs(pct) < 0.05) return { label: 'ATM', atm: true, pct };
    const itm = ticket.optionType === 'CALL' ? spotPrice > ticket.strike : spotPrice < ticket.strike;
    return { label: itm ? 'ITM' : 'OTM', atm: false, pct };
  }, [ticket.strike, ticket.optionType, spotPrice]);

  const contracts = ticket.quantity > 0 ? ticket.quantity : 1;
  const cost = ticket.premium !== null ? ticket.premium * 100 * contracts : null;

  function pickStrike(next: number | null) {
    if (next === null) {
      setTicket({ strike: null, premium: null, impliedVolatility: null });
      return;
    }
    const r = chainStrikes.find((s) => s.strike === next);
    const q = r ? (ticket.optionType === 'CALL' ? r.call : r.put) : null;
    const px = q ? (ticket.action === 'BUY' ? q.ask : q.bid) : 0;
    setTicket({
      strike: next,
      premium: px > 0 ? px : null,
      impliedVolatility: q && q.iv > 0 ? q.iv : null,
    });
  }

  // Changing side or direction repoints at the other quote for the same strike.
  function repriceFor(patch: Partial<typeof ticket>) {
    const nextType = patch.optionType ?? ticket.optionType;
    const nextAction = patch.action ?? ticket.action;
    const q = row ? (nextType === 'CALL' ? row.call : row.put) : null;
    const px = q ? (nextAction === 'BUY' ? q.ask : q.bid) : 0;
    setTicket({
      ...patch,
      premium: px > 0 ? px : ticket.premium,
      impliedVolatility: q && q.iv > 0 ? q.iv : ticket.impliedVolatility,
    });
  }

  const ready = ticket.strike !== null && ticket.premium !== null && ticket.premium > 0;

  return (
    <div className="panel">
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">Ticket</span>
          {moneyness && (
            <span
              className={moneyness.label === 'OTM' ? 'chip' : 'chip chip-accent'}
              title={`${moneyness.pct >= 0 ? '+' : ''}${moneyness.pct.toFixed(1)}% from spot ${spotPrice.toFixed(2)}`}
            >
              {moneyness.label}{moneyness.atm ? '' : ` ${moneyness.pct >= 0 ? '+' : ''}${moneyness.pct.toFixed(1)}%`}
            </span>
          )}
        </div>
      </div>

      <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', gap: '0.4375rem' }}>
        {/* Direction and right. Two-state segments rather than dropdowns: these
            are the choices most often changed, and they should take one click. */}
        <div style={{ display: 'flex', gap: '0.375rem' }}>
          <div className="segment" style={{ flex: 1 }}>
            <button
              className="segment-item" style={{ flex: 1 }}
              data-active={ticket.action === 'BUY'}
              onClick={() => repriceFor({ action: 'BUY' })}
            >Buy</button>
            <button
              className="segment-item" style={{ flex: 1 }}
              data-active={ticket.action === 'SELL'}
              onClick={() => repriceFor({ action: 'SELL' })}
            >Sell</button>
          </div>
          <div className="segment" style={{ flex: 1 }}>
            <button
              className="segment-item" style={{ flex: 1 }}
              data-active={ticket.optionType === 'CALL'}
              onClick={() => repriceFor({ optionType: 'CALL' })}
            >Call</button>
            <button
              className="segment-item" style={{ flex: 1 }}
              data-active={ticket.optionType === 'PUT'}
              onClick={() => repriceFor({ optionType: 'PUT' })}
            >Put</button>
          </div>
        </div>

        {/* Averaging. A peer of the Buy/Sell and Call/Put segments above and
            always visible, exactly as in ExerciseStylePanel -- hiding it
            behind a disclosure there made a shipped feature read as missing,
            and the same reasoning applies harder here, where the answer to
            "is this leg Asian?" changes what every downstream panel is even
            able to say. Vanilla is the default, so the row costs one segment
            and answers that question without a click. */}
        <div style={{ display: 'grid', gap: '0.1875rem' }}>
          <span className="stat-label">Averaging</span>
          <div className="segment" style={{ width: '100%' }}>
            {AVERAGING_CHOICES.map(([type, label]) => (
              <button
                key={type}
                className="segment-item"
                style={{ flex: 1 }}
                data-active={ticket.asianType === type}
                onClick={() => setTicket({ asianType: type })}
              >
                {label}
              </button>
            ))}
          </div>
          {/* Said BEFORE the leg is added, not after the engine refuses it.
              The refusal is correct and will still arrive, but a trader who
              learns of it only once the whole position has gone blank cannot
              tell which leg caused it. */}
          {ticket.asianType !== 'NOT_ASIAN' && (
            <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
              An Asian leg pays on the average price over its averaging window. The payoff,
              P&amp;L and probability panels are drawn against the price at expiry, so a
              position holding this leg will not be modelled &mdash; price it on its own in
              Exercise &amp; Averaging.
            </span>
          )}
        </div>

        <label style={{ display: 'grid', gap: '0.1875rem' }}>
          <span className="stat-label">Expiry</span>
          <select
            className="select"
            value={expiry}
            onChange={(e) => {
              setTicket({ expiration: e.target.value, strike: null, premium: null, impliedVolatility: null });
              setSelectedExpiration(e.target.value);
            }}
            disabled={chainExpirations.length === 0}
          >
            {chainExpirations.length === 0 && <option value="">Loading chain…</option>}
            {chainExpirations.map((e) => (
              <option key={e.date} value={e.date}>{e.date} · {e.dte}d</option>
            ))}
          </select>
        </label>

        <label style={{ display: 'grid', gap: '0.1875rem' }}>
          <span className="stat-label">Strike</span>
          <select
            className="select"
            value={ticket.strike ?? ''}
            onChange={(e) => pickStrike(e.target.value === '' ? null : Number(e.target.value))}
            disabled={chainStrikes.length === 0}
          >
            <option value="">
              {chainStatus === 'ready' ? `Select one of ${chainStrikes.length} strikes` : 'Loading chain…'}
            </option>
            {chainStrikes.map((s) => {
              const q = ticket.optionType === 'CALL' ? s.call : s.put;
              const px = ticket.action === 'BUY' ? q.ask : q.bid;
              return (
                <option key={s.strike} value={s.strike}>
                  {s.strike.toFixed(2)}{s.isAtm ? '  · ATM' : ''}{px > 0 ? `  ${px.toFixed(2)}` : '  no quote'}
                </option>
              );
            })}
          </select>
        </label>

        <div style={{ display: 'flex', gap: '0.375rem' }}>
          <label style={{ display: 'grid', gap: '0.1875rem', flex: 1 }}>
            <span className="stat-label">{ticket.action === 'BUY' ? 'Price paid' : 'Price received'}</span>
            <input
              className="input num"
              type="number" step="0.01" min="0"
              value={ticket.premium ?? ''}
              placeholder={quote && executable > 0 ? executable.toFixed(2) : 'no quote'}
              onChange={(e) => {
                const v = parseFloat(e.target.value);
                setTicket({ premium: Number.isNaN(v) ? null : v });
              }}
            />
          </label>
          <label style={{ display: 'grid', gap: '0.1875rem', width: '5rem' }}>
            <span className="stat-label">Contracts</span>
            <input
              className="input num"
              type="number" step="1" min="1"
              value={ticket.quantity}
              onChange={(e) => {
                const v = parseInt(e.target.value, 10);
                setTicket({ quantity: Number.isNaN(v) || v < 1 ? 1 : v });
              }}
            />
          </label>
          <label style={{ display: 'grid', gap: '0.1875rem', width: '5rem' }}>
            <span className="stat-label">IV %</span>
            <input
              className="input num"
              type="number" step="0.1" min="0"
              value={ticket.impliedVolatility !== null ? (ticket.impliedVolatility * 100).toFixed(1) : ''}
              placeholder="—"
              onChange={(e) => {
                const v = parseFloat(e.target.value);
                setTicket({ impliedVolatility: Number.isNaN(v) ? null : v / 100 });
              }}
            />
          </label>
        </div>

        <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between' }}>
          <span className="stat-label">
            {ticket.action === 'BUY' ? 'Debit' : 'Credit'} · {contracts} × 100
          </span>
          <span className="stat-value" style={{ fontVariantNumeric: 'tabular-nums' }}>
            {cost === null ? '—' : `$${cost.toFixed(2)}`}
          </span>
        </div>

        <button
          className="btn btn-primary"
          onClick={commitTicket}
          disabled={!ready}
          title={ready ? 'Add this leg to the position' : 'Pick a strike with a price first'}
        >
          {/* The averaging style is named on the button because it is the one
              ticket field with no other trace in the committed leg's own row
              wording -- "Add long call" for an average-price contract would
              be the button describing a different instrument from the one it
              adds. */}
          Add {ticket.action === 'BUY' ? 'long' : 'short'}{' '}
          {ticket.asianType === 'AVERAGE_PRICE'
            ? 'avg-price '
            : ticket.asianType === 'AVERAGE_STRIKE'
              ? 'avg-strike '
              : ''}
          {ticket.optionType.toLowerCase()}
        </button>
      </div>
    </div>
  );
}

export default OptionTicket;
