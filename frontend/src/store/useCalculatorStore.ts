import { create } from 'zustand';
import { createClient } from '../lib/supabase/client';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import { StrategyRequest, Leg as ProtoLeg, QuoteRequest } from '../grpc/calculator_pb';

export interface Leg {
  id: string;
  instrument_type: string;
  action: string;
  option_type: string;
  strike_price: number;
  premium: number;
  quantity: number;
  expiration_days?: number;
  implied_volatility?: number;
}

export interface MatrixCell {
  price: number;
  days_to_expiration: number;
  pnl_dollars: number;
  probability_density: number;
}

export interface CalculationResult {
  matrix: MatrixCell[];
  max_profit: number;
  max_loss: number;
  risk_reward_ratio: number;
  aggregate_greeks: {
    delta: number;
    gamma: number;
    theta: number;
    vega: number;
  };
}

interface CalculatorState {
  symbol: string;
  assetClass: 'EQUITY' | 'FUTURES' | 'CRYPTO';
  legs: Leg[];
  spotPrice: number;
  riskFreeRate: number;
  result: CalculationResult | null;
  isLoading: boolean;
  error: string | null;
  
  setSymbol: (symbol: string, spotPrice?: number, assetClass?: 'EQUITY' | 'FUTURES' | 'CRYPTO') => void;
  addLeg: (leg: Omit<Leg, 'id'>) => void;
  removeLeg: (id: string) => void;
  clearLegs: () => void;
  updateLeg: (id: string, updates: Partial<Leg>) => void;
  setSpotPrice: (price: number) => void;
  
  saveStrategy: (name: string, symbol: string) => Promise<void>;
  loadStrategies: () => Promise<void>;
  calculateStrategy: () => Promise<void>;
}

const supabase = createClient();

/**
 * Symbol -> asset class classification.
 *
 * This is reference data about *what an instrument is*, not a market
 * observation, so hardcoding it is legitimate. The previous build also carried
 * a hardcoded price for each symbol (SPY at 580.0 against a real 738.93); those
 * have been removed. Prices come from the backend quote service only — see
 * spec §3.4, real data only.
 */
const FUTURES_SYMBOLS = ['ES', 'NQ', 'RTY', 'YM', 'CL', 'NG', 'GC', 'SI', 'ZB', 'ZN'];
const CRYPTO_SYMBOLS = ['BTC', 'ETH', 'SOL'];

function classify(symbol: string): 'EQUITY' | 'FUTURES' | 'CRYPTO' {
  if (FUTURES_SYMBOLS.includes(symbol)) return 'FUTURES';
  if (CRYPTO_SYMBOLS.includes(symbol)) return 'CRYPTO';
  return 'EQUITY';
}

export const useCalculatorStore = create<CalculatorState>((set, get) => ({
  symbol: 'SPY',
  assetClass: 'EQUITY',
  legs: [],
  // Unknown until the quote service answers. Never seeded with a stand-in price.
  spotPrice: 0,
  riskFreeRate: 0.05,
  result: null,
  isLoading: false,
  error: null,

  setSymbol: (symbolInput, customPrice, customAssetClass) => {
    const sym = symbolInput.trim().toUpperCase();

    // A user-supplied simulation price is an explicit override and is honoured.
    // Otherwise the price is unknown until the quote service answers — it is
    // NOT defaulted to a plausible-looking number.
    set({
      symbol: sym,
      spotPrice: customPrice !== undefined ? customPrice : 0,
      assetClass: customAssetClass || classify(sym),
      error: null,
    });

    if (customPrice !== undefined) return;

    const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new OptionsCalculatorClient(backendUrl);
    const req = new QuoteRequest();
    req.setSymbol(sym);

    client.getMarketQuote(req, {}, (err, res) => {
      if (err || !res || res.getPrice() <= 0) {
        // Surface the failure. Substituting a fallback price here is what
        // previously let fabricated data reach the screen unnoticed.
        set({
          spotPrice: 0,
          error: `No quote available for ${sym}${err?.message ? `: ${err.message}` : ''}`,
        });
        return;
      }
      set({ spotPrice: res.getPrice(), error: null });
    });
  },

  addLeg: (leg) => set((state) => ({ 
    legs: [...state.legs, { ...leg, id: Math.random().toString(36).substring(7) }] 
  })),

  removeLeg: (id) => set((state) => ({
    legs: state.legs.filter(l => l.id !== id)
  })),

  clearLegs: () => set({ legs: [] }),

  updateLeg: (id, updates) => set((state) => ({
    legs: state.legs.map(l => l.id === id ? { ...l, ...updates } : l)
  })),

  setSpotPrice: (price) => set({ spotPrice: price }),

  calculateStrategy: async () => {
    set({ isLoading: true, error: null });
    try {
      const { legs, spotPrice, riskFreeRate } = get();
      
      const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'http://localhost:8080';
      const client = new OptionsCalculatorClient(backendUrl);
      const req = new StrategyRequest();
      req.setUnderlyingSymbol('SPY'); // Hardcoded or from state
      req.setCurrentPrice(spotPrice);
      req.setImpliedVolatility(0.20); // Hardcoded or from state
      req.setRiskFreeRate(riskFreeRate);
      
      const protoLegs = legs.map(l => {
        const pLeg = new ProtoLeg();
        pLeg.setAction(l.action === 'BUY' ? ProtoLeg.Action.BUY : ProtoLeg.Action.SELL);
        pLeg.setType(l.option_type === 'CALL' ? ProtoLeg.Type.CALL : ProtoLeg.Type.PUT);
        pLeg.setStrike(l.strike_price);
        pLeg.setExpirationDays(30); // Hardcoded or from state
        pLeg.setQuantity(l.quantity);
        return pLeg;
      });
      req.setLegsList(protoLegs);
      
      const res = await client.calculateStrategy(req, {});
      
      const points = res.getPnlMatrixList();
      const mappedMatrix = points.map((p: any) => ({
        price: p.getUnderlyingPrice(),
        days_to_expiration: 30, // Mocked from points if not returned
        pnl_dollars: p.getPnl(),
        probability_density: 0.5 // getProbability does not exist on PnLPoint
      }));
      
      set({
        isLoading: false,
        result: {
          matrix: mappedMatrix,
          max_profit: res.getMaxProfit(),
          max_loss: res.getMaxLoss(),
          risk_reward_ratio: res.getMaxLoss() !== 0 ? Math.abs(res.getMaxProfit() / res.getMaxLoss()) : 0,
          aggregate_greeks: { 
            delta: res.getNetGreeks()?.getDelta() || 0, 
            gamma: res.getNetGreeks()?.getGamma() || 0, 
            theta: res.getNetGreeks()?.getTheta() || 0, 
            vega: res.getNetGreeks()?.getVega() || 0 
          }
        }
      });
      
    } catch (err: unknown) {
      set({ isLoading: false, error: (err as Error).message || 'Calculation failed' });
    }
  },

  saveStrategy: async (name: string, symbol: string) => {
    set({ isLoading: true, error: null });
    try {
      const { legs } = get();
      const { data: { user } } = await supabase.auth.getUser();
      if (!user) throw new Error('Must be logged in to save strategies');

      const { error } = await supabase.from('saved_strategies').insert([{
        name,
        symbol,
        legs,
        user_id: user.id
      }]);

      if (error) throw error;
      set({ isLoading: false });
    } catch (err: unknown) {
      set({ isLoading: false, error: (err as Error).message || 'Failed to save strategy' });
    }
  },

  loadStrategies: async () => {
    // This could populate another state variable `savedStrategies` if we had one
    // But for now, just to show how it's done:
    set({ isLoading: true, error: null });
    try {
      const { data, error } = await supabase.from('saved_strategies').select('*');
      if (error) throw error;
      console.log('Loaded strategies:', data);
      set({ isLoading: false });
    } catch (err: unknown) {
      set({ isLoading: false, error: (err as Error).message || 'Failed to load strategies' });
    }
  }
}));
