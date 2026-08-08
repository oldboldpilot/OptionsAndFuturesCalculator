'use client';

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The Bermudan exercise-date builder: a date PICKER, never free text.
 *
 * Every date on this list eventually becomes one entry in
 * `OptionTreeRequest.bermudan_dates` -- a year fraction the engine matches
 * against its own backward-induction step times to within half a step. A
 * date the user could type freely (a raw day count, a raw year fraction) is
 * a date that could land strictly between two real tree steps and silently
 * contribute nothing while still being labelled Bermudan -- the worst
 * failure class this panel guards against. Presets generate dates that are
 * always in range by construction; the one free-text entry point (add at
 * day N) still gets range-checked before it can ever reach a chip.
 */
import { useState } from 'react';
import { useTreePricerStore } from '../store/useTreePricerStore';

export function BermudanDateBuilder({ yearsToExpiry }: { yearsToExpiry: number }) {
  const { bermudanDates, addBermudanPreset, addBermudanDay, removeBermudanDate } =
    useTreePricerStore();
  const [dayInput, setDayInput] = useState('');

  const disabled = yearsToExpiry <= 0;
  const maxDays = Math.floor(yearsToExpiry * 365);

  return (
    <div style={{ display: 'grid', gap: '0.375rem' }}>
      <div style={{ display: 'flex', gap: '0.25rem', flexWrap: 'wrap' }}>
        <button
          className="btn"
          disabled={disabled}
          onClick={() => addBermudanPreset('MONTHLY', yearsToExpiry)}
        >
          Monthly
        </button>
        <button
          className="btn"
          disabled={disabled}
          onClick={() => addBermudanPreset('QUARTERLY', yearsToExpiry)}
        >
          Quarterly
        </button>
        <button
          className="btn"
          disabled={disabled}
          onClick={() => addBermudanPreset('SEMI_ANNUAL', yearsToExpiry)}
        >
          Semi-annual
        </button>
      </div>

      {bermudanDates.length > 0 && (
        <div style={{ display: 'flex', gap: '0.25rem', flexWrap: 'wrap' }}>
          {bermudanDates
            .slice()
            .sort((a, b) => a.days - b.days)
            .map((d) => (
              <span
                key={d.id}
                className="chip"
                title={`${d.yearFraction.toFixed(4)}y from now -- the wire unit`}
              >
                {Math.round(d.days)}d &middot; {d.yearFraction.toFixed(3)}y
                <button
                  onClick={() => removeBermudanDate(d.id)}
                  aria-label={`Remove exercise date at ${Math.round(d.days)} days`}
                  style={{
                    marginLeft: '0.3125rem',
                    background: 'none',
                    border: 'none',
                    color: 'inherit',
                    cursor: 'pointer',
                    padding: 0,
                    font: 'inherit',
                    lineHeight: 1,
                  }}
                >
                  &times;
                </button>
              </span>
            ))}
        </div>
      )}

      <input
        className="input num"
        type="number"
        min={1}
        max={maxDays > 0 ? maxDays : undefined}
        step={1}
        placeholder={disabled ? 'pick an expiry first' : `add at day N (1-${maxDays})`}
        value={dayInput}
        disabled={disabled}
        onChange={(e) => setDayInput(e.target.value)}
        onKeyDown={(e) => {
          if (e.key !== 'Enter') return;
          const n = parseInt(dayInput, 10);
          if (!Number.isNaN(n) && n > 0) {
            addBermudanDay(n, yearsToExpiry);
            setDayInput('');
          }
        }}
      />
    </div>
  );
}

export default BermudanDateBuilder;
