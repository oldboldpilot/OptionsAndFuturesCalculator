'use client';

import { useCalculatorStore } from '../store/useCalculatorStore';
import { formatMoney, useCurrency } from '../lib/currency';
import { UpgradePrompt } from './UpgradePrompt';

const money = (v: number) => formatMoney(v, Math.abs(v) >= 100 ? 0 : 2);

/** Colour follows the SIGN of the number, never the word beside it. */
const tone = (v: number) => (v > 0 ? 'profit' : v < 0 ? 'loss' : 'flat');

/** Every break-even, compactly: two prices in full, more than that as "first +N". */
function breakEvens(prices: number[]): string {
  if (prices.length === 0) return '—';
  if (prices.length <= 2) return prices.map(money).join(' / ');
  return `${money(prices[0])} +${prices.length - 1}`;
}

/**
 * The headline numbers, pinned above the fold.
 *
 * The one thing this tool exists to show is what a position can make, what it
 * can lose, where it breaks even and how likely it is to profit. Those used to
 * live only in the Outcome panel, which sat in a 380px column with a 603px
 * grid inside it: at 1280, 1440 and 1920 the LABELS were on screen and every
 * VALUE was 200 to 300px past the right edge, inside a shell that clips, with
 * no scrollbar to say so. Lighthouse scored the page 98 to 100 throughout --
 * a performance score cannot see where a number is drawn.
 *
 * So the figures that matter are drawn here, once, at the top, where nothing
 * can push them out of view. `data-result` marks each one for the layout test
 * that asserts exactly that.
 *
 * An UNBOUNDED end is stated as "Unlimited" and is never printed as a number.
 * The engine reports `max_profit_unbounded` when the payoff has no finite
 * maximum over the underlying's whole range, and in that case `max_profit` is
 * only the profit at the edge of the +/-25% grid it swept -- the long call that
 * read "$19,037" and "42.40 : 1" was the profit at spot x 1.25, while the
 * guide two clicks away said "Unlimited".
 */
export function ResultsStrip({ embedded = false }: { embedded?: boolean } = {}) {
  useCurrency();
  const { result, gateDenied, modelLimit, error, notReady, isLoading, legs, removeLeg } =
    useCalculatorStore();

  // One entitlement notice for the whole results area, with the way out.
  // Probability, matrix and outcome used to each carry the identical pitch.
  if (gateDenied) {
    const last = legs[legs.length - 1];
    return (
      <div className="rs" data-testid="results-strip" data-state="gate">
        <div className="rs-msg" role="status">
          {embedded ? (
            // Inside somebody else's page a checkout cannot start (Stripe will
            // not render in a frame) and a sign-up form has no place: say what
            // the engine said and point at the real site.
            <>
              <strong style={{ color: 'var(--color-accent-text)' }}>Needs Pro</strong>
              <span style={{ color: 'var(--color-ink-200)' }}>{gateDenied}</span>
              <a
                className="btn"
                href="https://optionsandfuturescalculator.com"
                target="_blank"
                rel="noopener noreferrer"
              >
                Open the full calculator
              </a>
            </>
          ) : (
            <UpgradePrompt reason={gateDenied} compact />
          )}
          {last && (
            <button className="btn" onClick={() => removeLeg(last.id)}>
              Remove last leg
            </button>
          )}
        </div>
      </div>
    );
  }

  const message = modelLimit
    ? ['Not modelled', modelLimit]
    : error
      ? ['Unavailable', error]
      : !result && notReady
        ? ['Not priced yet', notReady]
        : null;

  if (message && !result) {
    return (
      <div className="rs" data-testid="results-strip" data-state="message">
        <div className="rs-msg" role="status">
          <strong style={{ color: 'var(--color-ink-100)' }}>{message[0]}</strong>
          <span>{message[1]}</span>
        </div>
      </div>
    );
  }

  if (!result) {
    return (
      <div className="rs" data-testid="results-strip" data-state="empty">
        <div className="rs-msg">
          {legs.length === 0
            ? 'Pick a price in the option chain, press Add, and your max profit, max loss and break-even appear here.'
            : 'Pricing the position…'}
        </div>
      </div>
    );
  }

  const profitText = result.max_profit_unbounded ? 'Unlimited' : money(result.max_profit);
  const lossText = result.max_loss_unbounded ? 'Unlimited' : money(result.max_loss);
  const popText = result.pop > 0 ? `${(result.pop * 100).toFixed(1)}%` : '—';

  return (
    <div
      className="rs"
      data-testid="results-strip"
      data-state="result"
      data-stale={isLoading ? 'true' : undefined}
    >
      {/* Spoken once per calculation, not once per figure. */}
      <span className="sr-only" role="status">
        {`Maximum profit ${profitText}. Maximum loss ${lossText}. Break-even ${breakEvens(result.breakeven_prices)}. Probability of profit ${popText}.`}
      </span>
      <div className="rs-item">
        <span className="rs-label">Max profit</span>
        <span
          className={`rs-value ${result.max_profit_unbounded ? 'profit' : tone(result.max_profit)}`}
          data-result="max-profit"
          title={
            result.max_profit_unbounded
              ? 'No ceiling: this position keeps gaining as the underlying rises.'
              : undefined
          }
        >
          {profitText}
        </span>
      </div>
      <div className="rs-item">
        <span className="rs-label">Max loss</span>
        <span
          className={`rs-value ${result.max_loss_unbounded ? 'loss' : tone(result.max_loss)}`}
          data-result="max-loss"
          title={
            result.max_loss_unbounded
              ? 'No floor: this position keeps losing as the underlying rises.'
              : undefined
          }
        >
          {lossText}
        </span>
      </div>
      <div className="rs-item">
        <span className="rs-label">Break-even</span>
        <span className="rs-value" data-result="break-even">
          {breakEvens(result.breakeven_prices)}
        </span>
      </div>
      <div className="rs-item">
        <span className="rs-label">
          <span className="rs-long">Chance of profit</span>
          <span className="rs-short">P(profit)</span>
        </span>
        <span className="rs-value" data-result="pop">
          {popText}
        </span>
      </div>
      <div className="rs-item rs-item--extra">
        <span className="rs-label">Expected value</span>
        <span className={`rs-value ${tone(result.expected_value)}`} data-result="ev">
          {money(result.expected_value)}
        </span>
      </div>
      <div className="rs-item rs-item--extra">
        <span className="rs-label">Risk / reward</span>
        <span className="rs-value" data-result="rr">
          {result.risk_reward_ratio > 0 ? `${result.risk_reward_ratio.toFixed(2)} : 1` : '—'}
        </span>
      </div>
    </div>
  );
}

export default ResultsStrip;
