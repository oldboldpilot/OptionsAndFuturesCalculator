import { create } from 'zustand';

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
  updateLeg: (id: string, updates: Partial<Leg>) => void;
  setSpotPrice: (price: number) => void;
  
  calculateStrategy: () => Promise<void>;
}

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

  updateLeg: (id, updates) => set((state) => ({
    legs: state.legs.map(l => l.id === id ? { ...l, ...updates } : l)
  })),

  setSpotPrice: (price) => set({ spotPrice: price }),

  calculateStrategy: async () => {
    set({ isLoading: true, error: null });
    try {
      const { legs, spotPrice, riskFreeRate } = get();
      
      // TODO: Call actual gRPC-Web client here
      // const client = new CalculatorEngineServiceClient('https://api.domain.com');
      // const req = new CalculationRequest();
      // ... populate req ...
      // const res = await client.computeStrategyPnL(req, {});
      
      // MOCK RESULT for now until protoc stubs are generated
      setTimeout(() => {
        set({
          isLoading: false,
          result: {
            matrix: [], // Would be populated with 3D heatmap data
            max_profit: 500,
            max_loss: -200,
            risk_reward_ratio: 2.5,
            aggregate_greeks: { delta: 10, gamma: -2, theta: 5, vega: 12 }
          }
        });
      }, 500);
      
    } catch (err: any) {
      set({ isLoading: false, error: err.message || 'Calculation failed' });
    }
  }
}));
