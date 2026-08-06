# Mortgage assistant model: publishing and pinning

`@author Olumuyiwa Oluwasanmi`

How the mortgage assistant's fine-tuned Qwen3-0.6B GGUF gets from the training
host into the running container, and what must be true before it is called
deployed.

> **STATUS, 2026-08-05 — the hosting half of this document is being replaced.**
>
> The private HuggingFace repository this page's "Publishing a model" section
> describes was **deleted** on 2026-08-05 at the owner's instruction: the weights
> are proprietary trade secrets and do not belong on a third-party model
> registry, private or not. Neither GGUF is in the deployed container today, and
> **no model URL is pinned** — both assistants therefore report their model
> unavailable at runtime while every other service in the image is unaffected,
> which is the supported empty-URL build this page already describes.
>
> A replacement hosting mechanism is being designed and is **not** settled here.
> Do not treat the HF procedure below as current, and do not invent a substitute
> from it. The `backend/models/` build-context staging path added to
> `backend/Dockerfile` in this same commit is a **stopgap** that keeps a locally
> held model reachable by a local `docker build`; it is not the answer for
> Railway, where `railway up` cannot carry 639 MB (see "Why a build-time fetch").
>
> What is NOT provisional, and survives whatever replaces the transport: a model
> is verified against a checksum before anything is allowed to use it, staging is
> not verification, and the checksum that counts is the one round-tripped from
> wherever the bytes are actually served.

This is a **sibling** of `STRATEGY_ASSISTANT_PIPELINE.md`, not a section of it.
That document is scoped end to end to one model —
`calculator.assistant.StrategyAssistant`'s — from its dataset generator through
its own accuracy bar and its own serving constraints; appending a second model's
distribution steps in the middle of it would make every unqualified "the model"
in the surrounding text ambiguous, and that document has already been misread
once in a way that cost a retrain (§2b). Everything here is deliberately narrow:
distribution only. The training, evaluation and serving story for this model
belongs with the service that owns it
(`docs/superpowers/specs/2026-08-05-mortgagefv-assistant-pipeline.md`).

## What ships

| | value |
| --- | --- |
| file | `mortgagefv-assistant-q8_0.gguf` |
| size | 639,447,136 bytes |
| sha256 | `31ed8f45b3fca52cf46d99ccecc65e9a7210b736cbc39ae043270be40c5630a5` |
| format | Q8_0 GGUF, Qwen3-0.6B |
| served from | `MORTGAGE_MODEL_PATH` (`/app/model/mortgage-assistant.gguf` in the image) |

That sha256 is the value `MORTGAGE_MODEL_SHA256` must carry **for this exact
file**, and it was computed against a local copy at
`backend/models/mortgagefv-assistant-q8_0.gguf`. It is not the number to trust
after an upload — see step 2 below, which exists because the local checksum and
the served checksum are two different claims.

The GGUF itself is **not in this repository and not in the deployed image**;
`**/*.gguf` is gitignored and `backend/models/` is committed empty (see its
`.gitkeep`). The checksum is recorded here so that whatever transport replaces
the deleted HF repo has a value to be checked against, rather than being
re-derived from whatever file happens to turn up later.

## Why a build-time fetch

Identical to the strategy model's reasoning
(`STRATEGY_ASSISTANT_PIPELINE.md` §4), and it applies with no adjustment:

- `railway up` enforces an upload deadline that a **62 MB** payload already
  failed, four consecutive times, at ~2.4 MB/s upstream. This file is ten times
  that. It cannot travel that way, ever. The **repository-root**
  `.railwayignore` and `.dockerignore` both exclude `**/*.gguf` for that reason.
  `backend/.dockerignore` says the same thing and cannot enforce it: the build
  context is the repository root (`railway.json` sets `dockerfilePath:
  backend/Dockerfile`), so only the root file is read. This was verified, not
  reasoned about — with the root entry removed, a `COPY backend/models` picked
  up all 639,447,136 bytes; with it, the directory arrives empty.

  **Both files now carry a deliberate `!backend/models/*.gguf` exception that
  re-admits a staged model, and it is the stopgap the banner above names.** It
  makes a local `docker build` work from a locally held GGUF now that the fetch
  URL is gone. It does NOT make `railway up` work — the deadline is a property
  of the upload, not of the ignore file, so a staged model on that path fails
  exactly as it always did. Whoever settles the replacement hosting mechanism
  should expect to remove this exception, not build on it.
- The model is therefore fetched during `docker build`, in `backend/Dockerfile`'s
  `model` stage, from `MORTGAGE_MODEL_URL`, and verified against
  `MORTGAGE_MODEL_SHA256` before anything is allowed to use it.
- The token is a plain build **ARG**, never `--mount=type=secret`. Railway's
  Metal builder rejects the entire Dockerfile if a secret mount appears anywhere
  in it, and does so silently: the build never starts, the previous container
  keeps serving, and `railway up` still exits 0. A local `docker build` cannot
  reproduce this — local BuildKit supports secret mounts fine. The ARG is safe
  here because the fetch is isolated in the `model` stage and the runtime stage
  takes only `COPY --from=model`, so the ARG's value never reaches a published
  layer.

## Build variables

| variable | meaning |
| --- | --- |
| `MORTGAGE_MODEL_URL` | direct download URL for the GGUF. **Empty is a supported build**: the image builds, the calculator, finance and strategy-assistant services are unaffected, and the mortgage assistant reports its model unavailable instead of answering. |
| `MORTGAGE_MODEL_SHA256` | sha256 of the bytes that URL actually serves. Set without it, `MORTGAGE_MODEL_URL` **fails the build** — deliberately, loudly, before the download is used for anything. |
| `MORTGAGE_MODEL_TOKEN` | optional. Only needed if the token that already authenticates the strategy model's repo does **not** cover this one. |

**On the token.** Nothing in this repository records the scope of `MODEL_TOKEN`,
and the two possibilities behave differently: a classic HuggingFace read token is
account-wide and already covers a second repo, while a fine-grained token can be
scoped to a single repo, in which case reusing it here produces a 401 that reads
exactly like a wrong URL. The Dockerfile therefore does not assume — it uses
`MORTGAGE_MODEL_TOKEN` if supplied and falls back to `MODEL_TOKEN` if not. If the
token in use is account-scoped, set nothing new. If a build fails with
`FATAL: could not fetch MORTGAGE_MODEL_URL`, a per-repo token is the first thing
to suspect, and `MORTGAGE_MODEL_TOKEN` is where it goes.

`MORTGAGE_MODEL_URL` and `MORTGAGE_MODEL_SHA256` **move together** on every swap.
Changing the URL alone fails on the checksum; changing the checksum alone fetches
the old file and fails the same way. Both are Railway build variables, so a model
swap needs no code change and no commit.

## Publishing a model

**Superseded — see the status banner at the top of this page.** The private HF
repository this procedure targets was deleted on 2026-08-05 and the mortgage
GGUF was never uploaded to it. The steps are kept only because step 0 and step 2
express the invariant any replacement must also satisfy: **the file you MEASURED
and the file that is SERVED have to be provably the same bytes, and an upload is
a place they can stop being.** Read them for that; do not follow them as-is.

```bash
# 0. Checksum what you are about to upload, from the file you are about to
#    upload. Not from a note, not from this document.
sha256sum backend/models/mortgagefv-assistant-q8_0.gguf

# 1. Publish to the private HF repo. HF_TOKEN (config/.env) needs the `write`
#    role. Confirm the repo id first -- the mortgage GGUF has NOT been uploaded
#    yet as of this writing, so no repo id here is verified; it is a sibling of
#    olumuyiwaoluwasanmi/options-param-agent-qwen3-0.6b, not that repo itself.
python -c "
from huggingface_hub import HfApi
HfApi(token='<HF_TOKEN>').upload_file(
    path_or_fileobj='backend/models/mortgagefv-assistant-q8_0.gguf',
    path_in_repo='mortgagefv-assistant-q8_0.gguf',
    repo_id='<owner>/<mortgage-repo>',
    repo_type='model')"

# 2. RE-DOWNLOAD AND RE-CHECKSUM. Do not assume the upload was faithful.
#    scripts/convert_to_gguf.sh prints this same instruction at the end of every
#    conversion, and it is not ceremony: the file MEASURED and the file SERVED
#    must be provably the same bytes, and an upload is a place they can stop
#    being. A truncated or re-encoded GGUF still loads and then answers mortgage
#    questions fluently and wrongly -- there is no crash to notice.
curl -sSL -H "Authorization: Bearer <HF_TOKEN>" \
  https://huggingface.co/<owner>/<mortgage-repo>/resolve/main/mortgagefv-assistant-q8_0.gguf \
  -o /tmp/verify.gguf && sha256sum /tmp/verify.gguf
#    This number -- the round-tripped one -- is what MORTGAGE_MODEL_SHA256 gets.
#    If it differs from step 0, the upload is the problem; re-upload, do not
#    "correct" the checksum to match what the hub happens to be serving.

# 3. Pin and redeploy.
railway variables --service options-calculator-backend \
  --set 'MORTGAGE_MODEL_URL=https://huggingface.co/<owner>/<mortgage-repo>/resolve/main/mortgagefv-assistant-q8_0.gguf' \
  --set 'MORTGAGE_MODEL_SHA256=<sha from step 2>' --skip-deploys
railway up --detach --service options-calculator-backend
```

## After deploying

- **Check the build log, not the CLI's exit code.** `railway up` exits 0 on
  builds that never started. The line to look for is
  `Mortgage model verified against MORTGAGE_MODEL_SHA256.`; its absence means the
  container is running without the model regardless of what the deploy said.
- **Compare `sha256sum` against the pinned value before calling anything "the
  deployed model."** The options project spent an entire session measuring a file
  production was not serving.
- **Verify through gRPC-Web.** Native gRPC does not survive the Railway ingress —
  it fails with `Stream removed` and no request reaches the container at all.
- **Watch resident memory.** A second Q8_0 model is roughly another 640 MB
  resident plus its KV cache, in a container that already holds one. If the two
  do not fit, the empty-`MORTGAGE_MODEL_URL` build is a real, supported fallback
  that keeps every other service up.
