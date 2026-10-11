'use client';

import { useEffect, useRef, useState, useSyncExternalStore } from 'react';
import TopBar from './TopBar';
import ProbabilityCurve from './ProbabilityCurve';
import ResultsStrip from './ResultsStrip';
import SymbolSwitchNotice from './SymbolSwitchNotice';
import PositionLegs from './PositionLegs';
import StrategyMetrics from './StrategyMetrics';
import OptionChain from './OptionChain';
import PnLMatrix from './PnLMatrix';
import OptionTicket from './OptionTicket';
import FuturesTicket from './FuturesTicket';
import ExerciseStylePanel from './ExerciseStylePanel';
import PnLSurface from './PnLSurface';
import TermStructure from './TermStructure';
import { StrategySelector } from './StrategySelector';
import AssistantPanel from './AssistantPanel';
import { ProPanel } from './ProPanel';
import { SavedScenarios } from './SavedScenarios';
import { useCalculatorStore } from '../store/useCalculatorStore';
import { FUTURES_SLUGS, PICKER_STRATEGY_FOR_SLUG, type StrategySlug } from '../config/strategies';

type Area = 'build' | 'chain' | 'results';

/**
 * Whether a media query matches, kept current. False on the server, so the
 * prerendered HTML never claims a layout it cannot know.
 *
 * Used for two small things only -- which tab to SAY is selected, and when to
 * switch tab by itself. The layout itself is never chosen here: that is CSS,
 * because a static page that picked its layout in JavaScript would paint the
 * desktop grid on a phone before correcting it.
 */
function useMediaQuery(query: string): boolean {
  return useSyncExternalStore(
    (notify) => {
      const mq = window.matchMedia(query);
      mq.addEventListener('change', notify);
      return () => mq.removeEventListener('change', notify);
    },
    () => window.matchMedia(query).matches,
    () => false,
  );
}

const AREAS: { id: Area; label: string }[] = [
  { id: 'build', label: 'Build' },
  { id: 'chain', label: 'Chain' },
  { id: 'results', label: 'Results' },
];

/**
 * The calculator workspace.
 *
 * Five areas, ordered the way the work flows: choose a structure and compose
 * legs on the left, read the outcome in the centre, look things up on the right.
 * The probability curve is the centre of gravity, so it gets the widest region.
 *
 * HOW IT LAYS OUT is decided entirely in `globals.css` (see "Workspace layout"
 * there), by viewport width, with no JavaScript in the choice -- this is a
 * static export, and picking the layout after hydration would paint the desktop
 * grid on a phone first. This component only says WHICH AREA IS ACTIVE for the
 * widths where areas are tabs, and marks each region with the class the CSS
 * addresses.
 *
 *   a  assistant + strategy picker      d  option chain (+ term structure)
 *   b  ticket, position, exercise       e  outcome detail
 *   c  probability, matrix, surface
 *
 * Both the home route and every /calculator/[strategy] landing page render
 * this, so there is one calculator to maintain rather than two that drift.
 */
export function StrategyWorkspace({
  heading,
  guideHref,
  guideLabel = 'Learn how this strategy works →',
  strategySlug,
}: {
  heading?: string;
  /** The page this is rendered for, so the picker opens on its strategy. */
  strategySlug?: StrategySlug;
  /** Link to the written guide, when the page has one. */
  guideHref?: string;
  guideLabel?: string;
}) {
  const { calculateStrategy, legs, setSymbol, symbol, assetClass } = useCalculatorStore();
  const [area, setArea] = useState<Area>('build');
  const isMid = useMediaQuery('(min-width: 900px) and (max-width: 1279.98px)');
  // On a laptop the Build column is always visible, so "Build" is not a tab
  // there: it shows the chain until a position exists, and the tab bar should
  // say so rather than highlight nothing.
  const shownArea: Area = isMid && area === 'build' ? 'chain' : area;

  // The first leg ends the "pick a price" phase: show the answer. Adjusted
  // during render against the last count seen, which is what this rule
  // (react-hooks/set-state-in-effect) asks for in place of an effect. Only when
  // it is the FIRST leg -- later ones leave the visitor where they are.
  const [seenLegs, setSeenLegs] = useState(legs.length);
  if (legs.length !== seenLegs) {
    setSeenLegs(legs.length);
    if (seenLegs === 0 && legs.length > 0 && isMid && area !== 'results') setArea('results');
  }
  const tabRefs = useRef<Record<Area, HTMLButtonElement | null>>({ build: null, chain: null, results: null });

  // Fetch the opening quote and chain once on mount. A futures page opens on a
  // futures symbol: its structure cannot be built on an equity chain, and the
  // picker would otherwise open on a button reading "Select a futures symbol
  // first".
  useEffect(() => {
    if (strategySlug && FUTURES_SLUGS.has(strategySlug)) setSymbol('ES', undefined, 'FUTURES');
    else setSymbol(symbol);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    // Only compute once there is something to compute. Firing on an empty
    // position produced a meaningless "result" in the previous build.
    if (legs.length > 0) calculateStrategy();
  }, [legs, calculateStrategy]);

  // Arrow keys move between tabs, as the tab pattern promises.
  function onTabKey(e: React.KeyboardEvent, i: number) {
    const step = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
    if (step === 0) return;
    e.preventDefault();
    const next = AREAS[(i + step + AREAS.length) % AREAS.length].id;
    setArea(next);
    tabRefs.current[next]?.focus();
  }

  return (
    <div className="ws" data-tab={area}>
      {/* The page's own skip link goes to <main>; this one goes past the symbol
          bar and the strategy picker to the order ticket, which is where a
          keyboard user's work starts. */}
      <a className="skip-link" href="#ws-ticket" onClick={() => setArea('build')}>
        Skip to the order ticket
      </a>
      <TopBar />

      <div className="ws-bar">
        {heading && (
          <div className="ws-summary animate-fade">
            <h1 className="ws-title">{heading}</h1>
            {/* The route from the tool to the writing. A real link, because the
                article has its own page now; it is also the only crawlable path
                from a calculator page to its guide. */}
            {guideHref && (
              <a href={guideHref} className="ws-guide-link">
                {guideLabel}
              </a>
            )}
          </div>
        )}
        <ResultsStrip />
      </div>

      <div className="ws-main">
        <div className="ws-left">
          {/* Choose a structure. The PICKER comes first and the assistant is a
              collapsed row beneath it: the assistant is Pro-only, so putting it
              on top meant the first thing a visitor met was a paid feature and
              three paragraphs of model caveats, with the picker -- the way
              almost everyone starts -- pushed below the fold. */}
          <div className="ws-area ws-a stagger">
            <StrategySelector initialId={strategySlug ? PICKER_STRATEGY_FOR_SLUG[strategySlug] : undefined} />
            <AssistantPanel />
            <div className="panel" style={{ flex: 'none' }}>
              <ProPanel />
            </div>
          </div>

          {/*
            Compose the position: ticket first, then what it has built. Every
            wrapper is `flex: none` so panels stand at their natural height and
            let the AREA scroll -- the browser's default compresses them to fit
            and quietly clips their content (the ticket was squeezed to 308px
            against 327px of content, Position handed a 91px body).
          */}
          <div className="ws-area ws-b stagger">
            <SymbolSwitchNotice />
            {/* Each instrument has its own ticket. A future has no strike,
                premium or implied volatility, and the option ticket asked a
                futures trader for all three. */}
            <div className="ws-slot" id="ws-ticket" tabIndex={-1} style={{ flex: 'none' }}>
              {assetClass === 'FUTURES' ? <FuturesTicket /> : <OptionTicket />}
            </div>
            <div className="ws-slot" style={{ flex: '1 0 auto', minHeight: 260 }}>
              <PositionLegs />
            </div>
            {assetClass !== 'FUTURES' && (
              <div className="ws-slot" style={{ flex: 'none' }}>
                <ExerciseStylePanel />
              </div>
            )}
            <div className="ws-slot" style={{ flex: 'none' }}>
              <SavedScenarios />
            </div>
          </div>
        </div>

        <div className="ws-right">
          {/* Where a laptop or a phone switches between areas. Hidden on a
              desktop, where there is room for all of them. */}
          <div className="ws-tabs" role="tablist" aria-label="Workspace area">
            {AREAS.map((a, i) => (
              <button
                key={a.id}
                ref={(el) => { tabRefs.current[a.id] = el; }}
                role="tab"
                data-area={a.id}
                aria-selected={shownArea === a.id}
                tabIndex={shownArea === a.id ? 0 : -1}
                className="ws-tab"
                onClick={() => setArea(a.id)}
                onKeyDown={(e) => onTabKey(e, i)}
              >
                {a.label}
              </button>
            ))}
          </div>

          {/* Read the result. The curve takes the leftover and is the largest
              region here; `minHeight` rather than a fraction, so a short
              viewport shrinks the two panels below it before it eats the plot --
              a distribution under about 300px stops being readable as a shape. */}
          <div className="ws-area ws-c stagger">
            <div className="ws-slot ws-slot-prob">
              <ProbabilityCurve />
            </div>
            <div className="ws-slot ws-slot-matrix">
              <PnLMatrix />
            </div>
            <div className="ws-slot ws-slot-surface">
              <PnLSurface />
            </div>
          </div>

          {/* Reference: the chain, at full height, opened at the money. */}
          <div className="ws-area ws-d stagger">
            <div className="ws-slot ws-slot-term">
              <TermStructure />
            </div>
            <div className="ws-slot ws-slot-chain">
              <OptionChain />
            </div>
          </div>

          <div className="ws-area ws-e stagger">
            <StrategyMetrics />
          </div>
        </div>
      </div>
    </div>
  );
}

export default StrategyWorkspace;
