import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.7.1"

serve(async (req) => {
  // Simple validation (e.g. API key from broker)
  const authHeader = req.headers.get('Authorization')
  if (authHeader !== `Bearer ${Deno.env.get('BROKER_WEBHOOK_SECRET')}`) {
    return new Response('Unauthorized', { status: 401 })
  }

  const body = await req.json()
  // Expected structure from broker webhook: { "lead_id": "uuid", "status": "funded" }
  const { lead_id, status } = body

  if (!lead_id || !status) {
    return new Response('Missing lead_id or status', { status: 400 })
  }

  const supabaseUrl = Deno.env.get('SUPABASE_URL') as string
  const supabaseServiceKey = Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') as string
  const supabase = createClient(supabaseUrl, supabaseServiceKey)

  const { error } = await supabase
    .from('broker_lead_events')
    .update({ 
      conversion_status: status, 
      converted_at: new Date().toISOString() 
    })
    .eq('id', lead_id)

  if (error) {
    console.error("Error updating lead event:", error)
    return new Response('Database error', { status: 500 })
  }

  return new Response(JSON.stringify({ success: true }), {
    headers: { 'Content-Type': 'application/json' },
  })
})
