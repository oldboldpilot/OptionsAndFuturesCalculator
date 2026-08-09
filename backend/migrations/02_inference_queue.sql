-- @author Olumuyiwa Oluwasanmi
--
-- Postgres substrate for a shared inference queue -- a cluster-wide,
-- crash-safe job queue so more than one engine replica can share the two
-- fine-tuned Qwen3-0.6B assistants (options-strategy and mortgage) as a pool
-- of workers rather than each replica needing its own loaded copy.
--
-- PURELY ADDITIVE. This migration creates only NEW objects -- one sequence,
-- one table, three partial indexes -- and touches no row, column, or
-- constraint that 01_init.sql already created. It is therefore safe to apply
-- to the live Railway database at any time, with no maintenance window and
-- no risk to public.users / public.profiles / public.saved_strategies. Every
-- statement is IF NOT EXISTS, matching 01_init.sql's own convention, so
-- re-running this file is a no-op.
--
-- Authored and tested against a LOCAL Postgres only. It is NOT applied to
-- Railway by this commit -- the live apply is a separate, owner-run step
-- (see the task that produced this file).
--
-- What this file does NOT do: nothing here is wired into the gRPC serving
-- path. assistant_service.cpp and mortgage_assistant_service.cpp do not
-- import the modules built on top of this schema (pg.cppm,
-- inference_queue.cppm) -- that wiring is a later, separate wave. This
-- migration exists purely so that substrate has somewhere to persist state
-- when it does land.

-- 1. Fencing token sequence.
--
-- A cluster-wide monotone counter, not a per-row column default, because the
-- token must keep increasing as leases move from row to row and from worker
-- to worker: two different jobs leased seconds apart must never be able to
-- produce the same fencing_token. A stale worker's COMPLETE is discarded by
-- comparing the token it was handed at lease time against the token the row
-- holds NOW (inference_jobs.fencing_token, updated on every re-lease) -- a
-- token collision would let a stale worker's write through instead of being
-- correctly discarded as belonging to a superseded lease.
CREATE SEQUENCE IF NOT EXISTS public.inference_fence;

-- 2. The job table.
--
-- One row per queued inference request, shared across both assistants
-- (`surface` distinguishes them) and across however many engine replicas are
-- running. `payload` and `result` are JSONB rather than TEXT so a malformed
-- caller payload fails at the ::jsonb cast on INSERT rather than being
-- silently stored as an opaque string only to be discovered unparseable at
-- lease time by a worker that can no longer refuse the submission.
CREATE TABLE IF NOT EXISTS public.inference_jobs (
  id              BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  surface         TEXT NOT NULL CHECK (surface IN ('strategy', 'mortgage')),
  payload         JSONB NOT NULL,
  state           TEXT NOT NULL DEFAULT 'pending'
                    CHECK (state IN ('pending', 'leased', 'done', 'failed', 'dead')),
  attempts        INT NOT NULL DEFAULT 0,
  -- max_attempts defaults to 2 deliberately: one retry covers a worker crash
  -- mid-lease; more than that burns roughly 1.1s of single-threaded CPU (see
  -- quota.cpp's cost_llm_generate, ~34 tokens/sec) per attempt on a request
  -- whose caller has, in the overwhelming majority of worker-crash cases,
  -- already given up and disconnected.
  max_attempts    INT NOT NULL DEFAULT 2,
  fencing_token   BIGINT,
  lease_owner     TEXT,
  lease_deadline  TIMESTAMPTZ,
  submit_deadline TIMESTAMPTZ NOT NULL,
  created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
  started_at      TIMESTAMPTZ,
  finished_at     TIMESTAMPTZ,
  result          JSONB,
  error           TEXT
);

-- 3. Partial indexes -- one per access pattern the queue protocol actually
-- runs, each scoped with WHERE so it stays small and cheap to maintain as
-- the table fills with terminal-state history between sweeper reap passes.

-- Pending: what LEASE's subquery scans -- the oldest pending row for one
-- surface whose submit_deadline has not passed. (surface, id) lets that
-- subquery's `WHERE surface = $1 ... ORDER BY id` walk the index directly
-- instead of scanning every pending row of every surface.
CREATE INDEX IF NOT EXISTS idx_inference_jobs_pending
  ON public.inference_jobs (surface, id)
  WHERE state = 'pending';

-- Leased: what the SWEEPER scans to find leases past lease_deadline.
CREATE INDEX IF NOT EXISTS idx_inference_jobs_leased
  ON public.inference_jobs (lease_deadline)
  WHERE state = 'leased';

-- Finished: what the SWEEPER's reap step scans (done/failed older than 24h,
-- dead older than 7d -- see inference_queue.cppm's Queue::sweep_once). All
-- three terminal states share one index because the reap step's WHERE
-- clause is a single predicate across exactly these three states, keyed on
-- the same finished_at column.
CREATE INDEX IF NOT EXISTS idx_inference_jobs_finished
  ON public.inference_jobs (finished_at)
  WHERE state IN ('done', 'failed', 'dead');
