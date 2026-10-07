'use client';

import { useCalculatorStore } from '../store/useCalculatorStore';

/**
 * Says what happened to the position when the symbol changed, and offers the
 * way back.
 *
 * Switching instrument clears the legs -- a position has one underlying, and
 * the engine prices every leg off one spot -- so the position the user built
 * just disappeared. That is a deliberate design with a cost, and this notice is
 * what pays it: it names the instrument and the leg count, and one press of
 * Undo puts both the symbol and the legs back. It is a `status` region so a
 * screen reader hears the change as it happens rather than finding an empty
 * Position panel with no explanation.
 *
 * Shown only while there is something to restore AND nothing new has been
 * built. The store forgets the retired position the moment a leg is added, so
 * this can never offer to resurrect old work over the top of new work.
 */
export function SymbolSwitchNotice() {
  const { retiredPosition, symbol, legs, undoSymbolSwitch } = useCalculatorStore();
  if (!retiredPosition || legs.length > 0) return null;

  const n = retiredPosition.legs.length;
  return (
    <div className="notice" role="status" data-testid="symbol-switch-notice">
      <span>
        Switched to <strong>{symbol}</strong>. Your {n} {retiredPosition.symbol} leg{n === 1 ? '' : 's'}{' '}
        {n === 1 ? 'was' : 'were'} cleared: a position is built on one instrument.
      </span>
      <button className="btn" onClick={undoSymbolSwitch}>
        Undo, back to {retiredPosition.symbol}
      </button>
    </div>
  );
}

export default SymbolSwitchNotice;
