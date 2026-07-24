import { serve } from "https://deno.land/std@0.177.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.7.1";

serve(async (req) => {
  if (req.method !== "POST") {
    return new Response("Method Not Allowed", { status: 405 });
  }

  try {
    const data = await req.json();
    const { utm_source, utm_medium, utm_campaign, utm_term, utm_content, email, name } = data;

    // Supabase client setup
    const supabaseUrl = Deno.env.get("SUPABASE_URL") ?? "";
    const supabaseServiceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? "";
    const supabase = createClient(supabaseUrl, supabaseServiceKey);

    // Save lead to Supabase database (for Lovable CRM sync)
    const { data: lead, error } = await supabase
      .from("leads")
      .insert([
        {
          email,
          name,
          utm_source,
          utm_medium,
          utm_campaign,
          utm_term,
          utm_content,
        },
      ])
      .select();

    if (error) {
      throw error;
    }

    // Optional: direct integration logic with Lovable CRM here if they have an API endpoint
    // await fetch('https://api.lovable.crm/leads', { method: 'POST', body: JSON.stringify(lead[0]) })

    return new Response(JSON.stringify({ success: true, lead }), {
      headers: { "Content-Type": "application/json" },
    });
  } catch (error: any) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 400,
      headers: { "Content-Type": "application/json" },
    });
  }
});
