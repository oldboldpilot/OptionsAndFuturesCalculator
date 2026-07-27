'use client';

import { useMemo, useState } from 'react';
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

const W = 760;
const H = 268;
const PAD = { l: 6, r: 6, t: 14, b: 30 };

const money = (v: number) =>
  `${v < 0 ? '−' : ''}$${Math.abs(v).toLocaleString(undefined, {
    maximumFractionDigits: Math.abs(v) >= 100 ? 0 : 2,
  })}`;

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
  const { result, error, isLoading, symbol } = useCalculatorStore();
  const [hoverX, setHoverX] = useState<number | null>(null);

  const model = useMemo(() => {
    if (!result) return null;
    const { spot, impliedVolatility: iv, days, riskFreeRate: r } = result.inputs;
    const curve = result.expiryCurve;
    if (spot <= 0 || iv <= 0 || days <= 0 || curve.length < 2) return null;

    const T = days / 365;
    const sd = iv * Math.sqrt(T);
    const mu = Math.log(spot) + (r - 0.5 * iv * iv) * T;

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
  }, [result]);

  const shell = (body: React.ReactNode, chips?: React.ReactNode) => (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">Probability Distribution</span>
          {chips}
        </div>
      </div>
      <div className="panel-body" style={{ flex: 1, display: 'flex', flexDirection: 'column' }}>
        {body}
      </div>
    </div>
  );

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
  const ev = result?.expected_value ?? 0;

  const hoverPrice =
    hoverX === null
      ? null
      : model.lo + ((hoverX - PAD.l) / (W - PAD.l - PAD.r)) * (model.hi - model.lo);
  const hoverPnl =
    hoverPrice === null ? null : pnlAt(result!.expiryCurve, hoverPrice) ?? null;

  const axisTicks = Array.from({ length: 7 }, (_, i) => model.lo + ((model.hi - model.lo) * i) / 6);

  return shell(
    <>
      <svg
        viewBox={`0 0 ${W} ${H}`}
        preserveAspectRatio="none"
        style={{ width: '100%', height: '100%', minHeight: 170, display: 'block', overflow: 'visible' }}
        onMouseMove={(e) => {
          const rect = e.currentTarget.getBoundingClientRect();
          setHoverX(((e.clientX - rect.left) / rect.width) * W);
        }}
        onMouseLeave={() => setHoverX(null)}
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
          className="draw-path"
          style={{ ['--dash' as string]: '2600' }}
          vectorEffect="non-scaling-stroke"
        />

        {/* Payoff zero line */}
        <line
          x1={PAD.l} y1={model.yPnl(0)} x2={W - PAD.r} y2={model.yPnl(0)}
          stroke="var(--color-line-strong)" strokeWidth="1" strokeDasharray="3 4"
        />

        {/* Engine payoff curve */}
        <path
          d={model.pnlLine}
          fill="none"
          stroke="var(--color-accent)"
          strokeWidth="2"
          strokeLinejoin="round"
          className="draw-path"
          style={{ ['--dash' as string]: '2600', animationDelay: '0.15s' }}
          vectorEffect="non-scaling-stroke"
        />

        {/* Break-evens */}
        {model.breakEvens.map((b, i) => (
          <g key={i} className="animate-fade" style={{ animationDelay: '0.5s' }}>
            <line
              x1={model.x(b)} y1={PAD.t} x2={model.x(b)} y2={H - PAD.b}
              stroke="var(--color-warn)" strokeWidth="1" strokeDasharray="2 3"
              vectorEffect="non-scaling-stroke"
            />
            <text
              x={model.x(b)} y={PAD.t + 9}
              fill="var(--color-warn)" fontSize="10" textAnchor="middle"
              fontFamily="var(--font-mono)"
            >
              BE {b.toFixed(2)}
            </text>
          </g>
        ))}

        {/* Spot */}
        <line
          x1={model.x(model.spot)} y1={PAD.t} x2={model.x(model.spot)} y2={H - PAD.b}
          stroke="var(--color-ink-100)" strokeWidth="1" opacity="0.55"
          vectorEffect="non-scaling-stroke"
        />

        {/* Crosshair */}
        {hoverX !== null && hoverPrice !== null && (
          <line
            x1={hoverX} y1={PAD.t} x2={hoverX} y2={H - PAD.b}
            stroke="var(--color-ink-300)" strokeWidth="1" strokeDasharray="2 2"
            vectorEffect="non-scaling-stroke"
          />
        )}

        {/* Price axis */}
        {axisTicks.map((t, i) => (
          <text
            key={i}
            x={model.x(t)}
            y={H - PAD.b + 15}
            fill="var(--color-ink-400)"
            fontSize="10"
            fontFamily="var(--font-mono)"
            textAnchor={i === 0 ? 'start' : i === axisTicks.length - 1 ? 'end' : 'middle'}
          >
            {t.toFixed(t >= 1000 ? 0 : 2)}
          </text>
        ))}
      </svg>

      {/* Hover readout — sits outside the SVG so it uses real text rendering */}
      <div
        style={{
          display: 'flex',
          gap: '0.75rem',
          justifyContent: 'center',
          minHeight: '1.1rem',
          fontSize: 'var(--text-2xs)',
          color: 'var(--color-ink-300)',
          fontFamily: 'var(--font-mono)',
        }}
      >
        {hoverPrice !== null ? (
          <>
            <span>{symbol} {hoverPrice.toFixed(2)}</span>
            <span>
              P&amp;L{' '}
              <span className={(hoverPnl ?? 0) >= 0 ? 'profit' : 'loss'}>
                {hoverPnl === null ? '—' : money(hoverPnl)}
              </span>
            </span>
            <span>P(≤) {(model.cdf(hoverPrice) * 100).toFixed(1)}%</span>
          </>
        ) : (
          <span style={{ color: 'var(--color-ink-400)' }}>
            Hover the chart to read probability and P&amp;L at a price
          </span>
        )}
      </div>

      {/* Probability readouts */}
      <div
        style={{
          display: 'grid',
          gridTemplateColumns: 'repeat(auto-fit, minmax(118px, 1fr))',
          gap: '0.5rem 0.875rem',
          marginTop: '0.5rem',
          paddingTop: '0.5rem',
          borderTop: '1px solid var(--color-line)',
        }}
      >
        <div>
          <div className="stat-label">
            Probability of profit {!popIsEngine && <span style={{ color: 'var(--color-warn)' }}>·model</span>}
          </div>
          <div className="stat-value" style={{ fontSize: 'var(--text-base)' }}>
            {(popValue * 100).toFixed(1)}%
          </div>
          <div className="meter" style={{ marginTop: '0.25rem' }}>
            <div
              className="meter-fill"
              style={{
                width: `${Math.min(100, popValue * 100)}%`,
                background: 'var(--color-profit)',
              }}
            />
          </div>
        </div>

        <div>
          <div className="stat-label">Expected value</div>
          <div className={`stat-value ${ev >= 0 ? 'profit' : 'loss'}`} style={{ fontSize: 'var(--text-base)' }}>
            {money(ev)}
          </div>
        </div>

        <div>
          <div className="stat-label">1σ range (68%)</div>
          <div className="stat-value">
            {model.oneSigma.lo.toFixed(2)} – {model.oneSigma.hi.toFixed(2)}
          </div>
        </div>

        <div>
          <div className="stat-label">2σ range (95%)</div>
          <div className="stat-value">
            {model.twoSigma.lo.toFixed(2)} – {model.twoSigma.hi.toFixed(2)}
          </div>
        </div>

        <div>
          <div className="stat-label">Implied vol · horizon</div>
          <div className="stat-value">
            {(model.iv * 100).toFixed(1)}% · {model.days}d
          </div>
        </div>
      </div>
    </>,
    <>
      <span className="chip chip-accent">payoff · engine</span>
      <span className="chip chip-modelled">density · lognormal</span>
    </>,
  );
}

export default ProbabilityCurve;
