import { create } from 'zustand';
import { createClient } from '../lib/supabase/client';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import { StrategyRequest, Leg as ProtoLeg } from '../grpc/calculator_pb';

export interface Leg {
  id: string;
  instrument_type: string; // 'EQUITY_OPTION', 'EQUITY_SPOT', etc.
  action: string; // 'BUY', 'SELL'
  quantity: number;
  strike_price: number;
  option_type: string; // 'CALL', 'PUT'
  premium: number;
  implied_volatility: number;
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
  legs: Leg[];
  spotPrice: number;
  riskFreeRate: number;
  result: CalculationResult | null;
  isLoading: boolean;
  error: string | null;
  
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

export const useCalculatorStore = create<CalculatorState>((set, get) => ({
  legs: [],
  spotPrice: 150.0,
  riskFreeRate: 0.05,
  result: null,
  isLoading: false,
  error: null,

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
      const mappedMatrix = points.map(p => ({
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
