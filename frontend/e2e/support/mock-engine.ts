/**
 * A deterministic stand-in for the calculator engine, for layout and flow tests.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The browser specs in this folder that need a PRICED position cannot rely on
 * the live engine: its chain depends on whether the US session is open, its
 * entitlement gate depends on the caller, and a layout assertion that goes red
 * because a market holiday returned no strikes is a measurement of the calendar
 * and not of the page.
 *
 * This is test infrastructure, not a second implementation of the engine. It
 * exists to put plausible, internally consistent numbers on the screen so that
 * what is asserted is WHERE they are rendered. It mirrors the engine's wire
 * contract (field for field) and its one non-obvious rule: P&L extremes are the
 * true extremes over S in (0, infinity), and a direction with unbounded slope
 * is flagged rather than reported as the edge of the grid. The engine's own
 * tests (backend/tests/test_calculator_service.cpp) are what prove the real
 * thing; nothing here is evidence about it.
 */
import * as pb from '../../src/grpc/calculator_pb';

const DAY_MS = 86_400_000;

/** Standard normal CDF, Abramowitz and Stegun 7.1.26. */
function ncdf(x: number): number {
  const s = x < 0 ? -1 : 1;
  const a = Math.abs(x) / Math.SQRT2;
  const t = 1 / (1 + 0.3275911 * a);
  const y =
    1 -
    ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t + 0.254829592) *
      t *
      Math.exp(-a * a);
  return 0.5 * (1 + s * y);
}
const npdf = (x: number) => Math.exp(-0.5 * x * x) / Math.sqrt(2 * Math.PI);

export interface Bs {
  value: number;
  delta: number;
  gamma: number;
  theta: number; // per year
  vega: number; // per 1.00 of vol
}

export function blackScholes(
  s: number, k: number, r: number, sigma: number, t: number, call: boolean,
): Bs {
  if (t <= 0 || sigma <= 0 || s <= 0) {
    const intrinsic = call ? Math.max(0, s - k) : Math.max(0, k - s);
    return { value: intrinsic, delta: 0, gamma: 0, theta: 0, vega: 0 };
  }
  const sq = sigma * Math.sqrt(t);
  const d1 = (Math.log(s / k) + (r + 0.5 * sigma * sigma) * t) / sq;
  const d2 = d1 - sq;
  const df = Math.exp(-r * t);
  const value = call
    ? s * ncdf(d1) - k * df * ncdf(d2)
    : k * df * ncdf(-d2) - s * ncdf(-d1);
  const delta = call ? ncdf(d1) : ncdf(d1) - 1;
  const gamma = npdf(d1) / (s * sq);
  const vega = s * npdf(d1) * Math.sqrt(t);
  const theta = -(s * npdf(d1) * sigma) / (2 * Math.sqrt(t)) -
    (call ? 1 : -1) * r * k * df * ncdf(call ? d2 : -d2);
  return { value, delta, gamma, theta, vega };
}

const isoDate = (ms: number) => new Date(ms).toISOString().slice(0, 10);

/* ----------------------------------------------------------------------- */
/* Market data                                                              */
/* ----------------------------------------------------------------------- */

export interface Instrument {
  symbol: string;
  assetClass: 'EQUITY' | 'FUTURES' | 'CRYPTO';
  spot: number;
  iv: number;
}

export const SPY: Instrument = { symbol: 'SPY', assetClass: 'EQUITY', spot: 779.09, iv: 0.105 };
export const NVDA: Instrument = { symbol: 'NVDA', assetClass: 'EQUITY', spot: 239.86, iv: 0.42 };
export const ES: Instrument = { symbol: 'ES', assetClass: 'FUTURES', spot: 7790.9, iv: 0.12 };

export const INSTRUMENTS: Record<string, Instrument> = { SPY, NVDA, ES };

/** Listed expirations relative to `now`, so DTE is always consistent with the date. */
export function expirations(now: number): { date: string; dte: number }[] {
  return [7, 10, 14, 21, 35, 70, 105, 189].map((dte) => ({
    date: isoDate(now + dte * DAY_MS),
    dte,
  }));
}

export function chainResponse(inst: Instrument, now: number, expiration?: string): pb.ChainResponse {
  const res = new pb.ChainResponse();
  res.setSymbol(inst.symbol);
  res.setSpotPrice(inst.spot);
  res.setProvider('alpaca');
  res.setFetchedAt(new Date(now).toISOString().replace(/\.\d+Z$/, 'Z'));

  if (inst.assetClass === 'FUTURES') {
    [72, 163].forEach((dte, i) => {
      const c = new pb.FuturesContract();
      c.setCode(`${inst.symbol}${i === 0 ? 'Z26' : 'H27'}`);
      c.setDeliveryMonth(i === 0 ? '2026-12' : '2027-03');
      c.setDaysToExpiry(dte);
      c.setFuturesPrice(inst.spot * Math.exp(0.04 * (dte / 365)));
      c.setBasis(inst.spot * (Math.exp(0.04 * (dte / 365)) - 1));
      c.setAnnualizedYield(0.04);
      c.setState('MODELLED');
      res.addFuturesContracts(c);
    });
    return res;
  }

  const exps = expirations(now);
  exps.forEach((e) => {
    const x = new pb.ExpirationDate();
    x.setDateStr(e.date);
    x.setDaysToExpiry(e.dte);
    x.setLabel(`${e.date} (${e.dte}d)`);
    res.addAvailableExpirations(x);
  });
  const chosen = exps.find((e) => e.date === expiration) ?? exps[0];
  res.setSelectedExpirationDate(chosen.date);

  const step = inst.spot > 400 ? 2.5 : 1;
  const atm = Math.round(inst.spot / step) * step;
  for (let i = -60; i <= 60; i++) {
    const k = +(atm + i * step).toFixed(2);
    if (k <= 0) continue;
    const t = chosen.dte / 365;
    const c = blackScholes(inst.spot, k, 0.038, inst.iv, t, true);
    const p = blackScholes(inst.spot, k, 0.038, inst.iv, t, false);
    const row = new pb.OptionStrike();
    row.setStrike(k);
    row.setIsAtm(i === 0);
    row.setCallBid(+(c.value * 0.99).toFixed(2));
    row.setCallAsk(+(c.value * 1.01 + 0.01).toFixed(2));
    row.setCallDelta(+c.delta.toFixed(3));
    row.setCallIv(inst.iv);
    row.setCallVolume(120 + (i * 7 > 0 ? i * 7 : -i * 3));
    row.setCallOpenInterest(2400 + Math.abs(i) * 13);
    row.setPutBid(+(p.value * 0.99).toFixed(2));
    row.setPutAsk(+(p.value * 1.01 + 0.01).toFixed(2));
    row.setPutDelta(+p.delta.toFixed(3));
    row.setPutIv(inst.iv);
    row.setPutVolume(90 + Math.abs(i) * 4);
    row.setPutOpenInterest(1900 + Math.abs(i) * 11);
    res.addOptionStrikes(row);
  }
  return res;
}

export function quoteResponse(inst: Instrument, now: number): pb.QuoteResponse {
  const q = new pb.QuoteResponse();
  q.setSymbol(inst.symbol);
  q.setPrice(inst.spot);
  q.setPreviousClose(inst.spot * 0.996);
  q.setAssetClass(inst.assetClass);
  q.setProvider('alpaca');
  q.setQuoteTimestamp(new Date(now).toISOString());
  return q;
}

export function rateResponse(now: number): pb.RiskFreeRateResponse {
  const r = new pb.RiskFreeRateResponse();
  r.setRate(0.038625);
  r.setRatePublished(0.0390);
  r.setTenor('3M');
  r.setAsOfDate(isoDate(now));
  r.setSource('us_treasury_par_yield');
  r.setFetchedAt(new Date(now).toISOString());
  return r;
}

/* ----------------------------------------------------------------------- */
/* Strategy pricing                                                         */
/* ----------------------------------------------------------------------- */

interface LegView {
  dir: number;
  type: 'CALL' | 'PUT' | 'FUTURE' | 'STOCK';
  strike: number;
  days: number;
  qty: number;
  premium: number;
  iv: number;
  mult: number;
}

const LEG_TYPES = ['CALL', 'PUT', 'FUTURE', 'STOCK'] as const;

function legsOf(req: pb.StrategyRequest): LegView[] {
  return req.getLegsList().map((l) => {
    const type = LEG_TYPES[l.getType()] ?? 'STOCK';
    const isOpt = type === 'CALL' || type === 'PUT';
    return {
      dir: l.getAction() === pb.Leg.Action.BUY ? 1 : -1,
      type,
      strike: l.getStrike(),
      days: l.getExpirationDays(),
      qty: l.getQuantity() > 0 ? l.getQuantity() : 1,
      premium: l.getPremium(),
      iv: l.getImpliedVolatility(),
      mult: l.getContractMultiplier() > 0 ? l.getContractMultiplier() : isOpt ? 100 : 1,
    };
  });
}

/** Net slope of the payoff as S -> infinity, in units of underlying per contract. */
export function upsideSlope(legs: Pick<LegView, 'dir' | 'type' | 'qty' | 'mult'>[]): number {
  return legs.reduce(
    (s, l) => s + (l.type === 'PUT' ? 0 : l.dir * l.qty * l.mult),
    0,
  );
}

export function priceStrategy(req: pb.StrategyRequest, now: number): pb.StrategyResponse {
  const legs = legsOf(req);
  const spot = req.getCurrentPrice();
  const r = req.getRiskFreeRate();
  const ivs = legs.filter((l) => l.iv > 0);
  const sigma = ivs.length
    ? ivs.reduce((a, l) => a + l.iv * l.qty, 0) / ivs.reduce((a, l) => a + l.qty, 0)
    : req.getImpliedVolatility();
  const days = legs.map((l) => l.days).filter((d) => d > 0);
  const horizon = Math.max(0, ...days);
  const curve = days.length ? Math.min(...days) : horizon;

  const value = (s: number, elapsed: number) =>
    legs.reduce((sum, l) => {
      if (l.type === 'FUTURE' || l.type === 'STOCK') {
        return sum + (s - (l.premium > 0 ? l.premium : l.strike)) * l.dir * l.mult * l.qty;
      }
      const remaining = (l.days > 0 ? l.days : horizon) - elapsed;
      const iv = l.iv > 0 ? l.iv : sigma;
      const v = blackScholes(s, l.strike, r, iv, remaining / 365, l.type === 'CALL').value;
      return sum + (v - l.premium) * l.dir * l.mult * l.qty;
    }, 0);

  const res = new pb.StrategyResponse();
  const steps = 61;
  const lo = Math.max(0.01, spot * 0.75);
  const hi = spot * 1.25;
  const grid = Array.from({ length: steps }, (_, i) => lo + ((hi - lo) * i) / (steps - 1));
  const pnl = grid.map((s) => value(s, curve));

  grid.forEach((s, i) => {
    const p = new pb.PnLPoint();
    p.setUnderlyingPrice(s);
    p.setPnl(pnl[i]);
    res.addPnlMatrix(p);
  });

  // True extremes: the grid, every strike, the S -> 0 limit and a far point.
  const candidates = [...grid, ...legs.map((l) => l.strike).filter((k) => k > 0), 0.01, spot * 40];
  const all = candidates.map((s) => value(s, curve));
  const slope = upsideSlope(legs);
  const gridMax = Math.max(...pnl);
  const gridMin = Math.min(...pnl);
  const maxProfit = slope > 0 ? gridMax : Math.max(...all);
  const maxLoss = slope < 0 ? gridMin : Math.min(...all);
  res.setMaxProfit(maxProfit);
  res.setMaxLoss(maxLoss);
  const unboundedProfit = slope > 0;
  const unboundedLoss = slope < 0;
  // Fields added with OFC-011. Optional-called so this mock still loads against
  // an older generated stub, which is how the RED run is made.
  const anyRes = res as unknown as Record<string, ((v: boolean) => void) | undefined>;
  anyRes.setMaxProfitUnbounded?.(unboundedProfit);
  anyRes.setMaxLossUnbounded?.(unboundedLoss);
  if (maxLoss < 0 && !unboundedProfit && !unboundedLoss) {
    res.setRiskRewardRatio(Math.abs(maxProfit / maxLoss));
  }

  const be: number[] = [];
  for (let i = 1; i < grid.length; i++) {
    const a = pnl[i - 1];
    const b = pnl[i];
    if (a === 0 || a * b < 0) {
      const t = Math.abs(a) / (Math.abs(a) + Math.abs(b));
      be.push(grid[i - 1] + t * (grid[i] - grid[i - 1]));
    }
  }
  be.forEach((b) => res.addBreakevenPrices(b));
  if (be.length) res.setBreakEven(be[0]);
  res.setCurveDaysToExpiration(curve);

  // Probability on the grid, lognormal at the curve date.
  if (sigma > 0 && curve > 0) {
    const T = curve / 365;
    const sd = sigma * Math.sqrt(T);
    const mu = Math.log(spot) + (r - 0.5 * sigma * sigma) * T;
    const dens = grid.map((s) => {
      const z = (Math.log(s) - mu) / sd;
      return Math.exp(-0.5 * z * z) / (s * sd * Math.sqrt(2 * Math.PI));
    });
    const total = dens.reduce((a, b) => a + b, 0) || 1;
    const w = dens.map((d) => d / total);
    res.setPop(w.reduce((a, d, i) => (pnl[i] > 0 ? a + d : a), 0));
    res.setExpectedValue(w.reduce((a, d, i) => a + d * pnl[i], 0));
    const rm = new pb.RiskMetrics();
    rm.setVarParametric95(-1.645 * sd * spot);
    rm.setVarParametric99(-2.326 * sd * spot);
    rm.setCvarParametric95(-2.063 * sd * spot);
    rm.setCvarParametric99(-2.665 * sd * spot);
    res.setRiskMetrics(rm);
  }

  // Greeks, summed over option legs.
  const net = new pb.Greeks();
  let delta = 0, gamma = 0, theta = 0, vega = 0;
  legs.forEach((l, idx) => {
    const scale = l.dir * l.mult * l.qty;
    const lr = new pb.LegRisk();
    lr.setLegIndex(idx);
    const g = new pb.Greeks();
    if (l.type === 'CALL' || l.type === 'PUT') {
      const iv = l.iv > 0 ? l.iv : sigma;
      const b = blackScholes(spot, l.strike, r, iv, (l.days > 0 ? l.days : horizon) / 365, l.type === 'CALL');
      delta += b.delta * scale; gamma += b.gamma * scale;
      theta += (b.theta / 365) * scale; vega += (b.vega / 100) * scale;
      g.setDelta(b.delta * scale); g.setGamma(b.gamma * scale);
      g.setTheta((b.theta / 365) * scale); g.setVega((b.vega / 100) * scale);
      lr.setModelPrice(b.value);
      lr.setOpenPnl((b.value - l.premium) * scale);
    } else {
      delta += scale;
      g.setDelta(scale);
      lr.setOpenPnl((spot - (l.premium > 0 ? l.premium : l.strike)) * scale);
    }
    lr.setGreeks(g);
    res.addLegRisk(lr);
  });
  net.setDelta(delta); net.setGamma(gamma); net.setTheta(theta); net.setVega(vega);
  res.setNetGreeks(net);

  // Price x date matrix. Dates are derived from the SAME clock as the days.
  const dateSteps = 8;
  const mlo = req.getMatrixPriceMin() > 0 ? req.getMatrixPriceMin() : lo;
  const mhi = req.getMatrixPriceMax() > 0 ? req.getMatrixPriceMax() : hi;
  const rows = 21;
  const risk = Math.abs(maxLoss);
  for (let d = 0; d < dateSteps; d++) {
    const frac = d / (dateSteps - 1);
    const dte = horizon * (1 - frac);
    const elapsed = Math.max(0, horizon - dte);
    const date = isoDate(now + Math.round(elapsed) * DAY_MS);
    for (let p = 0; p < rows; p++) {
      const price = mlo + ((mhi - mlo) * p) / (rows - 1);
      const pnlCell = value(price, elapsed);
      const cell = new pb.MatrixCell();
      cell.setPrice(price);
      cell.setDaysToExpiration(Math.round(dte));
      cell.setDateStr(date);
      cell.setPnlDollars(pnlCell);
      if (risk > 1e-9) cell.setReturnOnRiskPercent((pnlCell / risk) * 100);
      res.addMatrix(cell);
    }
  }
  return res;
}
