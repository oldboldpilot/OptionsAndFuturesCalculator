'use client';

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { formatAmount, formatMoney, useCurrency } from '../lib/currency';
import { GateNote } from './GateNote';
import { useCalculatorStore, type CurvePoint } from '../store/useCalculatorStore';

/* ---------------------------------------------------------------------------
   Distribution maths.

   These mirror `sensen::OptionStrategyBuilder::probability_of_profit` in
   backend/sensen/src/financial.cppm, which models the terminal underlying
   price as lognormal under GBM and integrates the density over the regions
   where profit(x) > 0. Using the same formulation here means the shaded area
   and the engine's reported POP describe the same quantity — if they ever
   disagree, that is a real discrepancy worth seeing, not a plotting artefact.
   ------------------------------------------------------------------------- */

/** Lognormal density of the terminal price. mu/sd are of log(S_T). */
function lognormalPdf(x: number, mu: number, sd: number): number {
  if (x <= 0 || sd <= 0) return 0;
  const z = (Math.log(x) - mu) / sd;
  return Math.exp(-0.5 * z * z) / (x * sd * Math.SQRT2 * Math.sqrt(Math.PI));
}

/** Abramowitz & Stegun 7.1.26 — accurate to ~1.5e-7, ample for a chart. */
function erf(x: number): number {
  const sign = x < 0 ? -1 : 1;
  const a = Math.abs(x);
  const t = 1 / (1 + 0.3275911 * a);
  const y =
    1 -
    ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t +
      0.254829592) *
      t *
      Math.exp(-a * a);
  return sign * y;
}

function normalCdf(z: number): number {
  return 0.5 * (1 + erf(z / Math.SQRT2));
}

/** Linear interpolation of the engine's at-expiry payoff at an arbitrary price. */
function pnlAt(curve: CurvePoint[], price: number): number | null {
  if (curve.length === 0) return null;
  if (price <= curve[0].price) return curve[0].pnl;
  if (price >= curve[curve.length - 1].price) return curve[curve.length - 1].pnl;
  for (let i = 1; i < curve.length; i++) {
    const a = curve[i - 1];
    const b = curve[i];
    if (price <= b.price) {
      const t = (price - a.price) / (b.price - a.price || 1);
      return a.pnl + t * (b.pnl - a.pnl);
    }
  }
  return curve[curve.length - 1].pnl;
}

/**
 * The chart is drawn in the pixels it is shown in.
 *
 * It used to be a fixed 760x268 viewBox stretched with
 * `preserveAspectRatio="none"` into whatever box the layout gave it. At the
 * 398x170 the centre column measured, that is a 0.52 horizontal and 0.63
 * vertical scale, so 10px labels drew at about 5px and circles would have been
 * ovals. The plot is now laid out against the measured width and height, so one
 * SVG unit is one CSS pixel and the type is the size it says it is.
 */
const FALLBACK = { w: 760, h: 268 };
const PAD = { l: 52, r: 10, t: 20, b: 26 };
const FONT = 11;

/** Width and height of an element, kept current. Null until first measured. */
function useElementSize<T extends HTMLElement>() {
  const [size, setSize] = useState<{ w: number; h: number } | null>(null);
  const ref = useRef<T | null>(null);
  const roRef = useRef<ResizeObserver | null>(null);

  const attach = useCallback((el: T | null) => {
    roRef.current?.disconnect();
    ref.current = el;
    if (!el || typeof ResizeObserver === 'undefined') return;
    const measure = () => {
      const r = el.getBoundingClientRect();
      setSize((prev) =>
        prev && Math.abs(prev.w - r.width) < 0.5 && Math.abs(prev.h - r.height) < 0.5
          ? prev
          : { w: r.width, h: r.height },
      );
    };
    measure();
    roRef.current = new ResizeObserver(measure);
    roRef.current.observe(el);
  }, []);

  useEffect(() => () => roRef.current?.disconnect(), []);
  return [attach, size] as const;
}

/** The second copy of StrategyMetrics' helper; both now share one formatter. */
const money = (v: number) => formatMoney(v, Math.abs(v) >= 100 ? 0 : 2);

/**
 * Terminal price distribution with the strategy payoff overlaid.
 *
 * Three things are drawn together, because they only mean something together:
 *   1. where the underlying is likely to land (lognormal density),
 *   2. what the position pays at each of those prices (engine payoff curve),
 *   3. the intersection — density shaded teal where the payoff is positive.
 *
 * Provenance is explicit. The payoff, POP and expected value come from the
 * engine. The density is *modelled* from live spot, chain IV and the leg's
 * real days-to-expiry, and is labelled MODELLED per spec §3.4. Nothing is
 * drawn at all until those real inputs exist.
 */
export function ProbabilityCurve() {
  // Subscribes this component to the currency picker. Without it the amounts
  // keep the previous locale's separators until something else re-renders.
  useCurrency();
  const { result, error, isLoading, symbol, modelLimit, gateDenied, notReady } = useCalculatorStore();
  const [hoverX, setHoverX] = useState<number | null>(null);
  // True once the crosshair has been moved by keyboard, so the readout is then
  // announced -- a mouse hover must not make a screen reader chatter.
  const [keyboardMoved, setKeyboardMoved] = useState(false);
  const [hostRef, measured] = useElementSize<HTMLDivElement>();
  const W = Math.max(measured?.w ?? FALLBACK.w, PAD.l + PAD.r + 40);
  const H = Math.max(measured?.h ?? FALLBACK.h, PAD.t + PAD.b + 60);

  const model = useMemo(() => {
    if (!result) return null;
    // curveDays, not days. The engine draws the payoff and computes PoP at the
    // EARLIEST leg expiry; for a calendar spread that is well short of the
    // position horizon. Modelling the density over the horizon instead would
    // shade one distribution beneath a PoP taken from another — two answers to
    // the same question in a single panel.
    const {
      spot,
      impliedVolatility: iv,
      curveDays: days,
      riskFreeRate: r,
      dividendYield: q,
    } = result.inputs;
    const curve = result.expiryCurve;
    if (spot <= 0 || iv <= 0 || days <= 0 || curve.length < 2) return null;

    const T = days / 365;
    const sd = iv * Math.sqrt(T);
    // Drift is (r - q), matching the engine's lognormal_over. A dividend-paying
    // underlying has a lower forward; shading a distribution centred on r while
    // the engine's PoP came from one centred on (r - q) would put two different
    // answers in the same panel.
    const mu = Math.log(spot) + (r - q - 0.5 * iv * iv) * T;

    // Span ±3.2σ in log space — wide enough that the tails visibly close.
    const lo = Math.max(0.01, spot * Math.exp(-3.2 * sd));
    const hi = spot * Math.exp(3.2 * sd);

    const N = 260;
    const samples = Array.from({ length: N + 1 }, (_, i) => {
      const price = lo + ((hi - lo) * i) / N;
      return {
        price,
        density: lognormalPdf(price, mu, sd),
        pnl: pnlAt(curve, price) ?? 0,
      };
    });

    const maxDensity = Math.max(...samples.map((s) => s.density), 1e-12);
    const pnlMax = Math.max(...samples.map((s) => s.pnl));
    const pnlMin = Math.min(...samples.map((s) => s.pnl));
    const pnlSpan = Math.max(Math.abs(pnlMax), Math.abs(pnlMin), 1e-9);

    // Riemann sum of the density over profitable prices — the same integral
    // sensen performs. Used only when the engine reports no POP of its own.
    const dx = (hi - lo) / N;
    const derivedPop = samples.reduce(
      (acc, s) => (s.pnl > 0 ? acc + s.density * dx : acc),
      0,
    );

    // Sign changes in the payoff are the break-evens the trader actually cares
    // about, and there can be several — an iron condor has two.
    const breakEvens: number[] = [];
    for (let i = 1; i < samples.length; i++) {
      const a = samples[i - 1];
      const b = samples[i];
      if (a.pnl === 0 || a.pnl * b.pnl < 0) {
        const t = Math.abs(a.pnl) / (Math.abs(a.pnl) + Math.abs(b.pnl) || 1);
        breakEvens.push(a.price + t * (b.price - a.price));
      }
    }

    const plotW = W - PAD.l - PAD.r;
    const plotH = H - PAD.t - PAD.b;
    const x = (price: number) => PAD.l + ((price - lo) / (hi - lo)) * plotW;
    // The density occupies the lower 62% of the plot; the payoff line rides
    // above it on its own symmetric scale, so both stay legible.
    const yDensity = (d: number) => H - PAD.b - (d / maxDensity) * plotH * 0.62;
    const yPnl = (p: number) => PAD.t + plotH * 0.30 - (p / pnlSpan) * plotH * 0.26;

    // Contiguous runs of one sign, so each shaded region is a single closed
    // path and profit/loss areas never bleed into each other.
    const regions: { profitable: boolean; d: string }[] = [];
    let run: typeof samples = [];
    let runSign = samples[0].pnl > 0;
    const flush = () => {
      if (run.length < 2) return;
      const head = `M ${x(run[0].price).toFixed(2)} ${(H - PAD.b).toFixed(2)}`;
      const body = run
        .map((s) => `L ${x(s.price).toFixed(2)} ${yDensity(s.density).toFixed(2)}`)
        .join(' ');
      const tail = `L ${x(run[run.length - 1].price).toFixed(2)} ${(H - PAD.b).toFixed(2)} Z`;
      regions.push({ profitable: runSign, d: `${head} ${body} ${tail}` });
    };
    for (const s of samples) {
      const sign = s.pnl > 0;
      if (sign !== runSign) {
        run.push(s);
        flush();
        run = [s];
        runSign = sign;
      } else {
        run.push(s);
      }
    }
    flush();

    const densityLine = samples
      .map((s, i) => `${i === 0 ? 'M' : 'L'} ${x(s.price).toFixed(2)} ${yDensity(s.density).toFixed(2)}`)
      .join(' ');

    const pnlLine = samples
      .map((s, i) => `${i === 0 ? 'M' : 'L'} ${x(s.price).toFixed(2)} ${yPnl(s.pnl).toFixed(2)}`)
      .join(' ');

    const sigma = (k: number) => ({
      lo: spot * Math.exp(-k * sd),
      hi: spot * Math.exp(k * sd),
    });

    return {
      lo, hi, spot, iv, days, sd, mu, T,
      samples, maxDensity, pnlSpan, derivedPop, breakEvens,
      regions, densityLine, pnlLine, x, yDensity, yPnl,
      oneSigma: sigma(1),
      twoSigma: sigma(2),
      /** P(S_T ≤ price) under the same lognormal. */
      cdf: (price: number) => normalCdf((Math.log(price) - mu) / sd),
    };
  }, [result, W, H]);

  const shell = (body: React.ReactNode, chips?: React.ReactNode) => (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <h2 className="panel-title">Probability Distribution</h2>
          {chips}
        </div>
      </div>
      <div className="panel-body" style={{ flex: 1, display: 'flex', flexDirection: 'column' }}>
        {body}
      </div>
    </div>
  );

  // Checked before `error` and rendered through the PLAIN empty state. The
  // distinction matters most on this panel: the curve it draws is the terminal
  // price distribution, and an Asian leg is refused precisely BECAUSE its
  // payoff is not a function of that variable. Saying "unavailable" in loss red
  // would describe a broken feed; what actually happened is that the question
  // this panel asks does not apply to the position. See PayoffLadder for the
  // shared reasoning, and the store for why it is matched on the gRPC code.
  if (modelLimit) {
    return shell(
      <div className="empty-state">
        <span className="empty-state-title">Distribution not modelled</span>
        <span>{modelLimit}</span>
      </div>,
    );
  }

  if (error) {
    return shell(
      <div className="empty-state empty-state--error">
        <span className="empty-state-title">Distribution unavailable</span>
        <span>{error}</span>
      </div>,
    );
  }

  if (isLoading) {
    return shell(
      <div style={{ display: 'flex', flexDirection: 'column', gap: '0.5rem', flex: 1 }}>
        <div className="skeleton" style={{ flex: 1, minHeight: 120 }} />
        <div className="skeleton" style={{ height: 34 }} />
      </div>,
    );
  }

  if (!model) {
    if (gateDenied) {
      return shell(<GateNote />);
    }

    /* Replaces the empty state when set, sitting below the panel's
       rendered-output branch so it cannot replace a drawn chart. */
    if (notReady) {
      return shell(
        /* A precondition, in the NEUTRAL style -- deliberately not the
           --error branch above. "You have not picked a strike yet" is the
           next thing to do, not a failure, and rendering it as "Unavailable"
           in loss red tells a trader the calculator is broken when nothing
           is wrong. The title names the action for the same reason. */
        <div className="empty-state">
          <span className="empty-state-title">Not priced yet</span>
          <span>{notReady}</span>
        </div>,
      );
    }

    // A position with a result but no volatility -- a future with none entered.
    // Everything else on screen is exact without one; only this panel needs it.
    if (result && result.inputs.impliedVolatility <= 0) {
      return shell(
        <div className="empty-state">
          <span className="empty-state-title">Needs a volatility</span>
          <span>
            The distribution is drawn from an implied volatility, and this position has none: a
            future&apos;s payoff does not depend on it. The profit, loss, break-even and matrix are
            exact without one. Enter a volatility in the futures ticket to draw the odds.
          </span>
        </div>,
      );
    }

    return shell(
      <div className="empty-state">
        <span className="empty-state-title">No distribution yet</span>
        <span>
          The curve needs a live spot, an implied volatility off the option
          chain, and a real expiry. Add priced legs to draw it.
        </span>
      </div>,
    );
  }

  const enginePop = result?.pop ?? 0;
  const popValue = enginePop > 0 ? enginePop : model.derivedPop;
  const popIsEngine = enginePop > 0;
  const profitUnbounded = result?.max_profit_unbounded ?? false;
  const lossUnbounded = result?.max_loss_unbounded ?? false;
  const curve = result!.expiryCurve;

  const plotW = W - PAD.l - PAD.r;
  const xToPrice = (x: number) => model.lo + ((x - PAD.l) / plotW) * (model.hi - model.lo);
  const hoverPrice = hoverX === null ? null : xToPrice(hoverX);
  const hoverPnl = hoverPrice === null ? null : pnlAt(curve, hoverPrice) ?? null;

  const tickCount = W < 420 ? 4 : 7;
  const axisTicks = Array.from(
    { length: tickCount },
    (_, i) => model.lo + ((model.hi - model.lo) * i) / (tickCount - 1),
  );

  // The payoff's own extremes, drawn as a labelled axis. The line was plotted
  // with no scale at all -- no zero line label, no P&L values, nothing but a
  // hover readout -- so a reader could see its shape and not read its height.
  const pnlTop = Math.max(...model.samples.map((x) => x.pnl));
  const pnlBottom = Math.min(...model.samples.map((x) => x.pnl));
  const pnlTicks = [pnlTop, 0, pnlBottom].filter(
    (v, i, a) => a.indexOf(v) === i && (v === 0 || Math.abs(model.yPnl(v) - model.yPnl(0)) > FONT + 2),
  );

  // What a screen reader gets instead of the picture. Generated from the same
  // numbers the chart is drawn from, so it cannot describe a different chart.
  const breakEvenText = model.breakEvens.length
    ? `Break-even ${model.breakEvens.map((b) => formatAmount(b, 2)).join(' and ')}.`
    : 'No break-even within the range shown.';
  const summary =
    `Probability distribution of ${symbol} at expiry, ${model.days} days out, with the position's profit and loss overlaid. ` +
    `Probability of profit ${(popValue * 100).toFixed(1)} percent. ` +
    `${profitUnbounded ? 'Profit has no ceiling.' : `Best case ${money(pnlTop)}.`} ` +
    `${lossUnbounded ? 'Loss has no floor.' : `Worst case ${money(pnlBottom)}.`} ${breakEvenText} ` +
    `Use the left and right arrow keys to read profit and probability at a price.`;

  function onKey(e: React.KeyboardEvent<SVGSVGElement>) {
    const step = (model!.hi - model!.lo) / 100;
    const cur = hoverPrice ?? model!.spot;
    let next: number | null = null;
    if (e.key === 'ArrowRight') next = cur + step * (e.shiftKey ? 10 : 1);
    else if (e.key === 'ArrowLeft') next = cur - step * (e.shiftKey ? 10 : 1);
    else if (e.key === 'Home') next = model!.lo;
    else if (e.key === 'End') next = model!.hi;
    else if (e.key === 'Escape') { setHoverX(null); setKeyboardMoved(false); return; }
    if (next === null) return;
    e.preventDefault();
    next = Math.min(model!.hi, Math.max(model!.lo, next));
    setHoverX(PAD.l + ((next - model!.lo) / (model!.hi - model!.lo)) * plotW);
    setKeyboardMoved(true);
  }

  const pointer = (e: React.PointerEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    setHoverX(((e.clientX - rect.left) / rect.width) * W);
    setKeyboardMoved(false);
  };

  return shell(
    <>
      {/* The chart's host. It owns the measured size; the SVG is absolutely
          positioned inside it so the panel can shrink the chart without the
          SVG's own height holding the layout open. */}
      <div ref={hostRef} style={{ position: 'relative', flex: 1, minHeight: 170 }}>
        <svg
          viewBox={`0 0 ${W} ${H}`}
          width={W}
          height={H}
          role="img"
          aria-label={summary}
          tabIndex={0}
          style={{ position: 'absolute', inset: 0, display: 'block', overflow: 'visible', touchAction: 'pan-y' }}
          onPointerMove={pointer}
          onPointerDown={pointer}
          onPointerLeave={() => { if (!keyboardMoved) setHoverX(null); }}
          onKeyDown={onKey}
          onBlur={() => { setKeyboardMoved(false); setHoverX(null); }}
        >
          <defs>
            <linearGradient id="pdProfit" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="var(--color-profit)" stopOpacity="0.55" />
              <stop offset="100%" stopColor="var(--color-profit)" stopOpacity="0.06" />
            </linearGradient>
            <linearGradient id="pdLoss" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="var(--color-loss)" stopOpacity="0.45" />
              <stop offset="100%" stopColor="var(--color-loss)" stopOpacity="0.05" />
            </linearGradient>
          </defs>

          {/* ±2σ then ±1σ bands, painted before everything so they sit behind */}
          <rect
            x={model.x(model.twoSigma.lo)}
            y={PAD.t}
            width={Math.max(0, model.x(model.twoSigma.hi) - model.x(model.twoSigma.lo))}
            height={H - PAD.t - PAD.b}
            fill="var(--color-accent)"
            opacity="0.045"
          />
          <rect
            x={model.x(model.oneSigma.lo)}
            y={PAD.t}
            width={Math.max(0, model.x(model.oneSigma.hi) - model.x(model.oneSigma.lo))}
            height={H - PAD.t - PAD.b}
            fill="var(--color-accent)"
            opacity="0.055"
          />

          {/* Baseline */}
          <line
            x1={PAD.l} y1={H - PAD.b} x2={W - PAD.r} y2={H - PAD.b}
            stroke="var(--color-line)" strokeWidth="1"
          />

          {/* Density, split by the sign of the payoff */}
          {model.regions.map((r, i) => (
            <path
              key={i}
              d={r.d}
              fill={r.profitable ? 'url(#pdProfit)' : 'url(#pdLoss)'}
              className="animate-fade"
              style={{ animationDelay: `${0.05 + i * 0.04}s` }}
            />
          ))}

          {/* Density outline */}
          <path
            d={model.densityLine}
            fill="none"
            stroke="var(--color-violet)"
            strokeWidth="1.5"
            strokeLinejoin="round"
          />

          {/* Payoff zero line, and the scale beside it */}
          <line
            x1={PAD.l} y1={model.yPnl(0)} x2={W - PAD.r} y2={model.yPnl(0)}
            stroke="var(--color-line-strong)" strokeWidth="1" strokeDasharray="3 4"
          />
          {pnlTicks.map((v) => (
            <text
              key={v}
              x={PAD.l - 6}
              y={model.yPnl(v) + 4}
              textAnchor="end"
              fontSize={FONT}
              fontFamily="var(--font-mono)"
              fill={v === 0 ? 'var(--color-ink-300)' : v > 0 ? 'var(--color-profit)' : 'var(--color-loss)'}
            >
              {v === 0 ? '$0' : `${v > 0 ? '+' : '−'}${money(Math.abs(v)).replace('-', '')}`}
            </text>
          ))}

          {/* Engine payoff curve */}
          <path
            d={model.pnlLine}
            fill="none"
            stroke="var(--color-accent)"
            strokeWidth="2"
            strokeLinejoin="round"
          />
          {/* Break-evens. The label sits clear of the dashed line, to its right. */}
          {model.breakEvens.map((b, i) => (
            <g key={i} className="animate-fade" style={{ animationDelay: '0.5s' }}>
              <line
                x1={model.x(b)} y1={PAD.t} x2={model.x(b)} y2={H - PAD.b}
                stroke="var(--color-warn)" strokeWidth="1" strokeDasharray="2 3"
              />
              <text
                x={model.x(b) + 4} y={PAD.t + 10}
                fill="var(--color-warn)" fontSize={FONT} textAnchor="start"
                fontFamily="var(--font-mono)"
              >
                BE {formatAmount(b, 2)}
              </text>
            </g>
          ))}

          {/* Spot */}
          <line
            x1={model.x(model.spot)} y1={PAD.t} x2={model.x(model.spot)} y2={H - PAD.b}
            stroke="var(--color-ink-100)" strokeWidth="1" opacity="0.55"
          />

          {/* Crosshair */}
          {hoverX !== null && hoverPrice !== null && (
            <line
              x1={hoverX} y1={PAD.t} x2={hoverX} y2={H - PAD.b}
              stroke="var(--color-ink-300)" strokeWidth="1" strokeDasharray="2 2"
            />
          )}

          {/* Price axis */}
          {axisTicks.map((t, i) => (
            <text
              key={i}
              x={model.x(t)}
              y={H - PAD.b + 16}
              fill="var(--color-ink-300)"
              fontSize={FONT}
              fontFamily="var(--font-mono)"
              textAnchor={i === 0 ? 'start' : i === axisTicks.length - 1 ? 'end' : 'middle'}
            >
              {formatAmount(t, t >= 1000 ? 0 : 2)}
            </text>
          ))}
        </svg>
      </div>

      {/* Readout -- outside the SVG so it uses real text rendering. Announced
          only once the crosshair has been driven from the keyboard. */}
      <div
        aria-live={keyboardMoved ? 'polite' : 'off'}
        style={{
          display: 'flex',
          gap: '0.75rem',
          justifyContent: 'center',
          flexWrap: 'wrap',
          minHeight: '1.1rem',
          fontSize: 'var(--text-2xs)',
          color: 'var(--color-ink-300)',
          fontFamily: 'var(--font-mono)',
          flex: 'none',
        }}
      >
        {hoverPrice !== null ? (
          <>
            <span>{symbol} {formatAmount(hoverPrice, 2)}</span>
            <span>
              P&amp;L{' '}
              <span className={(hoverPnl ?? 0) >= 0 ? 'profit' : 'loss'}>
                {hoverPnl === null ? '—' : money(hoverPnl)}
              </span>
            </span>
            <span>P(≤) {(model.cdf(hoverPrice) * 100).toFixed(1)}%</span>
          </>
        ) : (
          <span style={{ color: 'var(--color-ink-300)' }}>
            Hover, touch or press ← → to read P&amp;L at a price
          </span>
        )}
      </div>
      {/* Said in words, not drawn on the chart: an open-ended payoff is cut at
          the edge of the range, and the number at that edge is not a maximum. */}
      {(profitUnbounded || lossUnbounded) && (
        <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)', flex: 'none' }}>
          {profitUnbounded && <span className="profit">↗ Profit has no ceiling. </span>}
          {lossUnbounded && <span className="loss">↘ Loss has no floor. </span>}
          The line is cut at the edge of the range drawn.
        </div>
      )}

      {/* The ranges the distribution implies. Probability of profit and expected
          value are in the results strip above, once, rather than here a second
          time: this block took 70px the chart needed, and the panel clipped it
          through the numerals. One line, natural height, never cut. */}
      <div
        style={{
          display: 'flex',
          flexWrap: 'wrap',
          gap: '0.25rem 1.25rem',
          marginTop: '0.375rem',
          paddingTop: '0.375rem',
          borderTop: '1px solid var(--color-line)',
          fontSize: 'var(--text-2xs)',
          color: 'var(--color-ink-300)',
          flex: 'none',
        }}
      >
        <span>
          1σ range (68%){' '}
          <span className="num" style={{ color: 'var(--color-ink-100)' }}>
            {formatAmount(model.oneSigma.lo, 2)} – {formatAmount(model.oneSigma.hi, 2)}
          </span>
        </span>
        <span>
          2σ range (95%){' '}
          <span className="num" style={{ color: 'var(--color-ink-100)' }}>
            {formatAmount(model.twoSigma.lo, 2)} – {formatAmount(model.twoSigma.hi, 2)}
          </span>
        </span>
        <span>
          Implied vol{' '}
          <span className="num" style={{ color: 'var(--color-ink-100)' }}>
            {(model.iv * 100).toFixed(1)}% · {model.days}d
          </span>
          {!popIsEngine && <span style={{ color: 'var(--color-warn)' }}> · odds modelled</span>}
        </span>
      </div>
    </>,
    <>
      <span className="chip chip-accent" title="The payoff line comes from the pricing engine.">payoff</span>
      <span className="chip chip-modelled" title="The shaded density is modelled: a lognormal from the live spot, the chain's implied volatility and the days to expiry.">
        modelled odds
      </span>
    </>,
  );
}

export default ProbabilityCurve;
