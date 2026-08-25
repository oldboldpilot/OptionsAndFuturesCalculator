'use client';

import React, { useEffect, useState } from 'react';
import { startCheckout } from '../lib/licence';
import { useProStatus } from '../lib/useProStatus';
import { useCalculatorStore } from '../store/useCalculatorStore';
import { useSavedScenariosStore, type SavedScenario } from '../store/useSavedScenariosStore';

/**
 * Save the current position under a name, and reopen a saved one.
 *
 * The panel states what it is for and gets out of the way, matching ProPanel's
 * register -- this sits in a trading terminal, and a loud upsell next to live
 * quotes is the wrong thing to make prominent.
 *
 * The two refusals it can receive are rendered DIFFERENTLY on purpose, and the
 * store has already discriminated them by gRPC status code rather than by
 * message text (see useSavedScenariosStore's own note, and entitlement.test.ts
 * for why text is not a safe discriminator here). Not signed in offers sign-in;
 * signed in but not Pro offers checkout. Collapsing the two would show a
 * subscriber a sign-in box, or ask an anonymous visitor to pay before they have
 * an account to attach it to.
 */
export const SavedScenarios: React.FC = () => {
  const { scenarios, status, failure, lastSavedName, refresh, save, remove, apply } =
    useSavedScenariosStore();
  const legCount = useCalculatorStore((s) => s.legs.length);
  const { pro } = useProStatus();
  const [name, setName] = useState('');
  const [busy, setBusy] = useState(false);

  // Refetched when the entitlement changes, not only on mount: signing in is
  // what turns the refusal below into a list, and without this the panel would
  // keep showing "sign in" until the page was reloaded.
  useEffect(() => {
    void refresh();
  }, [refresh, pro]);

  const onSave = async () => {
    setBusy(true);
    const ok = await save(name);
    if (ok) setName('');
    setBusy(false);
  };

  const body = () => {
    if (failure?.needsSignIn) {
      // The server's own sentence, not a fixed one. UNAUTHENTICATED covers two
      // different situations that want different words -- never signed in
      // ("Saved scenarios belong to an account...") and an account that has
      // since been deleted while its token is still valid ("This account no
      // longer exists. Sign in again."). Hardcoding the first tells the second
      // group something untrue.
      return (
        <div className="empty-state">
          <span className="empty-state-title">Sign in to save</span>
          <span>
            {failure.message ||
              'Saved scenarios belong to an account, so they follow you between devices.'}
          </span>
        </div>
      );
    }
    if (failure?.needsPro) {
      return (
        <div className="empty-state">
          <span className="empty-state-title">Needs Pro</span>
          <span>{failure.message}</span>
          <button
            className="btn"
            style={{ marginTop: 8 }}
            onClick={() => {
              void startCheckout('monthly').then((url) => {
                window.location.href = url;
              });
            }}
          >
            Upgrade
          </button>
        </div>
      );
    }

    return (
      <>
        <div style={{ display: 'flex', gap: 6, padding: '8px 10px' }}>
          <input
            className="input"
            style={{ flex: 1, minWidth: 0 }}
            placeholder="Name this position"
            value={name}
            maxLength={120}
            onChange={(e) => setName(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && !busy && legCount > 0) void onSave();
            }}
          />
          <button
            className="btn"
            disabled={busy || legCount === 0 || name.trim().length === 0}
            onClick={() => void onSave()}
            // Says which precondition is unmet rather than being inertly
            // greyed out -- a disabled control with no reason is a dead end.
            title={
              legCount === 0
                ? 'Add at least one leg before saving'
                : name.trim().length === 0
                  ? 'Give this scenario a name'
                  : 'Save this position'
            }
          >
            Save
          </button>
        </div>

        {failure && !failure.needsPro && !failure.needsSignIn && (
          <div style={{ padding: '0 10px 8px', fontSize: 'var(--text-2xs)', color: 'var(--color-loss)' }}>
            {failure.message}
          </div>
        )}
        {!failure && lastSavedName && (
          <div style={{ padding: '0 10px 8px', fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)' }}>
            Saved “{lastSavedName}”.
          </div>
        )}

        {scenarios.length === 0 ? (
          <div className="empty-state">
            <span className="empty-state-title">Nothing saved yet</span>
            <span>Build a position, name it, and it will be here next time.</span>
          </div>
        ) : (
          <table className="grid-table">
            <thead>
              <tr>
                <th style={{ textAlign: 'left' }}>Name</th>
                <th style={{ textAlign: 'left' }}>Symbol</th>
                <th>Legs</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {scenarios.map((s: SavedScenario) => (
                <tr key={s.id}>
                  <td style={{ textAlign: 'left' }} title={`Last saved ${s.updatedAt}`}>
                    {s.name}
                  </td>
                  <td style={{ textAlign: 'left' }}>{s.symbol}</td>
                  <td>{s.legs.length}</td>
                  <td style={{ whiteSpace: 'nowrap' }}>
                    <button className="btn" onClick={() => apply(s)} title="Load this position">
                      Open
                    </button>{' '}
                    <button
                      className="btn"
                      onClick={() => void remove(s.id)}
                      title="Delete this saved scenario"
                    >
                      ✕
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </>
    );
  };

  return (
    <div className="panel">
      <div className="panel-head">
        <span className="panel-title">
          Saved{scenarios.length > 0 ? ` · ${scenarios.length}` : ''}
        </span>
        {status === 'loading' && (
          <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)' }}>…</span>
        )}
      </div>
      <div className="panel-body panel-body--flush">{body()}</div>
    </div>
  );
};
