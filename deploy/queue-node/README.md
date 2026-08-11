# SGEE queue-node deploy

@author Olumuyiwa Oluwasanmi

Three Railway services — `sgee-queue-1`, `sgee-queue-2`, `sgee-queue-3` — each
running one replica of `backend/Dockerfile.queue-node` with its own `/data`
volume. Together they are one Raft cluster.

## Why three services rather than `numReplicas: 3`

Railway is explicit that **replicas cannot be used with volumes**, and a Raft
node has to fsync `currentTerm`, `votedFor` and its log before answering any
RPC. Beyond durability, replicas of one service share a name:
`<service>.railway.internal` resolves to a randomly chosen replica, so peers
would have no way to address each other, and `RAILWAY_REPLICA_ID` is visible
only inside the container that owns it. N separate services, each with a volume
and a stable internal hostname, is Railway's own idiom for this — it is how
their MongoDB replica-set template is built.

This is a materially different topology from the `numReplicas: 3` on the
`options-calculator-backend` service, which is stateless and wants exactly the
opposite thing.

## Why the deploy goes through a staging directory

`railway up` uploads the current directory and Railway reads `railway.json`
from the **root of that upload**. The repository root already has one, and it
describes the engine: `backend/Dockerfile`, `numReplicas: 3`. A queue service
deploying from the repository root would therefore build the wrong image and
ask for three replicas of a volume-backed service, which Railway rejects.

Railway's only per-service escape hatches are the `railwayConfigFile` and
`rootDirectory` settings, neither of which the CLI can set. So `deploy.sh`
assembles an upload whose root holds *this* `railway.json` and the `backend/`
tree, and runs `railway up` from there. Nothing about it is a workaround for a
missing permission — it is the same mechanism, pointed at a different root.

## Deploying

```bash
deploy/queue-node/deploy.sh 1        # one node
deploy/queue-node/deploy.sh all      # all three, in order
```

Set the per-node variables first (they have no image defaults on purpose — see
the Dockerfile header for why a default node id is worse than a missing one):

```bash
deploy/queue-node/set-vars.sh
```

## Rolling, not simultaneous

Deploy one node at a time and wait for `/statusz` to show the cluster back at
three healthy members before starting the next. A three-node cluster tolerates
exactly one node down; replacing two at once loses quorum and every enqueue
fails until it returns.
