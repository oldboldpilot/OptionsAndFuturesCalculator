# Container model hosting: getting two 639 MB GGUFs into every replica, every deploy

`@author Olumuyiwa Oluwasanmi`

2026-08-05. Status: **DECIDED — Railway bucket, fetched at image build time.**
Design only; nothing in this document has been executed against production.

## 1. The problem

Two fine-tuned Qwen3-0.6B Q8_0 GGUFs (~639 MB each) must be present in the
`options-calculator-backend` container (project `fearless-amazement`,
environment `production`, `numReplicas: 2`) on every deploy:

| model | file (NAS archive) | sha256 (pinned on Railway) |
| --- | --- | --- |
| strategy assistant | `param-agent-qlora-v2-Q8_0.gguf` (639,447,616 B) | `eab97cf531b0c3746e366afafaaf74f90bec2d9263f269cd66018605223d80ac` |
| mortgage assistant | `mortgagefv-assistant-q8_0.gguf` (639,447,136 B) | `31ed8f45b3fca52cf46d99ccecc65e9a7210b736cbc39ae043270be40c5630a5` |

Both live at `/home/muyiwa/PrimaryNAS/DataFolder/model_checkpoints/` with a
`MANIFEST.md` whose checksums match the `MODEL_SHA256` /
`MORTGAGE_MODEL_SHA256` values already set on Railway. The private HuggingFace
repo that used to serve them was deleted on 2026-08-05 at the owner's
instruction, so **the container currently deploys with `/app/model/` empty**
(verified today via `railway ssh "ls /app/model/"`) and both assistants answer
MODEL_UNAVAILABLE.

### Constraints (measured, not assumed)

1. `railway up` returns **HTTP 413** from Cloudflare at 1,229,621,349 bytes
   and again at 619,301,067 bytes; the 321 MB repo context uploads fine. The
   cap is a payload-size limit somewhere around ~500 MB, not the ~30 s timeout
   `.railwayignore`'s comment describes. No `railway up` payload can carry
   even one model.
2. The weights are proprietary trade secrets. No HuggingFace, no public model
   host. The owner deleted the HF repo specifically to stop hosting them on an
   external model registry.
3. The GitHub repo `oldboldpilot/OptionsAndFuturesCalculator` is public and
   must never hold weights.
4. Cloudflare R2 is not enabled and enabling it requires an owner dashboard
   action.
5. `gh` is authenticated (`oldboldpilot`) with scopes `gist, read:org, repo,
   workflow` — no `write:packages`.
6. `podman` 5.8.4 locally; no `docker`.
7. Railway CLI 4.35.0. Capabilities verified today, `--help` and live probes —
   see §2.
8. `railway.json`: `builder: DOCKERFILE`, `dockerfilePath: backend/Dockerfile`
   (context = repo root), `numReplicas: 2`.

### What the design must guarantee

- Checksum verification before any model byte is trusted (staged, downloaded,
  or otherwise). The Dockerfile's `model` stage already enforces this and is
  kept as-is.
- A missing model degrades to MODEL_UNAVAILABLE (already implemented:
  empty-URL builds are supported, and `start.sh` unsets a `MODEL_PATH` /
  `MORTGAGE_MODEL_PATH` naming a nonexistent file).
- **Both** replicas get the model.
- Redeploys never need manual re-seeding.
- No secret in a published image layer.

## 2. CLI capabilities, verified today

Everything below was run, not read about.

- `railway ssh "<cmd>"` **works** — commands execute in the running container
  (`railway ssh "echo remote-ok && ls /app/model/"` returned; `/app/model/`
  is empty). `-d <deployment-instance-id>` targets a specific replica
  instance.
- **stdin cannot be piped through `railway ssh`.** `printf ... | railway ssh
  "cat | wc -c"` hung until timeout in my probe; a parallel probe measured
  `printf 'hello\n' | railway ssh "cat > /tmp/probe.txt"` producing a **0-byte
  file**. Streaming 639 MB over ssh stdin is not a thing this CLI can do.
- **The runtime container has no HTTP client.** `command -v curl wget` finds
  neither (measured today). Pull-based seeding from inside the running
  container is impossible without changing the runtime image.
- `railway volume` exists (`list/add/delete/update/detach/attach`), but the
  platform constraint kills it regardless — see §3.2.
- `railway bucket` exists: `list / create / delete / info / credentials /
  rename`, with `credentials --reset`, regions `sjc | iad | ams | sin`, and
  `--json` everywhere. `railway bucket list --json` returns `[]` today — no
  bucket exists yet. Railway's docs
  (docs.railway.com/storage-buckets): buckets are **private by default**,
  **S3-compatible**, reachable over **public networking** at
  `https://storage.railway.app` (virtual-hosted style —
  `https://<bucket>.storage.railway.app/<key>`), region `auto`, billed at
  $0.015/GB-month with **free, unlimited egress and API operations**. Public
  buckets are not supported at all — there is no way to accidentally make the
  weights world-readable.
- `railway deployment list --json` enumerates deployments;
  `railway logs --build <deployment-id>` fetches the build log; `railway
  redeploy` re-runs the latest deployment.

## 3. Options considered

### 3.1 `railway up` build context — direct or chunked-context reassembly

Dead on arithmetic. The 413 is on the whole upload payload: 619 MB failed,
321 MB (context alone) passed. Chunking a GGUF into `*.part` files that dodge
the `**/*.gguf` ignore does not change the sum: 321 MB context + 639 MB of
chunks ≈ 960 MB for **one** model, ~1.6 GB for both — both far past a cap two
uploads have already hit. There is no chunk size that makes the total smaller.

The `!backend/models/*.gguf` exceptions added to `.railwayignore` and the root
`.dockerignore` today were this attempt. The staged-context branch in the
Dockerfile's `model` stage is still valuable for **local podman builds** (it
verifies the staged file against the same checksum), but the `.railwayignore`
exception is now a landmine: a model left staged in `backend/models/` makes
every subsequent `railway up` fail with 413. §5 step 7 removes it.

### 3.2 Railway volume + ssh seeding

Dead three independent ways:

1. **Volumes are incompatible with replicas.** Railway's own documentation
   (docs.railway.com/reference/volumes): *"Replicas cannot be used with
   volumes"* and *"Each service can only have a single volume."* Attaching a
   volume to `options-calculator-backend` means giving up `numReplicas: 2`.
   That alone disqualifies the entire volume family — the question "does the
   volume attach to both replicas?" has the answer "there are no replicas once
   a volume is attached."
2. **Seeding cannot happen over ssh.** stdin does not survive `railway ssh`
   (0-byte file, measured), and the runtime container has no curl/wget to pull
   the bytes itself.
3. Even if 1 and 2 were solved, seeding is an out-of-band manual step that a
   fresh environment (or a volume wipe) silently lacks — exactly the
   "redeploys must not require re-seeding" failure this design must exclude.

### 3.3 Cloudflare R2

Mechanically fine (S3-compatible, private, would slot into the same build-time
fetch as §4), but it requires the owner to enable R2 in the Cloudflare
dashboard first — the API refuses today. A design that needs a manual owner
action before it can be executed is strictly worse than one that does not,
and §3.6 provides the same properties with zero dashboard involvement. Not
chosen; not needed even as fallback (§3.5 is a better fallback).

### 3.4 Private registry base image (ghcr.io)

Bake the weights into a private base image (`FROM ghcr.io/oldboldpilot/...`)
that the Railway build extends. Dead for this account today:

- Pushing to ghcr.io needs `write:packages`, which the current `gh` token
  lacks; acquiring it is an interactive `gh auth refresh` browser round trip —
  an owner action, the same demerit as R2.
- Railway's builder would need registry credentials to pull the private base
  image on **every** build. Whether the Metal builder supports authenticated
  `FROM` pulls at all is undocumented and was not verifiable from here;
  betting the deploy path on it is a research project, not a design.
- The weights would sit in a registry layer on a third-party host — the same
  externalization the owner just deleted the HF repo to end.

### 3.5 Private GitHub release asset (the fallback, not the choice)

A **new** private repo (say `oldboldpilot/ofc-model-weights`), one release,
both GGUFs as assets. Evaluated properly:

- **Size:** fits. GitHub's documented limit is *"each file included in a
  release must be under 2 GiB"*, with no total-size or bandwidth limit.
  639 MB per asset is comfortable.
- **Upload:** works today with existing credentials — `repo` scope covers
  private-repo creation and `gh release upload`.
- **Download shape:** for a private repo the browser URL does not work with a
  bare token. The build must GET
  `https://api.github.com/repos/<owner>/<repo>/releases/assets/<asset_id>`
  with `Accept: application/octet-stream` and `Authorization: Bearer <token>`,
  and follow the 302 to `objects.githubusercontent.com`. **The existing wget
  branch cannot do this**: wget re-sends the `Authorization` header on the
  cross-host redirect, and the signed storage URL rejects requests carrying
  two authentication mechanisms. curl ≥ 7.58 strips `Authorization` on
  cross-host redirects by default, so the fetch must move to curl. (Documented
  curl/wget behavior; not measured against a live private asset today — no
  weights repo was created during this analysis.)
- **The token is the real problem.** The build needs a long-lived token with
  read access to that private repo, stored as a Railway variable. The choices:
  - The existing `gho_...` CLI OAuth token has classic `repo` scope — i.e.
    **read/write to every repo the owner has**, including the public one.
    Parking that in Railway service variables (which are also visible in the
    runtime container's environment) is a much bigger credential than the job
    needs, and it dies whenever `gh auth` is refreshed or logged out —
    breaking builds at a distance, silently, later.
  - A fine-grained PAT scoped to the one repo is the correct credential — and
    minting one is a GitHub **web UI** action. That is the same owner-action
    demerit as R2.
- Weights would live on a third-party host again, which the owner has already
  vetoed once by deleting the HF repo.

Verdict: viable, and the best fallback if Railway buckets ever disappear or
misbehave — but second place on credential hygiene, on owner-action count,
and on data-sovereignty grounds.

### 3.6 Railway bucket, fetched at image build time — **chosen**

Create a bucket **in the same project and environment as the service**, upload
both GGUFs once over the S3 API (no size deadline — this is a plain S3 PUT,
not `railway up`), and have the Dockerfile's existing `model` stage fetch them
at build time with SigV4 auth, verified against the already-pinned checksums.

Why it wins on every axis this problem has:

- **No owner dashboard action.** `railway bucket create` and `railway bucket
  credentials` are CLI calls under the already-authenticated Railway login.
- **No third-party host.** The bytes live inside the same Railway project
  that already runs them in RAM. The trust boundary does not move.
- **Private by construction.** Buckets are private-only; Railway does not
  even support public buckets.
- **Purpose-scoped, rotatable credential.** The S3 keypair opens exactly one
  bucket containing exactly these weights — nothing else. `railway bucket
  credentials --reset` rotates it in one command. Contrast with a `repo`-wide
  GitHub token.
- **No new upload ceiling.** S3 single PUT is good to 5 GB; 639 MB at the
  measured 8.35 MB/s upstream is ~77 s per model, once.
- **Free egress** means the per-build re-download of ~1.28 GB costs nothing;
  storage is ~$0.02/month.
- **Replicas solved by construction** — see §4.1.
- **Redeploys solved by construction**: every image build re-fetches and
  re-verifies; there is no seed step to forget.
- **Minimal diff.** The Dockerfile's `model` stage, its checksum gate, its
  empty-URL mode, its staged-context mode, and `start.sh`'s dangling-path
  guard all stay exactly as they are. The change is one new fetch branch and
  one apt package (`curl`, needed because wget cannot sign SigV4) in a stage
  whose ARGs never reach a published layer.

### 3.7 Runtime fetch (start.sh downloads from the bucket at boot) — rejected variant

Same bucket, but each replica pulls at container start. Rejected because: the
runtime image has no HTTP client (would need one added, growing the runtime
attack surface); the S3 credentials would be *required* in the runtime
environment rather than incidentally visible; every replica start pays a
~1.3 GB download inside the 300 s health-check budget; and a bucket outage
turns into a runtime outage instead of a failed build. Build-time baking
converts all of those into a loud, pre-traffic build failure, which is the
failure mode this project has consistently chosen.

## 4. The design

```
[ NAS archive ]                        (once per model version)
      │  curl --aws-sigv4 PUT  (~77 s/model, then round-trip re-download + sha256)
      ▼
[ Railway bucket: ofc-model-weights ]  private, S3, same project/environment
      │  curl --aws-sigv4 GET  (every image build, model stage)
      ▼
[ backend/Dockerfile `model` stage ]   sha256sum -c against pinned MODEL_SHA256 /
      │                                MORTGAGE_MODEL_SHA256  → mismatch FAILS BUILD
      ▼
[ published image, /app/model/*.gguf ] one image
      ├──────────────► replica 1       identical bytes, no seeding
      └──────────────► replica 2
```

- `MODEL_URL` / `MORTGAGE_MODEL_URL` keep their exact meaning — they now point
  at bucket object URLs (`https://<bucket>.storage.railway.app/<file>`).
- `MODEL_SHA256` / `MORTGAGE_MODEL_SHA256` are already on Railway with the
  correct values and are untouched.
- Two new Railway variables, `MODEL_BUCKET_ACCESS_KEY_ID` and
  `MODEL_BUCKET_SECRET_ACCESS_KEY`, carry the bucket keypair into the build as
  ARGs (the same mechanism `MODEL_TOKEN` used — plain ARGs, because Railway's
  Metal builder rejects any `--mount=type=secret` silently, as this repo has
  already paid five deployments to learn). One keypair covers both objects
  because both live in the one bucket — the MODEL_TOKEN/MORTGAGE_MODEL_TOKEN
  scope ambiguity that cost a documented 401-hunt cannot recur here: bucket
  credentials are bucket-scoped by construction.
- The now-dead `MODEL_TOKEN` / `MORTGAGE_MODEL_TOKEN` variables (HF bearer
  tokens for a deleted repo) should be deleted from Railway.

### 4.1 The replica question, answered

The model is baked into the image during build, and both replicas of a Railway
service run **the same image**. There is no per-replica state, no seeding, no
volume. A volume could never have done this: Railway documents *"Replicas
cannot be used with volumes"* — the volume family forces `numReplicas: 1` and
was disqualified outright (§3.2). Image baking is the only mechanism on this
platform that satisfies `numReplicas: 2` by construction, and it also survives
replica count changes (3 replicas, 10 — same image, same bytes).

### 4.2 Secret handling

- The keypair enters only the `model` stage as ARGs. The runtime stage takes
  `COPY --from=model /model/` and nothing else, so — as the Dockerfile already
  documents for `MODEL_TOKEN` — intermediate-stage ARG values never reach the
  published image's layers or history.
- The fetch commands do not echo the secret and the RUN uses `set -eu`
  (never `set -x`).
- Honest caveat: Railway exposes service variables to the **runtime
  environment** too, so the keypair is readable by anyone who can exec into
  the container or read service variables. Its blast radius is one bucket
  holding weights the container already carries on disk; rotation is
  `railway bucket credentials --reset` plus updating the two variables.

## 5. Procedure (exact, ordered, copy-pasteable)

Executor prerequisites: this repo checked out, `railway` CLI linked to
`fearless-amazement` / `production` / `options-calculator-backend` (verify
with `railway status`), NAS mounted at `/home/muyiwa/PrimaryNAS`.

### Step 0 — verify the artifacts before moving them

```bash
cd /home/muyiwa/Development/OptionsAndFuturesCalculator

sha256sum /home/muyiwa/PrimaryNAS/DataFolder/model_checkpoints/param-agent-qlora-v2-Q8_0.gguf
# MUST print eab97cf531b0c3746e366afafaaf74f90bec2d9263f269cd66018605223d80ac
sha256sum /home/muyiwa/PrimaryNAS/DataFolder/model_checkpoints/mortgagefv-assistant-q8_0.gguf
# MUST print 31ed8f45b3fca52cf46d99ccecc65e9a7210b736cbc39ae043270be40c5630a5
```

If either differs, stop: the archive is corrupt and nothing downstream is
worth doing. Also confirm the pinned values on Railway match:

```bash
railway variables --service options-calculator-backend --kv | grep -E 'MODEL_SHA256|MORTGAGE_MODEL_SHA256'
```

### Step 1 — create the bucket and capture credentials

```bash
railway bucket create ofc-model-weights --region iad --json
railway bucket credentials --bucket ofc-model-weights --json
```

The credentials output supplies: `ACCESS_KEY_ID`, `SECRET_ACCESS_KEY`, the
globally-unique bucket name (e.g. `ofc-model-weights-<hash>`), the region, and
the endpoint (`https://storage.railway.app`, virtual-hosted style per current
docs — **read the actual output; do not assume this document over what the CLI
prints**, and note older buckets are documented as path-style). Export for the
following steps:

```bash
export B_KEY='<ACCESS_KEY_ID>' B_SECRET='<SECRET_ACCESS_KEY>'
export B_NAME='<globally-unique-bucket-name>' B_REGION='<region from output, likely auto>'
export B_HOST="https://${B_NAME}.storage.railway.app"
```

### Step 2 — upload both models (plain S3 PUT; no railway up, no deadline)

```bash
curl -fsS --retry 3 \
     --aws-sigv4 "aws:amz:${B_REGION}:s3" --user "${B_KEY}:${B_SECRET}" \
     --upload-file /home/muyiwa/PrimaryNAS/DataFolder/model_checkpoints/param-agent-qlora-v2-Q8_0.gguf \
     "${B_HOST}/param-agent-qlora-v2-Q8_0.gguf"

curl -fsS --retry 3 \
     --aws-sigv4 "aws:amz:${B_REGION}:s3" --user "${B_KEY}:${B_SECRET}" \
     --upload-file /home/muyiwa/PrimaryNAS/DataFolder/model_checkpoints/mortgagefv-assistant-q8_0.gguf \
     "${B_HOST}/mortgagefv-assistant-q8_0.gguf"
```

(~77 s each at the measured 8.35 MB/s. Local curl is 8.15.0; `--aws-sigv4`
needs ≥ 7.75. If SigV4 with the reported region is rejected, retry with the
literal region string the credentials output named — and `rclone` is installed
locally as the fallback uploader:
`rclone copyto <file> ":s3,provider=Other,endpoint=https://storage.railway.app,access_key_id=${B_KEY},secret_access_key=${B_SECRET},region=${B_REGION},force_path_style=false:${B_NAME}/<file>"`.)

### Step 3 — round-trip verification (the upload is not trusted)

Per this repo's own standing rule (`docs/MORTGAGE_MODEL_DISTRIBUTION.md` step
2): re-download and re-checksum; the file measured and the file served must be
provably the same bytes.

```bash
S=/tmp/claude-verify; mkdir -p $S
curl -fsS --aws-sigv4 "aws:amz:${B_REGION}:s3" --user "${B_KEY}:${B_SECRET}" \
     -o $S/strategy.gguf "${B_HOST}/param-agent-qlora-v2-Q8_0.gguf"
sha256sum $S/strategy.gguf   # MUST be eab97cf5...23d80ac
curl -fsS --aws-sigv4 "aws:amz:${B_REGION}:s3" --user "${B_KEY}:${B_SECRET}" \
     -o $S/mortgage.gguf "${B_HOST}/mortgagefv-assistant-q8_0.gguf"
sha256sum $S/mortgage.gguf   # MUST be 31ed8f45...0c5630a5
rm -rf $S
```

A mismatch means the upload is the problem — re-upload; never "correct" the
checksum to match what the bucket serves.

### Step 4 — set Railway variables (no deploy yet)

```bash
railway variables --service options-calculator-backend --skip-deploys \
  --set "MODEL_URL=${B_HOST}/param-agent-qlora-v2-Q8_0.gguf" \
  --set "MORTGAGE_MODEL_URL=${B_HOST}/mortgagefv-assistant-q8_0.gguf" \
  --set "MODEL_BUCKET_ACCESS_KEY_ID=${B_KEY}" \
  --set "MODEL_BUCKET_SECRET_ACCESS_KEY=${B_SECRET}"
```

`MODEL_SHA256` and `MORTGAGE_MODEL_SHA256` are already correct — do not touch
them. Delete the dead HF tokens (`MODEL_TOKEN`, `MORTGAGE_MODEL_TOKEN`) via
the variables UI or `railway variables --unset` if the CLI version supports it.
Also update the stale `MODEL_URL` in the local `config/.env` (line 218) to the
bucket URL so local tooling stops pointing at the deleted HF repo.

### Step 5 — Dockerfile edits (backend/Dockerfile, `model` stage only)

Three edits. No other stage changes; `start.sh` needs **no** change (its
dangling-path guard already covers every no-model mode).

**5a.** Add `curl` to the model stage's apt line (wget cannot sign SigV4):

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget curl \
    && rm -rf /var/lib/apt/lists/*
```

**5b.** After `ARG MORTGAGE_MODEL_TOKEN`, declare the keypair (with a comment
in the file's house style explaining that one keypair covers both objects
because both live in one bucket, and that these are ARGs — never
`--mount=type=secret` — for the Metal-builder reason already documented above
them):

```dockerfile
ARG MODEL_BUCKET_ACCESS_KEY_ID
ARG MODEL_BUCKET_SECRET_ACCESS_KEY
```

**5c.** In **each** of the two fetch RUNs, insert a SigV4 branch between the
"URL is set, checksum present" check and the existing bearer-token wget logic.
The two RUNs stay parallel-to-the-point-of-near-duplication, per the stage's
own documented rule. For the strategy model, the download portion of the
`else` branch becomes:

```sh
if [ -n "${MODEL_BUCKET_ACCESS_KEY_ID:-}" ] && [ -n "${MODEL_BUCKET_SECRET_ACCESS_KEY:-}" ]; then \
    echo "Fetching via S3 SigV4 (Railway bucket)."; \
    curl -fsSL --retry 3 --connect-timeout 60 \
         --aws-sigv4 "aws:amz:auto:s3" \
         --user "${MODEL_BUCKET_ACCESS_KEY_ID}:${MODEL_BUCKET_SECRET_ACCESS_KEY}" \
         -o /model/strategy-assistant.gguf "${MODEL_URL}"; \
else \
    ... existing AUTH_HEADER / wget logic, unchanged ... \
fi || { \
    echo "FATAL: could not fetch MODEL_URL." >&2; \
    echo "If MODEL_URL is a Railway bucket object, check MODEL_BUCKET_ACCESS_KEY_ID / MODEL_BUCKET_SECRET_ACCESS_KEY (rotated by 'railway bucket credentials --reset'?) and that the bucket exists in THIS environment." >&2; \
    exit 1; \
}; \
```

The mortgage RUN gets the identical branch with
`/model/mortgage-assistant.gguf` and `MORTGAGE_MODEL_URL`, reusing the **same**
two keypair ARGs (deliberately — one bucket, one credential; put that in its
comment so nobody reintroduces a second token variable out of symmetry). The
region string `auto` must match what Step 1's credentials output reported; if
it reported something else, use that literal.

**Everything downstream of the download is untouched**: the
`sha256sum -c` gate, the staged-context precedence branch, the empty-URL
supported build, the `COPY --from=model`, the unconditional `ENV MODEL_PATH` /
`MORTGAGE_MODEL_PATH`.

### Step 6 — local validation build before touching Railway

The `model` stage can be exercised alone, with podman, without building the
40-minute C++ stages:

```bash
cd /home/muyiwa/Development/OptionsAndFuturesCalculator
podman build --target model -f backend/Dockerfile \
  --build-arg MODEL_URL="${B_HOST}/param-agent-qlora-v2-Q8_0.gguf" \
  --build-arg MODEL_SHA256=eab97cf531b0c3746e366afafaaf74f90bec2d9263f269cd66018605223d80ac \
  --build-arg MORTGAGE_MODEL_URL="${B_HOST}/mortgagefv-assistant-q8_0.gguf" \
  --build-arg MORTGAGE_MODEL_SHA256=31ed8f45b3fca52cf46d99ccecc65e9a7210b736cbc39ae043270be40c5630a5 \
  --build-arg MODEL_BUCKET_ACCESS_KEY_ID="${B_KEY}" \
  --build-arg MODEL_BUCKET_SECRET_ACCESS_KEY="${B_SECRET}" \
  .
```

Required output: `Model verified against MODEL_SHA256.` **and** `Mortgage
model verified against MORTGAGE_MODEL_SHA256.` Do not proceed to Step 7 until
both appear. (Remember the standing caveat: a local build passing does not
prove the Railway builder will — the type=secret rejection was
Railway-specific — but a local build failing proves the branch is wrong
cheaply.)

### Step 7 — remove the `railway up` landmine

In `.railwayignore`, delete the exception block ending in
`!backend/models/*.gguf` (keep the `**/*.gguf` rule and its comment). This
restores the invariant that no GGUF can ever ride the upload — with the
exception in place, a model staged for a local build makes every deploy 413.
**Keep** the matching exception in the root `.dockerignore`: it is what lets
the staged-context branch keep working for local podman builds, and Railway
never reads that file's exception because the staged file never reaches its
context. Update the two files' comments to say the models arrive from the
Railway bucket at build time (this document is the reference).

### Step 8 — deploy and verify like the repo's own docs demand

```bash
railway up --detach --service options-calculator-backend
```

Then gate on evidence, not exit codes (`railway up` has exited 0 on builds
that never started):

```bash
# 1. The deployment actually built and is SUCCESS:
railway deployment list --limit 3

# 2. The build log carries BOTH verification lines:
railway logs --build <new-deployment-id> | grep -E "verified against (MODEL_SHA256|MORTGAGE_MODEL_SHA256)"

# 3. The bytes in the running container are the pinned bytes:
railway ssh "sha256sum /app/model/strategy-assistant.gguf /app/model/mortgage-assistant.gguf"
#   MUST print eab97cf5...23d80ac and 31ed8f45...0c5630a5

# 4. BOTH replicas: repeat 3 against each instance id
#    (railway ssh -d <deployment-instance-id>; ids via the dashboard or
#    `railway deployment list --json`). Same image → same bytes, but the check
#    costs one command per replica and closes the question with measurement.

# 5. The service answers: probe the strategy assistant over gRPC-Web (native
#    gRPC does not survive the ingress) with a Pro licence via
#    scripts/mint_pro_gate_creds.mjs — an unauthenticated probe returns
#    grpc-status 7 and looks exactly like a broken model.
```

Also watch resident memory after both models load (~640 MB each plus KV): the
empty-URL build remains the supported fallback if the two do not fit.

### Step 9 — documentation follow-ups (same change set)

- `docs/STRATEGY_ASSISTANT_PIPELINE.md` §4 and
  `docs/MORTGAGE_MODEL_DISTRIBUTION.md`: replace the HF publish/pin procedure
  with the bucket upload + round-trip procedure above (steps 1–4).
- `CLAUDE.md`'s strategy-assistant section: one line noting weights are served
  to builds from the project's Railway bucket, referencing this spec.
- `MANIFEST.md` on the NAS: note the bucket as the production distribution
  channel; the NAS remains the archive of record.

## 6. Failure modes and their detection

| # | failure | effect | detected by |
| --- | --- | --- | --- |
| 1 | Bucket credentials rotated (`--reset`) without updating the two Railway variables | fetch gets 403 → **build fails loudly** (`FATAL: could not fetch`), previous image keeps serving | `railway deployment list` shows FAILED; build log names the fetch and points at the keys |
| 2 | Bucket or object deleted | same as 1 (404) | same as 1 |
| 3 | Bytes in bucket wrong/truncated/substituted | `sha256sum -c` fails → **build fails**, file removed, nothing ships | build log `FATAL: ... failed sha256 verification` |
| 4 | Railway stops passing a variable (typo'd name, deleted var) → `MODEL_URL` empty | **silent no-model build** — this is the one non-loud path, and it is deliberate (empty URL is a supported build) | Step 8 gates 2–3: the `verified against` grep and the in-container `sha256sum` are mandatory, not optional; absence of the line = model missing regardless of deploy status |
| 5 | Metal builder quirk (e.g. rejects `--aws-sigv4`-era curl, or ARG passing changes) | build fails at fetch | build log; fallback is §3.5 (GitHub release + curl bearer), which reuses the same checksum gate |
| 6 | Keypair visible in runtime env / to variable readers | credential exposure, blast radius = this one bucket | accepted and documented (§4.2); rotate with `credentials --reset` + Step 4 |
| 7 | PR/duplicated environment gets its own empty bucket instance (buckets are per-environment) | builds in that environment 404 → fail loudly | build log; per-environment answer: upload there too, or set empty URLs for a supported model-less build |
| 8 | Model staged in `backend/models/` while `.railwayignore` exception still present | `railway up` 413s | Step 7 removes the exception; if skipped, the 413 itself is the (unpleasant) detector |
| 9 | Image grows ~1.28 GB | slower image pulls/deploys, more registry storage | deploy duration in dashboard; no correctness impact — both replicas still get the bytes |
| 10 | Replica divergence | cannot happen by construction (one image), but is still **measured** per-replica in Step 8.4 rather than asserted | `railway ssh -d <instance>` sha256sum per replica |
| 11 | Both models resident exceed container memory | engine OOM at load | deploy logs / restarts; supported fallback: empty `MORTGAGE_MODEL_URL` build keeps everything else up |

## 7. What was NOT verified (stated plainly)

- **Bucket creation and its real endpoint/region strings.** No bucket exists
  yet (`railway bucket list` → `[]`) and none was created during this
  analysis. The endpoint form (`https://<bucket>.storage.railway.app`,
  virtual-hosted, region `auto`) comes from Railway's current documentation,
  not from a live `credentials` output. Step 1 explicitly instructs the
  executor to read the CLI's actual output over this document.
- **That Railway's Metal builder passes these service variables as build
  ARGs.** This is the same mechanism the previous `MODEL_URL`/`MODEL_TOKEN`
  HF fetch used and the repo's docs treat as the deployed reality, so it is
  prior evidence, not fresh measurement. Step 8's gates catch it if it has
  changed.
- **`--aws-sigv4` acceptance by Railway storage with region `auto`.** curl's
  SigV4 works against other `auto`-region S3 implementations; Steps 2/6 fail
  fast and cheaply if this one differs, and rclone is the on-disk fallback.
- **GitHub release-asset redirect behavior** (fallback §3.5): the
  wget-resends-Authorization trap and curl's cross-host header stripping are
  documented behavior, not measured today against a live private asset.
- **The exact 413 threshold.** Bounded by measurement (321 MB passes,
  619 MB fails); the "~500 MB" figure is an interpolation, and nothing in the
  chosen design depends on it.
- **Railway build-machine bandwidth** to the bucket (affects build duration
  only; egress is free).
