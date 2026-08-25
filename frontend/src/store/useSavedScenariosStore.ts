import { create } from 'zustand';
import { authMetadata } from '../lib/licence';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import {
  DeleteStrategyRequest,
  ListStrategiesRequest,
  Leg as ProtoLeg,
  SaveStrategyRequest,
  SavedStrategy,
} from '../grpc/calculator_pb';
import { buildStrategyRequest, useCalculatorStore, type Leg } from './useCalculatorStore';
import type { AsianStyle } from './useTreePricerStore';

/**
 * Saved scenarios: a Pro user's named positions, stored server-side.
 *
 * Kept in its own store rather than folded into `useCalculatorStore` for one
 * concrete reason: that store's `isLoading` is read by five analytics panels as
 * "the payoff is being recalculated". Reusing it for a save would blank all
 * five behind a spinner while a database write ran. That exact defect is why
 * the previous `saveStrategy`/`loadStrategies` pair was deleted rather than
 * fixed -- see useCalculatorStore's own note that `isLoading` now has exactly
 * one writer. This store keeps its own status so that stays true.
 */

/** One saved scenario, flattened out of the wire message. */
export interface SavedScenario {
  id: string;
  name: string;
  symbol: string;
  /** RFC3339 UTC, exactly as the server recorded it. */
  updatedAt: string;
  spotPrice: number;
  dividendYield: number;
  legs: Leg[];
}

export type SavedStatus = 'idle' | 'loading' | 'ready' | 'error';

/**
 * Why a call failed, in a form the UI can act on.
 *
 * `needsSignIn` and `needsPro` are separated because they lead to different
 * buttons -- one opens sign-in, the other opens checkout -- and because the
 * backend already distinguishes them by gRPC STATUS CODE. Matching on code
 * rather than on message text is the same rule `useCalculatorStore` follows
 * for its own Pro gate, and for the same reason: the copy was reworded twice
 * in a single day, and a store matching on text would have broken silently.
 */
export interface SavedFailure {
  message: string;
  needsSignIn: boolean;
  needsPro: boolean;
}

interface SavedScenariosStore {
  scenarios: SavedScenario[];
  status: SavedStatus;
  failure: SavedFailure | null;
  /** Set briefly after a successful save, for the confirmation line. */
  lastSavedName: string | null;

  refresh: () => Promise<void>;
  save: (name: string) => Promise<boolean>;
  remove: (id: string) => Promise<boolean>;
  apply: (scenario: SavedScenario) => void;
  clearFailure: () => void;
}

const UNAUTHENTICATED = 16;
const PERMISSION_DENIED = 7;

function backendUrl(): string {
  return process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
}

/**
 * Maps a gRPC error onto a failure the UI can act on.
 *
 * `code` is the discriminator, never `message`. An unknown code keeps the
 * server's own sentence rather than inventing one -- the engine is the side
 * that knows why it refused.
 */
function toFailure(err: { code?: number; message?: string } | null): SavedFailure {
  const code = err?.code ?? 0;
  const message = err?.message || 'Saved scenarios are unavailable right now.';
  return {
    message,
    needsSignIn: code === UNAUTHENTICATED,
    needsPro: code === PERMISSION_DENIED,
  };
}

/** Wire leg -> store leg. The exact inverse of buildStrategyRequest's mapping. */
function fromProtoLeg(p: ProtoLeg, index: number): Leg {
  const TYPE: Record<number, string> = {
    [ProtoLeg.Type.CALL]: 'CALL',
    [ProtoLeg.Type.PUT]: 'PUT',
    [ProtoLeg.Type.FUTURE]: 'FUTURE',
    [ProtoLeg.Type.STOCK]: 'STOCK',
  };
  const ASIAN: Record<number, AsianStyle> = {
    [ProtoLeg.AsianType.NOT_ASIAN]: 'NOT_ASIAN',
    [ProtoLeg.AsianType.AVERAGE_PRICE]: 'AVERAGE_PRICE',
    [ProtoLeg.AsianType.AVERAGE_STRIKE]: 'AVERAGE_STRIKE',
  };
  const optionType = TYPE[p.getType()] ?? 'STOCK';
  return {
    // Replaced by the calculator store's own counter on add; present because
    // `Leg` requires it. Never persisted -- a leg id is only meaningful for
    // the lifetime of one tab.
    id: `saved-${index}`,
    instrument_type: optionType === 'CALL' || optionType === 'PUT' ? 'OPTION' : optionType,
    action: p.getAction() === ProtoLeg.Action.BUY ? 'BUY' : 'SELL',
    option_type: optionType,
    strike_price: p.getStrike(),
    premium: p.getPremium(),
    quantity: p.getQuantity(),
    expiration_days: p.getExpirationDays(),
    implied_volatility: p.getImpliedVolatility(),
    asian_type: ASIAN[p.getAsianType()] ?? 'NOT_ASIAN',
  };
}

function fromWire(s: SavedStrategy): SavedScenario {
  const req = s.getRequest();
  return {
    id: s.getId(),
    name: s.getName(),
    symbol: req?.getUnderlyingSymbol() ?? '',
    updatedAt: s.getUpdatedAt(),
    spotPrice: req?.getCurrentPrice() ?? 0,
    dividendYield: req?.getDividendYield() ?? 0,
    legs: (req?.getLegsList() ?? []).map(fromProtoLeg),
  };
}

export const useSavedScenariosStore = create<SavedScenariosStore>((set, get) => ({
  scenarios: [],
  status: 'idle',
  failure: null,
  lastSavedName: null,

  clearFailure: () => set({ failure: null }),

  refresh: async () => {
    set({ status: 'loading', failure: null });
    try {
      const client = new OptionsCalculatorClient(backendUrl());
      const res = await client.listStrategies(new ListStrategiesRequest(), authMetadata());
      set({
        scenarios: res.getStrategiesList().map(fromWire),
        status: 'ready',
        failure: null,
      });
    } catch (err) {
      // An unauthenticated or free-tier caller is the ORDINARY case here, not
      // an error state: the panel simply shows its upgrade prompt. The
      // scenario list is emptied so a previous user's names cannot linger on
      // screen after a sign-out.
      set({ scenarios: [], status: 'error', failure: toFailure(err as never) });
    }
  },

  save: async (name: string) => {
    const calc = useCalculatorStore.getState();
    const trimmed = name.trim();
    if (!trimmed) {
      set({ failure: { message: 'Give this scenario a name.', needsSignIn: false, needsPro: false } });
      return false;
    }
    if (calc.legs.length === 0) {
      set({ failure: { message: 'Add at least one leg before saving.', needsSignIn: false, needsPro: false } });
      return false;
    }
    // `riskFreeRate` is null until a rate has actually been measured. Saving a
    // fabricated 0 in its place would store a scenario that reprices
    // differently the moment it is reopened against a real rate -- the same
    // "refuse rather than answer a question we were not asked" rule the
    // calculator store applies to spot, IV and expiry.
    if (calc.riskFreeRate === null) {
      set({
        failure: {
          message: 'Waiting for the risk-free rate. Try again in a moment.',
          needsSignIn: false,
          needsPro: false,
        },
      });
      return false;
    }

    set({ status: 'loading', failure: null });
    try {
      // The SAME builder the pricing call uses, so a reopened scenario prices
      // identically to the one that was saved.
      const req = new SaveStrategyRequest();
      req.setName(trimmed);
      req.setRequest(
        buildStrategyRequest({
          symbol: calc.symbol,
          spotPrice: calc.spotPrice,
          impliedVolatility: positionIvOf(calc.legs),
          riskFreeRate: calc.riskFreeRate,
          dividendYield: calc.dividendYield,
          legs: calc.legs,
          horizonDays: horizonOf(calc.legs),
        }),
      );

      const client = new OptionsCalculatorClient(backendUrl());
      await client.saveStrategy(req, authMetadata());
      set({ lastSavedName: trimmed });
      await get().refresh();
      return true;
    } catch (err) {
      set({ status: 'error', failure: toFailure(err as never) });
      return false;
    }
  },

  remove: async (id: string) => {
    set({ failure: null });
    try {
      const req = new DeleteStrategyRequest();
      req.setId(id);
      const client = new OptionsCalculatorClient(backendUrl());
      await client.deleteStrategy(req, authMetadata());
      // Removed locally as well as refetched: the list must not show a row the
      // server has already dropped, even for the moment before refresh lands.
      set({ scenarios: get().scenarios.filter((s) => s.id !== id) });
      await get().refresh();
      return true;
    } catch (err) {
      set({ status: 'error', failure: toFailure(err as never) });
      return false;
    }
  },

  apply: (scenario: SavedScenario) => {
    const calc = useCalculatorStore.getState();
    // Order matters. setSymbol clears the legs and kicks off a fresh quote and
    // chain load, so the legs have to go on AFTER it -- adding them first would
    // have them wiped by the symbol change a line later.
    calc.setSymbol(scenario.symbol, scenario.spotPrice);
    calc.clearLegs();
    calc.setDividendYield(scenario.dividendYield);
    for (const leg of scenario.legs) {
      const { id: _ignored, ...rest } = leg;
      calc.addLeg(rest);
    }
    void calc.calculateStrategy();
  },
}));

/**
 * The position's IV and horizon, recomputed here rather than imported.
 *
 * `positionIv`/`horizonDays` live inside useCalculatorStore.ts as module-local
 * helpers and are not exported. These two are deliberately small and total --
 * a scenario is saved from whatever the ticket currently holds, and the ENGINE
 * is the side that refuses an unpriceable one, so a missing IV here must not
 * stop the save.
 */
function positionIvOf(legs: Leg[]): number {
  const ivs = legs.map((l) => l.implied_volatility).filter((v): v is number => typeof v === 'number' && v > 0);
  return ivs.length > 0 ? ivs.reduce((a, b) => a + b, 0) / ivs.length : 0;
}

function horizonOf(legs: Leg[]): number {
  const days = legs.map((l) => l.expiration_days).filter((v): v is number => typeof v === 'number');
  return days.length > 0 ? Math.max(...days) : 0;
}
