/**
 * Broker partner links.
 *
 * Every URL here is the broker's own public page. NONE of them is a tracking
 * link yet, and that distinction is what `trackingId` exists to record: until a
 * programme is approved and an id is filled in, a click through this module
 * earns nothing. The module reads that flag rather than assuming, because
 * labelling an unpaid link "Sponsored" is a false statement about a commercial
 * relationship that does not exist -- and the FTC disclosure rules that govern
 * this cut both ways.
 *
 * To turn a partner on: join the network, get approved for that advertiser,
 * then set `trackingId` to the id they issue and `url` to the tracking URL they
 * give you. Do not hand-assemble a tracking URL from a template; networks
 * change the parameter names and a malformed link records no click while still
 * sending the user to the broker.
 *
 * Programme routes, correct as of writing and worth re-checking before applying
 * -- these terms change and several brokers have moved networks at least once:
 *
 *   Interactive Brokers  IBKR's own portal (Introducing Broker / referral),
 *                        not a network.
 *   Tradier              Direct developer/partner programme. API-first and the
 *                        most receptive of these to a tool like this one.
 *   tastytrade           Conventional affiliate programme, usually via a
 *                        network. The closest audience fit of any broker here:
 *                        it is an options-first retail broker.
 *   Alpaca               B2B Broker API partnership rather than click
 *                        affiliate. A conversation, not a signup form.
 *
 * Charles Schwab is deliberately absent. It has historically run no public
 * affiliate programme, so listing it would be an unpaid link taking up the slot
 * of one that could pay.
 */

export interface BrokerPartner {
  name: string;
  /** What this broker is actually good at, for someone pricing an option. */
  blurb: string;
  cta: string;
  url: string;
  /**
   * The affiliate/partner id, once approved. `null` means the link is a plain
   * outbound link that pays nothing -- see the disclosure logic in
   * SponsoredBrokers.
   */
  trackingId: string | null;
}

export const BROKER_PARTNERS: BrokerPartner[] = [
  {
    name: 'Interactive Brokers',
    blurb: 'Broad options and futures access with institutional-grade routing.',
    cta: 'See IBKR',
    url: 'https://www.interactivebrokers.com/',
    trackingId: null,
  },
  {
    name: 'tastytrade',
    blurb: 'Built around options. Per-leg pricing with capped commissions.',
    cta: 'See tastytrade',
    url: 'https://tastytrade.com/',
    trackingId: null,
  },
  {
    name: 'Tradier',
    blurb: 'API-first brokerage. Flat-rate options trading for developers.',
    cta: 'See Tradier',
    url: 'https://tradier.com/',
    trackingId: null,
  },
  {
    name: 'Alpaca',
    blurb: 'Commission-free API trading — the market data behind this site.',
    cta: 'See Alpaca',
    url: 'https://alpaca.markets/',
    trackingId: null,
  },
];

/** True once at least one partner is a real, paying link. */
export const HAS_PAID_PARTNER = BROKER_PARTNERS.some((b) => b.trackingId !== null);
