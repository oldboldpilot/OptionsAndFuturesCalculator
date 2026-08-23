# assistant-worker

The replica that HOLDS the assistant weights. It leases from the shared
inference queue, decodes, and writes the answer back. Nothing routes to it
directly: it has no domain and serves no user traffic.

`options-calculator-backend` is the counterpart. It keeps its replicas — that
is what serves the two live sites' calculator and finance RPCs — but carries
**no** models: `MODEL_URL` and `MORTGAGE_MODEL_URL` are empty there, so its
assistants run submit-only, accepting the RPC and handing the work here.

## Why this is a separate service, not another replica

Railway config is per-SERVICE. This one needs `numReplicas: 1` and the model
URLs set; the engine needs its own replica count and the model URLs empty.
A `numReplicas` bump cannot express that, and replicas share a hostname.

Same reason `sgee-queue-1/2/3` are three services rather than one with three
replicas — see `deploy/queue-node/README.md`.

## Why the upload is staged

Identical to the queue node's reason, and it is load-bearing rather than
cosmetic: Railway reads `railway.json` from the ROOT of the upload, and the one
at the repository root describes the ENGINE (`numReplicas: 2`, model URLs set).
Deploying that here would give the worker the engine's replica count. Railway's
per-service escape hatches (`railwayConfigFile`, `rootDirectory`) are not
reachable from the CLI, so the staged tree carries this directory's
`railway.json` instead.

## The rule that keeps this honest

**This service must NEVER have its model URLs emptied, and the engine must
NEVER have them set.** If both carry weights the split saves nothing; if
neither does, every assistant request refuses. The startup banner is the check —
one line per assistant per replica:

    assistant-worker            "model is LOADED"
    options-calculator-backend  "NOT LOCAL -- submitting to the shared inference queue"

A replica logging LOADED when it should be submit-only means the flip did not
take. That distinction exists precisely so this is checkable; `available()` is
true in both states and cannot tell them apart.

## Deploy order

Worker FIRST, engine second, and it is not cosmetic. While the engine still
carries weights it can execute locally, so a worker that is not yet leasing
costs nothing. Flip the engine to submit-only only once this service is
answering — otherwise there is a window where nobody holds the models and every
assistant request fails.

Rollback is the same order reversed: set the engine's model URLs back, redeploy,
and the queue becomes an optimisation again rather than the only path.
