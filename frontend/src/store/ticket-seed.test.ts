/**
 * The ticket opens on the at-the-money contract (OFC-026), and a committed leg
 * says when it expires (OFC-025).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The Strike list opened 28% in the money: 151 strikes starting at 550.00 with
 * SPY at 779.09, the at-the-money one about ninetieth, each labelled with a
 * bare second number ("550.00  230.94") that was an ask or a bid and said
 * neither. Nobody starts a position at 550, and the first screen had no
 * example of what a finished ticket looks like. The ticket now opens on the
 * at-the-money strike with its executable price, and every option can still be
 * changed before it is added.
 *
 * It is a SEED, never an overwrite: a strike the trader has already chosen is
 * left exactly as it is.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  settle,
} from '../test/grpc-harness';

const fake = createFakeClient({
  getMarketChain: () => ({
    ok: chainResponse(
      [chainExpiration('2026-10-13', 7), chainExpiration('2026-10-23', 17)],
      [
        chainStrike(777.5, { callBid: 5.1, callAsk: 5.2, callIv: 0.11, putBid: 3.0, putAsk: 3.1, putIv: 0.12 }),
        chainStrike(780, { isAtm: true, callBid: 4.4, callAsk: 4.49, callIv: 0.105, putBid: 3.4, putAsk: 3.54, putIv: 0.118 }),
        chainStrike(782.5, { callBid: 3.6, callAsk: 3.7, callIv: 0.1, putBid: 0, putAsk: 0, putIv: 0 }),
      ],
      '2026-10-13',
    ),
  }),
});

vi.mock('../grpc/CalculatorServiceClientPb', () => ({
  OptionsCalculatorClient: class {
    getMarketQuote = fake.client.getMarketQuote;
    getRiskFreeRate = fake.client.getRiskFreeRate;
    getMarketChain = fake.client.getMarketChain;
    calculateStrategy = fake.client.calculateStrategy;
  },
}));
vi.mock('../lib/supabase/client', () => ({
  createClient: () => ({
    auth: {
      getSession: async () => ({ data: { session: null }, error: null }),
      onAuthStateChange: () => ({ data: { subscription: { unsubscribe() {} } } }),
    },
  }),
}));
vi.mock('../lib/licence', () => ({ authMetadata: () => ({}) }));

import { useCalculatorStore } from './useCalculatorStore';

const st = () => useCalculatorStore.getState();

async function loaded() {
  useCalculatorStore.setState({
    symbol: 'SPY', assetClass: 'EQUITY', legs: [], spotPrice: 779.09, retiredPosition: null,
    notReady: null, error: null,
    ticket: {
      action: 'BUY', optionType: 'CALL', expiration: '', strike: null, premium: null,
      quantity: 1, impliedVolatility: null, asianType: 'NOT_ASIAN',
    },
  });
  st().loadChain();
  await settle(() => st().chainStatus === 'ready', 'chain');
}

describe('seedTicketFromAtm', () => {
  beforeEach(loaded);

  it('fills strike, executable price and IV from the at-the-money row', () => {
    st().seedTicketFromAtm();
    const t = st().ticket;
    expect(t.strike).toBe(780);
    expect(t.premium).toBe(4.49); // buying lifts the ask
    expect(t.impliedVolatility).toBe(0.105);
  });

  it('follows the side and type the ticket already carries', () => {
    st().setTicket({ action: 'SELL', optionType: 'PUT' });
    st().seedTicketFromAtm();
    const t = st().ticket;
    expect(t.premium).toBe(3.4); // selling hits the bid
    expect(t.impliedVolatility).toBe(0.118);
  });

  it('never overwrites a strike the trader chose', () => {
    st().setTicket({ strike: 777.5, premium: 5.2, impliedVolatility: 0.11 });
    st().seedTicketFromAtm();
    expect(st().ticket.strike).toBe(777.5);
  });

  it('leaves the price empty rather than zero when the ATM side has no quote', () => {
    useCalculatorStore.setState({
      chainStrikes: [
        {
          strike: 782.5,
          isAtm: true,
          call: { bid: 0, ask: 0, delta: 0, iv: 0, volume: 0, openInterest: 0 },
          put: { bid: 0, ask: 0, delta: 0, iv: 0, volume: 0, openInterest: 0 },
        },
      ],
    });
    st().seedTicketFromAtm();
    const t = st().ticket;
    expect(t.strike).toBe(782.5);
    expect(t.premium).toBeNull();
    expect(t.impliedVolatility).toBeNull();
  });

  it('does nothing without an at-the-money row', () => {
    useCalculatorStore.setState({ chainStrikes: [] });
    st().seedTicketFromAtm();
    expect(st().ticket.strike).toBeNull();
  });
});

describe('a committed leg says when it expires', () => {
  beforeEach(loaded);

  it('records the chain date it was priced against', () => {
    st().seedTicketFromAtm();
    st().commitTicket();
    const leg = st().legs[0];
    expect(leg.expiry_label).toBe('2026-10-13');
    expect(leg.expiration_days).toBe(7);
    expect(leg.symbol).toBe('SPY');
  });

  it('records the date of the expiry the ticket was changed to', async () => {
    st().setSelectedExpiration('2026-10-23');
    await settle(() => st().chainStatus === 'ready' && st().selectedExpiration === '2026-10-23', 'second chain');
    st().setTicket({ expiration: '2026-10-23' });
    st().seedTicketFromAtm();
    st().commitTicket();
    expect(st().legs[0].expiry_label).toBe('2026-10-23');
    expect(st().legs[0].expiration_days).toBe(17);
  });
});
