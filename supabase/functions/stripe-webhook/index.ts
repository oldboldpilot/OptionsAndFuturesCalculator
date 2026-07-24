import { serve } from "https://deno.land/std@0.177.0/http/server.ts";
import Stripe from "https://esm.sh/stripe@11.1.0?target=deno";

const stripe = new Stripe(Deno.env.get("STRIPE_SECRET_KEY") as string, {
  apiVersion: "2022-11-15",
  httpClient: Stripe.createFetchHttpClient(),
});
const endpointSecret = Deno.env.get("STRIPE_WEBHOOK_SECRET") as string;

serve(async (req) => {
  const signature = req.headers.get("stripe-signature");
  if (!signature) {
    return new Response("No signature", { status: 400 });
  }

  const body = await req.text();
  let event;

  try {
    event = await stripe.webhooks.constructEventAsync(
      body,
      signature,
      endpointSecret
    );
  } catch (err: any) {
    console.error(`Webhook Error: ${err.message}`);
    return new Response(`Webhook Error: ${err.message}`, { status: 400 });
  }

  // Handle the event for B2B SaaS
  switch (event.type) {
    case "customer.subscription.created":
      console.log("Subscription created:", event.data.object);
      break;
    case "customer.subscription.updated":
      console.log("Subscription updated:", event.data.object);
      break;
    case "customer.subscription.deleted":
      console.log("Subscription deleted:", event.data.object);
      break;
    default:
      console.log(`Unhandled event type ${event.type}`);
  }

  return new Response(JSON.stringify({ received: true }), {
    headers: { "Content-Type": "application/json" },
  });
});
