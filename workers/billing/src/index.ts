/**
 * Billing Worker for Options & Futures Calculator Pro.
 *
 * Three jobs, and nothing else:
 *
 *   POST /checkout          start a Stripe Checkout session
 *   GET  /licence           exchange a completed session for a licence
 *   POST /webhook           receive Stripe events, mint and email licences
 *
 * It lives here rather than in the C++ engine because the engine is a
 * stateless compute service reached over gRPC, and none of this is compute:
 * it is HTTP, webhook signature verification and outbound email. Putting it in
 * the engine would mean an HTTP router and an SMTP path inside a process whose
 * job is pricing options.
 *
 * It is NOT in the frontend because the frontend is a static export -- it
 * cannot hold the Stripe secret key, and it cannot receive a webhook.
 */
import { mintLicence, verifyStripeSignature } from './licence.ts';

export interface Env {
  STRIPE_SECRET_KEY: string;
  STRIPE_WEBHOOK_SECRET: string;
  LICENCE_SIGNING_KEY: string;
  RESEND_API_KEY: string;
  PRICE_MONTHLY: string;
  PRICE_ANNUAL: string;
  SITE_URL: string;
  TRIAL_DAYS: string;
}

/**
 * Origins allowed to start a checkout.
 *
 * Narrow rather than `*`: this endpoint creates real payment sessions, and an
 * open CORS policy would let any site drive our billing flow and present it as
 * their own.
 */
const ALLOWED_ORIGINS = [
  'https://optionsandfuturescalculator.com',
  'https://www.optionsandfuturescalculator.com',
  'https://optionsandfuturescalculator.pages.dev',
];

function corsHeaders(origin: string | null): Record<string, string> {
  const allowed = origin && ALLOWED_ORIGINS.includes(origin) ? origin : ALLOWED_ORIGINS[0];
  return {
    'Access-Control-Allow-Origin': allowed,
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Vary': 'Origin',
  };
}

function json(data: unknown, status: number, origin: string | null): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json', ...corsHeaders(origin) },
  });
}

/** Stripe's API is form-encoded, including for nested keys like a[b][c]. */
async function stripe(
  env: Env,
  path: string,
  method: 'GET' | 'POST',
  form?: Record<string, string>,
): Promise<any> {
  const body = form ? new URLSearchParams(form).toString() : undefined;
  const url = method === 'GET' && body
    ? `https://api.stripe.com/v1/${path}?${body}`
    : `https://api.stripe.com/v1/${path}`;

  const res = await fetch(url, {
    method,
    headers: {
      Authorization: `Bearer ${env.STRIPE_SECRET_KEY}`,
      ...(method === 'POST' ? { 'Content-Type': 'application/x-www-form-urlencoded' } : {}),
    },
    ...(method === 'POST' ? { body } : {}),
  });
  return res.json();
}

// ---------------------------------------------------------------------------
// POST /checkout
// ---------------------------------------------------------------------------

async function handleCheckout(req: Request, env: Env, origin: string | null): Promise<Response> {
  let plan = 'monthly';
  try {
    const body = (await req.json()) as { plan?: string };
    if (body.plan === 'annual') plan = 'annual';
  } catch {
    // No body is fine -- monthly is the default.
  }

  const price = plan === 'annual' ? env.PRICE_ANNUAL : env.PRICE_MONTHLY;
  if (!price) return json({ error: 'price not configured' }, 500, origin);

  const trialDays = Number(env.TRIAL_DAYS || '7');
  const site = env.SITE_URL || ALLOWED_ORIGINS[0];

  const form: Record<string, string> = {
    mode: 'subscription',
    'line_items[0][price]': price,
    'line_items[0][quantity]': '1',
    // The session id in the success URL is what lets the browser collect its
    // licence immediately instead of waiting for the email to land.
    success_url: `${site}/?checkout=success&session_id={CHECKOUT_SESSION_ID}`,
    cancel_url: `${site}/?checkout=cancel`,
    allow_promotion_codes: 'true',
  };
  if (trialDays > 0) form['subscription_data[trial_period_days]'] = String(trialDays);

  const session = await stripe(env, 'checkout/sessions', 'POST', form);
  if (session.error) return json({ error: session.error.message }, 502, origin);

  return json({ url: session.url }, 200, origin);
}

// ---------------------------------------------------------------------------
// GET /licence?session_id=cs_...
// ---------------------------------------------------------------------------

/**
 * Exchanges a completed Checkout session for a licence.
 *
 * The session id is only ever handed to the browser that completed the
 * payment, via the success URL. It is treated as a bearer credential for
 * exactly this exchange, which is why the session is re-fetched from Stripe
 * and its payment status checked here rather than trusted from the query
 * string -- a session id alone proves nothing until Stripe confirms it is
 * paid.
 */
async function handleLicence(url: URL, env: Env, origin: string | null): Promise<Response> {
  const sessionId = url.searchParams.get('session_id');
  if (!sessionId || !sessionId.startsWith('cs_')) {
    return json({ error: 'missing or malformed session_id' }, 400, origin);
  }

  const session = await stripe(env, `checkout/sessions/${sessionId}`, 'GET', {
    'expand[0]': 'subscription',
  });
  if (session.error) return json({ error: 'unknown session' }, 404, origin);

  // `paid` covers an immediate charge; `no_payment_required` is what a session
  // with a free trial returns, and refusing it would lock out exactly the
  // customers the trial exists to attract.
  const ok = session.status === 'complete' &&
    (session.payment_status === 'paid' || session.payment_status === 'no_payment_required');
  if (!ok) return json({ error: 'session is not complete' }, 402, origin);

  const sub = session.subscription;
  const periodEnd = typeof sub === 'object' && sub?.current_period_end
    ? Number(sub.current_period_end)
    : Math.floor(Date.now() / 1000) + 31 * 86400;

  const licence = await mintLicence(env.LICENCE_SIGNING_KEY, {
    customerId: String(session.customer ?? 'unknown'),
    tier: 'pro',
    periodEndEpoch: periodEnd,
  });

  return json({ licence, expires: periodEnd }, 200, origin);
}

// ---------------------------------------------------------------------------
// POST /webhook
// ---------------------------------------------------------------------------

async function handleWebhook(req: Request, env: Env): Promise<Response> {
  const body = await req.text();
  const sig = req.headers.get('Stripe-Signature');

  // The webhook URL is public, so the signature IS the authentication. An
  // unverified request here would mint Pro licences for anyone who can POST.
  if (!(await verifyStripeSignature(env.STRIPE_WEBHOOK_SECRET, sig, body))) {
    return new Response('invalid signature', { status: 400 });
  }

  const event = JSON.parse(body);
  const relevant = [
    'checkout.session.completed',
    'customer.subscription.created',
    'customer.subscription.updated',
  ];
  // 200 on events we do not handle: a non-2xx makes Stripe retry with backoff
  // and eventually disable the endpoint, so "not interested" must not look
  // like "failed".
  if (!relevant.includes(event.type)) return new Response('ignored', { status: 200 });

  const obj = event.data.object;
  const customerId = String(obj.customer ?? '');
  if (!customerId) return new Response('no customer', { status: 200 });

  // A cancelled or unpaid subscription simply stops being reissued. There is
  // nothing to revoke -- the outstanding licence expires on its own, which is
  // the behaviour a customer who paid for the month is owed.
  const status = obj.status ?? 'active';
  if (['canceled', 'unpaid', 'incomplete_expired'].includes(status)) {
    return new Response('not reissuing', { status: 200 });
  }

  const periodEnd = Number(obj.current_period_end ?? Math.floor(Date.now() / 1000) + 31 * 86400);
  const licence = await mintLicence(env.LICENCE_SIGNING_KEY, {
    customerId,
    tier: 'pro',
    periodEndEpoch: periodEnd,
  });

  const email = obj.customer_details?.email ?? obj.customer_email ?? (await customerEmail(env, customerId));
  if (email) await sendLicenceEmail(env, email, licence);

  return new Response('ok', { status: 200 });
}

async function customerEmail(env: Env, customerId: string): Promise<string | null> {
  const c = await stripe(env, `customers/${customerId}`, 'GET');
  return c?.email ?? null;
}

async function sendLicenceEmail(env: Env, to: string, licence: string): Promise<void> {
  if (!env.RESEND_API_KEY) return;
  await fetch('https://api.resend.com/emails', {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${env.RESEND_API_KEY}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({
      from: 'Options & Futures Calculator <noreply@optionsandfuturescalculator.com>',
      to,
      subject: 'Your Pro licence key',
      text:
        `Thanks for subscribing to Options & Futures Calculator Pro.\n\n` +
        `Your licence key:\n\n  ${licence}\n\n` +
        `Paste it into the app under "Activate Pro". It unlocks multi-leg ` +
        `strategies — spreads, straddles, condors, butterflies and futures ` +
        `spreads.\n\n` +
        `The key renews automatically with your subscription; you will get a ` +
        `fresh one each period, and the old one keeps working until it expires.\n`,
    }),
  });
}

export default {
  async fetch(req: Request, env: Env): Promise<Response> {
    const url = new URL(req.url);
    const origin = req.headers.get('Origin');

    if (req.method === 'OPTIONS') {
      return new Response(null, { status: 204, headers: corsHeaders(origin) });
    }

    switch (`${req.method} ${url.pathname}`) {
      case 'POST /checkout':
        return handleCheckout(req, env, origin);
      case 'GET /licence':
        return handleLicence(url, env, origin);
      case 'POST /webhook':
        return handleWebhook(req, env);
      case 'GET /health':
        return json({ ok: true }, 200, origin);
      default:
        return json({ error: 'not found' }, 404, origin);
    }
  },
};
