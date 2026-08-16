'use client';

import { useEffect } from 'react';
import TopBar from './TopBar';
import ProbabilityCurve from './ProbabilityCurve';
import PayoffLadder from './PayoffLadder';
import PositionLegs from './PositionLegs';
import StrategyMetrics from './StrategyMetrics';
import OptionChain from './OptionChain';
import PnLMatrix from './PnLMatrix';
import OptionTicket from './OptionTicket';
import ExerciseStylePanel from './ExerciseStylePanel';
import PnLSurface from './PnLSurface';
import TermStructure from './TermStructure';
import { StrategySelector } from './StrategySelector';
import AssistantPanel from './AssistantPanel';
import { useCalculatorStore } from '../store/useCalculatorStore';

/**
 * The calculator workspace.
 *
 * Three columns, ordered the way the work actually flows: pick the structure
 * and legs on the left, read the outcome in the centre, assess risk on the
 * right. The probability curve is the centre of gravity — it is where the
 * payoff and the odds of reaching it are visible at once — so it gets the
 * largest region rather than being demoted to a panel in a split view.
 *
 * Both the home route and every /calculator/[strategy] landing page render
 * this, so there is one calculator to maintain rather than two that drift.
 */
export function StrategyWorkspace({
  heading,
  guideHref,
}: {
  heading?: string;
  /** Anchor to the written guide below, when the page has one. */
  guideHref?: string;
}) {
  const { calculateStrategy, legs, setSymbol, symbol } = useCalculatorStore();

  // Fetch the opening quote and chain once on mount.
  useEffect(() => {
    setSymbol(symbol);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    // Only compute once there is something to compute. Firing on an empty
    // position produced a meaningless "result" in the previous build.
    if (legs.length > 0) calculateStrategy();
  }, [legs, calculateStrategy]);

  const column: React.CSSProperties = {
    display: 'flex',
    flexDirection: 'column',
    gap: '0.5rem',
    minHeight: 0,
  };

  return (
    /*
     * `overflowX: 'auto'`, `overflowY: 'hidden'` -- deliberately not the
     * `overflow: 'hidden'` this shell carried.
     *
     * The vertical half stays hidden on purpose: the shell is pinned to the
     * viewport height and every panel scrolls inside itself, so a page-level
     * vertical scroll would sit alongside a dozen panel scrollbars scrolling
     * different things. The HORIZONTAL half was the bug. Measured in Chrome at
     * a 768px viewport, this div's clientWidth was 768 against a scrollWidth of
     * 1238, so 470px of the workspace -- the whole right-hand column -- was cut
     * off. And because it CLIPPED rather than overflowed,
     * `document.documentElement.scrollWidth` also read 768: the page reported
     * that everything fitted while 2049 elements had a bounding right edge past
     * the viewport, and nothing on the page could be scrolled to reach them.
     *
     * Re-measured after the change: at 768 the shell scrolls 768 -> 1238 and
     * the rightmost column's right edge lands inside the viewport once scrolled
     * fully right; at 1280 and 1440 scrollWidth still equals clientWidth, so no
     * horizontal scrollbar appears at desktop widths. That last part is the
     * point -- `auto` produces a scrollbar only where one is needed, and a fix
     * that made every desktop width scroll sideways would be a regression.
     *
     * A sweep for clipping ancestors whose scrollWidth exceeds their clientWidth
     * returns this element plus four `.panel`s. The panels are not the same
     * thing: `.panel` clips by design and its `.panel-body` scrolls internally,
     * so their content is reachable. This div was the only one that clipped
     * with nothing behind it to scroll.
     */
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        height: '100vh',
        overflowX: 'auto',
        overflowY: 'hidden',
      }}
    >
      <TopBar />

      {heading && (
        <div
          className="animate-fade"
          style={{
            display: 'flex',
            alignItems: 'baseline',
            justifyContent: 'space-between',
            gap: '0.75rem',
            padding: '0.4375rem 0.75rem 0',
            flex: 'none',
          }}
        >
          <h1
            style={{
              fontSize: 'var(--text-base)',
              fontWeight: 600,
              color: 'var(--color-ink-200)',
            }}
          >
            {heading}
          </h1>
          {/*
            The only thing on the first screen that says the page continues.
            This shell is `height: 100vh`, so the article below opens exactly
            one viewport down with no visual hint above the fold that it is
            there. An anchor also works regardless of where the pointer is:
            every column here scrolls internally, so a wheel gesture over a
            panel scrolls that panel rather than the document.
          */}
          {guideHref && (
            <a
              href={guideHref}
              style={{
                fontSize: 'var(--text-2xs)',
                color: 'var(--color-accent)',
                textDecoration: 'none',
                whiteSpace: 'nowrap',
              }}
            >
              How this strategy works ↓
            </a>
          )}
        </div>
      )}

      <main
        style={{
          flex: 1,
          display: 'grid',
          // Four columns, and the chain gets a full-height one of its own.
          // It was sharing a column and resolving to 256px — nine of 116 rows —
          // which is what made the ladder unusable regardless of how it was
          // sorted or scrolled. Reference data needs height more than anything
          // else on this page.
          gridTemplateColumns:
            'minmax(240px, 280px) minmax(280px, 320px) minmax(0, 1fr) minmax(300px, 380px)',
          gap: '0.5rem',
          padding: '0.5rem',
          minHeight: 0,
        }}
      >
        {/* Choose a structure. The assistant sits ABOVE the picker because
            that is the order the work happens in when it is used at all: a
            parse selects an entry in the picker below it, and the picker's own
            Apply is still what builds the legs. Putting it below would have
            the output of one control appear above the control itself. */}
        <div className="stagger" style={{ ...column, overflowY: 'auto' }}>
          <AssistantPanel />
          <StrategySelector />
        </div>

        {/* Compose the position: ticket first, then what it has built. This
            column is the only one with natural-height panels and its own
            scroll -- columns 3/4 wrap panels in fixed flex fractions and
            `.panel` clips (globals.css:334-345), which already clipped
            content off-screen once (UpgradePrompt.tsx:66-73). The exercise
            panel's Bermudan date list has variable height, so it belongs
            here. */}
        <div className="stagger" style={{ ...column, overflowY: 'auto' }}>
          {/*
            `flexShrink: 0` on all three, which is the whole fix for this
            column.

            It scrolls (`overflowY: auto`), so its panels should stand at their
            natural height and let the COLUMN move. They were instead the flex
            default, `0 1 auto`, so the browser compressed them to fit the
            viewport and the column never had anything to scroll. Measured at
            1440x900: the ticket was squeezed to 308px against 327px of content
            and the exercise panel to 330 against 337 — both quietly clipped —
            while Position was handed 131px total, a 91px body, for a panel
            whose whole job is to list legs. Every leg added made it worse,
            because the compression is proportional.

            Position also gets `flex: 1` and a floor: it is the one panel here
            that grows without bound, so it should absorb spare height when
            there is some and never fall under roughly three legs when there is
            not.
          */}
          <div style={{ flexShrink: 0, display: 'flex' }}>
            <OptionTicket />
          </div>
          <div style={{ flex: 1, flexShrink: 0, minHeight: 260, display: 'flex' }}>
            <PositionLegs />
          </div>
          <div style={{ flexShrink: 0, display: 'flex' }}>
            <ExerciseStylePanel />
          </div>
        </div>

        {/* Read the result */}
        <div className="stagger" style={column}>
          {/*
            The curve takes the leftover, and it is now the LARGEST region in
            this column rather than the smallest.

            This file's own header says the probability curve is the centre of
            gravity and "gets the largest region rather than being demoted to a
            panel in a split view". The fractions below contradicted it: with
            the matrix at 40% and the surface at 30%, the curve got whatever
            remained, which measured 219px against their 314 and 235 at
            1440x900. The comment described the intent and the numbers
            described a split view.

            `minHeight` rather than a fraction, so a short viewport shrinks the
            two panels below before it starts eating the plot — a distribution
            under about 240px stops being readable as a shape at all.
          */}
          <div style={{ flex: 1, minHeight: 240, display: 'flex' }}>
            <ProbabilityCurve />
          </div>
          {/* The matrix wants width — a dozen date columns plus the price axis —
              so it lives in the widest column and scrolls internally rather
              than forcing the page to scroll sideways. It gives up 8 points of
              height for the curve above; it already scrolls internally, so the
              cost is rows per screen rather than reachable content. */}
          <div style={{ flex: '0 0 32%', minHeight: 0, display: 'flex' }}>
            <PnLMatrix />
          </div>
          {/* 26%, down from 30%. This panel is OFF by default and renders a
              one-line message until someone turns it on, so it was the most
              expensive idle space on the page. */}
          <div style={{ flex: '0 0 26%', minHeight: 0, display: 'flex' }}>
            <PnLSurface />
          </div>
        </div>

        {/* Reference: the chain, at full height, opened at the money */}
        <div className="stagger" style={column}>
          <TermStructure />
          {/*
            The chain takes the leftover height of this column, which is the
            point of giving it a column at all.

            There was a 300x250 `AdSlot size="rectangle"` below the metrics
            panel here. It was not an ad: NEXT_PUBLIC_ADSENSE_SLOT_RECTANGLE is
            unset, so AdSlot fell to its unconfigured branch and painted a
            dashed placeholder box — no <ins>, no slot id, nothing served and
            nothing earned. Measured at 1440x900 it held 250px while the chain's
            own scrollable body had 204px, showing about 7 of 126 strikes
            against a content height of 3564px. The largest single block in the
            column was an empty box.

            Removing it costs no revenue. The real unit is the multiplex below
            the workspace, which fills normally.
          */}
          <div style={{ flex: 1, minHeight: 260, display: 'flex' }}>
            <OptionChain />
          </div>
          <div style={{ flex: '0 0 30%', minHeight: 0, display: 'flex' }}>
            <StrategyMetrics />
          </div>
        </div>
      </main>
    </div>
  );
}

export default StrategyWorkspace;
