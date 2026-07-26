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

const TICKER_DATABASE: Record<string, { price: number; category: 'EQUITY' | 'FUTURES' | 'CRYPTO' }> = {
  // Equities & ETFs
  'SPY': { price: 580.0, category: 'EQUITY' },
  'QQQ': { price: 490.0, category: 'EQUITY' },
  'IWM': { price: 220.0, category: 'EQUITY' },
  'DIA': { price: 410.0, category: 'EQUITY' },
  'NVDA': { price: 125.0, category: 'EQUITY' },
  'AAPL': { price: 230.0, category: 'EQUITY' },
  'MSFT': { price: 440.0, category: 'EQUITY' },
  'AMZN': { price: 185.0, category: 'EQUITY' },
  'GOOGL': { price: 175.0, category: 'EQUITY' },
  'META': { price: 500.0, category: 'EQUITY' },
  'TSLA': { price: 240.0, category: 'EQUITY' },
  'AMD': { price: 150.0, category: 'EQUITY' },
  'PLTR': { price: 28.50, category: 'EQUITY' },
  'COIN': { price: 220.0, category: 'EQUITY' },
  'NFLX': { price: 650.0, category: 'EQUITY' },
  'DIS': { price: 95.0, category: 'EQUITY' },
  'BA': { price: 180.0, category: 'EQUITY' },
  'JPM': { price: 210.0, category: 'EQUITY' },

  // Futures Contracts
  'ES': { price: 5850.0, category: 'FUTURES' },
  'NQ': { price: 20400.0, category: 'FUTURES' },
  'RTY': { price: 2220.0, category: 'FUTURES' },
  'YM': { price: 41200.0, category: 'FUTURES' },
  'CL': { price: 78.50, category: 'FUTURES' },
  'NG': { price: 2.45, category: 'FUTURES' },
  'GC': { price: 2420.0, category: 'FUTURES' },
  'SI': { price: 31.00, category: 'FUTURES' },
  'ZB': { price: 118.25, category: 'FUTURES' },
  'ZN': { price: 110.50, category: 'FUTURES' },

  // Crypto Derivatives
  'BTC': { price: 67500.0, category: 'CRYPTO' },
  'ETH': { price: 3500.0, category: 'CRYPTO' },
  'SOL': { price: 175.0, category: 'CRYPTO' }
};

export const useCalculatorStore = create<CalculatorState>((set, get) => ({
  symbol: 'SPY',
  assetClass: 'EQUITY',
  legs: [],
  spotPrice: 580.0,
  riskFreeRate: 0.05,
  result: null,
  isLoading: false,
  error: null,

  setSymbol: (symbolInput, customPrice, customAssetClass) => {
    const sym = symbolInput.trim().toUpperCase();
    const known = TICKER_DATABASE[sym];

    const finalPrice = customPrice !== undefined 
      ? customPrice 
      : known 
        ? known.price 
        : 100.0;

    const finalCategory = customAssetClass || (known ? known.category : (['ES', 'NQ', 'CL', 'GC', 'ZB', 'NG', 'RTY', 'YM', 'SI', 'ZN'].includes(sym) ? 'FUTURES' : ['BTC', 'ETH', 'SOL'].includes(sym) ? 'CRYPTO' : 'EQUITY'));

    set({
      symbol: sym,
      spotPrice: finalPrice,
      assetClass: finalCategory
    });

    // Asynchronously fetch live Yahoo Finance quote from Railway gRPC-Web backend
    try {
      const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
      const client = new OptionsCalculatorClient(backendUrl);
      const req = new QuoteRequest();
      req.setSymbol(sym);

      client.getMarketQuote(req, {}, (err, res) => {
        if (!err && res && res.getPrice() > 0) {
          set({
            spotPrice: res.getPrice()
          });
        }
      });
    } catch (e) {
      console.warn('Backend quote lookup fallback to static DB:', e);
    }
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
