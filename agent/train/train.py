#!/usr/bin/env python3
"""
Fine-tunes Qwen3-0.6B to turn chat into calculator parameters.

THE RECIPE THAT ACTUALLY SHIPPED, AND WHY (read this before changing
--method's default): `--method qlora` -- a 4-bit frozen base with LoRA
adapters trained on top, per get_peft_model() below -- not the full
fine-tune with int8 QAT this file's docstring used to recommend (that
notebook-derived recipe is `--method full`, kept only as a documented
alternative; see its own comments further down for what it does and why it
is NOT the default).

Both were run to completion on the SAME clean dataset and evaluated the same
way, and the gap is not close:

    --method qlora   rank 16, alpha 16, all 7 projections   95.0% params exact-match
    --method full    int8 QAT, every one of 398M params      49.8% params exact-match

On a 0.6B model, updating all 398M parameters (`--method full`) damaged the
pretrained representations faster than it learned this task -- a five-field
JSON extraction problem does not need, and cannot survive, that much of the
base model moving. QLoRA's frozen 4-bit base plus a small trained adapter
(2.5M-ish params, see the "trainable" print below) leaves the pretrained
representations alone and only ever moves the part that has to change. THE
BAR FOR ANY FUTURE RUN IS 95.0% PARAMS EXACT-MATCH ON THE HELD-OUT SET
(agent/dataset/data/val.jsonl), evaluated on the same Q8_0 GGUF this pipeline
exports (see EXPORT below) -- not on these bf16 merged weights, which are an
intermediate artifact, not what a trader's request is ever actually decoded
by. A run that lands materially below 95.0% is a regression, full stop, no
matter what else about it looks better (faster, smaller loss, more data).
Per-field accuracy at that baseline, same held-out set: symbol 98.7%,
asset_class 99.0%, strategy 97.0%, expiration_days 100%, quantity 100%. Use
these to tell "regressed" apart from "just different" -- 80% overall with
symbol still near 99% but strategy collapsed to 60% is a different failure
than a uniform drop, and the fix is not the same.

THIS RUN, budgeted: ~2056 steps, ~22 minutes wall clock, ~1.97 GB peak GPU
memory on the training host's smaller card. That is the number to hold a new
run against too -- augmenting the dataset (agent/dataset/build_dataset.py) is
expected and fine, but a run that grows to take materially longer than ~30
minutes total means something about the recipe changed, not just the data,
and is worth stopping to explain rather than shipping quietly.

THE FULL CHAIN THIS FILE IS ONE STAGE OF -- train (this file, 4-bit QLoRA) ->
merge (save_pretrained_merged below, 4-bit adapters folded into bf16 weights)
-> quantize (scripts/convert_to_gguf.sh, sensen's own converter, bf16 -> Q8_0
GGUF in one step, ~639 MB) ->
distribute (private HF repo, checksum pinned, fetched at Docker build time,
never through `railway up`) -> serve (sensen's in-process LLMPipeline, Q8_0
weights, q8 KV cache, CPU) -- is documented end to end, including the parts
that are NOT visible from this file (the training system prompt, the
Dockerfile's secret-mount trap, why `n_gpu_layers=0` is load-bearing, the
four-turn clarification shape the backend's prompt builder depends on) in
docs/STRATEGY_ASSISTANT_PIPELINE.md. Read that before touching anything
downstream of this script; the constraints it documents were each learned by
breaking production once, and this file only covers the training stage's own
slice of them.

    python3 train.py --method qlora --data ../dataset/data --out /scratch/agents/param-agent-qlora
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

# Must precede transformers/torch imports for the patches to apply.
from unsloth import FastLanguageModel
from unsloth.chat_templates import get_chat_template

import torch
from datasets import Dataset
from trl import SFTConfig, SFTTrainer

MODEL_ID = "unsloth/Qwen3-0.6B"
QAT_SCHEME = "int8"


def load_jsonl(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]



# ---------------------------------------------------------------------------
# Vocabulary extension
# ---------------------------------------------------------------------------
def closing_cost_vocab(train_path: str) -> list[str]:
    """The identifiers a ComputeClosingCosts answer must reproduce verbatim.

    Derived from the DATA rather than hardcoded, so it cannot drift from the
    proto the dataset was generated against: every key the generator emits for
    that operation, plus the operation id itself.
    """
    import json, re

    found: set[str] = set()
    with open(train_path) as fh:
        for line in fh:
            # Parse the ROW first: the file is JSON, so a naive regex over the
            # raw line matches escaped text and yields keys with backslashes in
            # them -- tokens that would then be added to the vocabulary and
            # never appear in a real answer.
            try:
                row = json.loads(line)
            except Exception:
                continue
            for turn in row.get("conversations", []):
                if turn.get("role") != "assistant":
                    continue
                m = re.search(r"<params>(.*?)</params>", turn.get("content", ""), re.S)
                if not m:
                    continue
                try:
                    obj = json.loads(m.group(1))
                except Exception:
                    continue
                if obj.get("operation") != "ComputeClosingCosts":
                    continue
                found.add("ComputeClosingCosts")
                found.update(k for k in obj if k != "operation")
    return sorted(found)


def extend_vocabulary(model, tokenizer, tokens: list[str]) -> tuple[int, int]:
    """Add `tokens` as single tokens and seed each from its own sub-tokens.

    Two things matter here and neither is default behaviour.

    ONE: a token added to the tokenizer gets a FRESH row in the embedding
    matrix, and a randomly initialised row is noise the model must unlearn
    before it can use the token at all. Each new row is initialised to the MEAN
    of the embeddings of the sub-tokens the string used to decompose into, so
    `homeowners_insurance_annual` starts life pointing where
    ['home','owners','_ins','urance','_ann','ual'] already pointed. It begins
    approximately right instead of at noise.

    TWO: the same is done for the OUTPUT side (`lm_head`) when it is untied.
    Seeding the input embedding alone leaves the model able to read the token
    and unable to write it, which on structured output is the half that matters.
    """
    import torch as _torch

    fresh = [t for t in tokens if len(tokenizer(t, add_special_tokens=False)["input_ids"]) > 1]
    if not fresh:
        # Nothing to add -- every identifier is already a single token. Report a
        # boundary anyway so the caller can unpack unconditionally; with 0 added
        # rows the freeze mask is never installed, so the value is inert.
        return 0, len(tokenizer)

    # Capture the OLD decomposition before the tokenizer learns the new tokens,
    # otherwise each string now tokenizes to itself and the mean is undefined.
    decomposition = {
        t: tokenizer(t, add_special_tokens=False)["input_ids"] for t in fresh
    }

    # The row index the new tokens start at, captured BEFORE they are added.
    # This is the freeze boundary: everything below it is pre-existing and
    # must not move, everything at or above it is new and must train.
    first_new_row = len(tokenizer)
    added = tokenizer.add_tokens(fresh)

    # Resize ONLY to grow, never to shrink -- and here it does not need to grow
    # at all. Qwen3-0.6B ships 151936 embedding rows against a tokenizer of
    # ~151669: the surplus is reserved padding, so seventeen new ids land at
    # 151669..151685, inside the matrix that already exists.
    #
    # Calling resize_token_embeddings(len(tokenizer)) unconditionally SHRINKS
    # it to 151686 and discards 250 unused rows. That looks harmless and is
    # not: PEFT builds the lm_head adapter from the config's vocab_size, so the
    # base output becomes 151686 while lora_B stays 151936 and the first full
    # logits computation dies with
    #   "The size of tensor a (151686) must match the size of tensor b (151936)".
    # Training survived 179 steps before hitting it, because packed training
    # never materialises full logits and the first evaluation does.
    #
    # Leaving the matrix alone also keeps the exported GGUF's vocab at 151936,
    # so the serving artifact is unchanged in shape -- only its contents move.
    rows_before = model.get_input_embeddings().weight.shape[0]
    current_rows = rows_before
    grew = len(tokenizer) > current_rows
    if grew:
        model.resize_token_embeddings(len(tokenizer))
        current_rows = model.get_input_embeddings().weight.shape[0]

    embed = model.get_input_embeddings().weight
    head = model.get_output_embeddings()
    head_w = head.weight if head is not None else None
    tied = head_w is not None and head_w.data_ptr() == embed.data_ptr()

    with _torch.no_grad():
        for tokstr, pieces in decomposition.items():
            new_id = tokenizer.convert_tokens_to_ids(tokstr)
            src = _torch.tensor(pieces, device=embed.device)
            embed[new_id] = embed[src].mean(dim=0).to(embed.dtype)
            if head_w is not None and not tied:
                head_w[new_id] = head_w[src].mean(dim=0).to(head_w.dtype)

    # Say which of the three states the matrix is in, not two. `current_rows ==
    # len(tokenizer)` is TRUE both when the matrix was grown to fit and when it
    # already fitted exactly, so reporting "grown" on that test alone claims a
    # resize that may never have happened -- and whether the resize ran is the
    # one fact this line exists to record.
    if grew:
        fit = f"grown from {rows_before} to {current_rows}"
    elif current_rows == len(tokenizer):
        fit = "unchanged -- already an exact fit"
    else:
        fit = f"unchanged -- {current_rows - len(tokenizer)} reserved row(s) still spare"
    print(
        f"vocab: added {added} token(s), seeded from sub-token means "
        f"(lm_head {'tied' if tied else 'seeded separately'}); "
        f"tokenizer now {len(tokenizer)}, embedding rows {current_rows} ({fit})"
    )
    return added, first_new_row


def _patch_merged_embedding(out_dir: Path, model) -> None:
    """Write the LIVE embedding matrix into the merged export, and verify it.

    See the call site for why this is necessary. The verification is the point:
    a patch that silently no-ops leaves exactly the artifact it was written to
    prevent, and the failure is invisible because the tokenizer still carries
    the new tokens.

    lm_head is tied on this checkpoint and is therefore absent from the
    exported state dict entirely (`[k for k in sd if "lm_head" in k] == []`),
    so patching the input embedding fixes the read side and the write side
    together. If a future checkpoint untied them, extend_vocabulary already
    refuses to train it.
    """
    from safetensors.torch import load_file, save_file

    live = model.get_input_embeddings().weight.detach().to(torch.bfloat16).cpu()

    shards = sorted(out_dir.glob("model*.safetensors"))
    if not shards:
        raise RuntimeError(f"no safetensors to patch in {out_dir}")

    key = "model.embed_tokens.weight"
    for shard in shards:
        sd = load_file(str(shard))
        if key not in sd:
            continue
        before = sd[key].float()
        if tuple(before.shape) != tuple(live.shape):
            raise RuntimeError(
                f"embedding shape mismatch: file {tuple(before.shape)} vs live "
                f"{tuple(live.shape)} -- refusing to patch"
            )
        moved = (before - live.float()).abs().max().item()
        sd[key] = live
        # save_file drops metadata; safetensors for HF needs the format tag or
        # transformers refuses to load the shard.
        save_file(sd, str(shard), metadata={"format": "pt"})
        print(
            f"patched {key} into {shard.name}: max |file - live| was {moved:.3e} "
            f"(0.0 would mean the export already had it)"
        )

        check = load_file(str(shard))[key].float()
        if (check - live.float()).abs().max().item() != 0.0:
            raise RuntimeError("embedding patch did not round-trip -- refusing to continue")
        print("  verified: re-read matches the live matrix exactly")
        return

    raise RuntimeError(f"{key} not found in any shard of {out_dir}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="../dataset/data")
    ap.add_argument("--out", default="/scratch/agents/param-agent")
    ap.add_argument("--max-seq-length", type=int, default=1024)
    ap.add_argument("--epochs", type=float, default=2.0)
    # Vocabulary extension is OPT-IN and off by default, because it changes what
    # the SERVING ARTIFACT contains: the token ids an answer decodes to move,
    # and every gate in the consuming repo is defined against the current set.
    #
    # It does NOT necessarily change the vocab SIZE, and on this model it does
    # not: Qwen3-0.6B's 151936 embedding rows already exceed its ~151669-token
    # tokenizer, so the new ids land in reserved padding and the exported GGUF
    # is 151936 wide before and after. Do not read "extended vocabulary" as
    # "resized matrix" -- the resize is conditional, and skipping it is what
    # keeps the artifact's shape stable.
    ap.add_argument(
        "--extend-vocab",
        action="store_true",
        help="add the ComputeClosingCosts identifiers as single tokens and train "
             "the embeddings (LoRA on embed_tokens/lm_head). Changes which token "
             "ids an answer decodes to; grows the matrix only if it must.",
    )
    ap.add_argument("--batch-size", type=int, default=8)
    ap.add_argument("--grad-accum", type=int, default=4)
    # Default None so the method can pick its own: LoRA and full fine-tuning do
    # not want the same learning rate, and silently giving one the other's is the
    # easiest way to make a method look worse than it is.
    ap.add_argument("--lr", type=float, default=None)
    ap.add_argument("--seed", type=int, default=3407)
    ap.add_argument(
        "--method",
        choices=["qlora", "full"],
        default="qlora",
        help="qlora: 4-bit frozen base + LoRA adapters. " "full: full fine-tune with int8 QAT.",
    )
    # LoRA capacity. r=16 on a 0.6B model is generous for a task whose output is
    # a five-field JSON object; it is here to be lowered if the adapter overfits,
    # not raised.
    ap.add_argument("--lora-r", type=int, default=16)
    ap.add_argument("--lora-alpha", type=int, default=16)
    ap.add_argument("--lora-dropout", type=float, default=0.0)
    # Profiling runs want a handful of steps, not a full epoch.
    ap.add_argument("--max-steps", type=int, default=-1)
    ap.add_argument(
        "--profile",
        action="store_true",
        help="Emit a torch profiler trace and NVTX ranges for nsys.",
    )
    args = ap.parse_args()

    qlora = args.method == "qlora"
    # 2e-4 is the standard LoRA rate and ~4x the full fine-tuning rate. The
    # adapters start at zero and are the only thing being trained, so they have
    # to move much further per step than a full model whose weights are already
    # in a good place -- 5e-5 on LoRA underfits and reads as "LoRA is worse".
    if args.lr is None:
        args.lr = 2e-4 if qlora else 5e-5
    print(f"method={args.method}  lr={args.lr}")

    data = Path(args.data)
    train_rows = load_jsonl(data / "train.jsonl")
    val_rows = load_jsonl(data / "val.jsonl")
    print(f"train {len(train_rows)}  val {len(val_rows)}")

    # THREE CONSTRAINTS THAT ARE LOAD-BEARING IN PRODUCTION AND NOT VISIBLE
    # FROM ANYTHING ELSE IN THIS FILE -- each cost a debugging cycle before it
    # was written down. Full detail in docs/STRATEGY_ASSISTANT_PIPELINE.md;
    # stated here because someone editing the dataset or this script is
    # exactly the person who needs to see them before, not after, changing
    # something that depends on them.
    #
    # 1. The system prompt every row below carries (agent/dataset/
    #    build_dataset.py's SYSTEM constant) must stay byte-identical to the
    #    one backend/src/modules/assistant_service.cpp injects on every RPC
    #    call (kSystemPrompt). Retraining against a reworded prompt, however
    #    similar, produces a model tuned to a system turn production never
    #    actually sends it -- Qwen3 reverts to stock behaviour (a <think>
    #    block, no <params> block, ever) when the prompt it sees at inference
    #    doesn't match the one it was tuned against, and nothing here would
    #    catch that at training time; it only shows up as a live 0% at eval.
    # 2. build_dataset.py's make_clarification() produces a specific FOUR-TURN
    #    shape -- user request, assistant question, user reply, assistant
    #    params -- and assistant_service.cpp's build_prompt() constructs
    #    exactly that shape (system, user, assistant, user, assistant) when a
    #    caller sends a prior_clarification. The two must move together: this
    #    file does not enforce the shape, build_dataset.py's docstring on
    #    make_clarification does, and it says not to change it without
    #    changing build_prompt() in the same breath.
    # 3. Every `strategy` value in every row must be an id that exists in
    #    backend/src/modules/strategy_catalogue.cppm (generated from
    #    agent/dataset/strategies.json, the single source of truth). An id
    #    the catalogue does not know is_known() on gets refused at serve
    #    time regardless of how confidently this model learned to emit it --
    #    training on one teaches a behaviour production will never honour.

    # QLoRA: the base weights are loaded in 4-bit and FROZEN, and the only
    # trainable parameters are the low-rank adapters bolted onto the attention
    # and MLP projections. That is what makes it cheap -- no optimizer state for
    # 0.6B of weights, so the adamw_8bit moments that dominated peak memory in
    # the full run (6.12 GB) mostly disappear.
    #
    # Note this changes what "quantized" means here. The full path used QAT: the
    # quantization is simulated in the forward pass so the weights learn to
    # tolerate it, and the exported model IS the trained model. QLoRA instead
    # quantizes a base that is never updated, and the adapters train in bf16 on
    # top. For CPU deployment the adapters get merged back into a 16-bit model
    # and quantized afterwards, so the deployed weights are NOT the ones that
    # were trained. On a task this narrow that is usually fine; it is a real
    # difference and worth remembering if accuracy drops only after conversion.
    model, tokenizer = FastLanguageModel.from_pretrained(
        model_name=MODEL_ID,
        max_seq_length=args.max_seq_length,
        dtype=torch.bfloat16,
        load_in_4bit=qlora,
        full_finetuning=not qlora,
        **({} if qlora else {"qat_scheme": QAT_SCHEME}),
    )

    # Vocabulary extension happens BETWEEN loading and get_peft_model, and the
    # order is not negotiable: resizing the embedding matrix after the adapters
    # are attached leaves LoRA wrapping a matrix of the old width.
    vocab_added = 0
    tokenizer_rows_before_extension = 0
    if args.extend_vocab:
        toks = closing_cost_vocab(str(Path(args.data) / "train.jsonl"))
        if not toks:
            raise SystemExit(
                "--extend-vocab was requested but no ComputeClosingCosts rows were "
                "found in the training data. Regenerate the dataset before training; "
                "silently training the old label space is how a retrain gets "
                "attributed to the wrong recipe."
            )
        vocab_added, tokenizer_rows_before_extension = extend_vocabulary(
            model, tokenizer, toks
        )

    if qlora:
        model = FastLanguageModel.get_peft_model(
            model,
            r=args.lora_r,
            # All seven projections, not just q and v. Restricting LoRA to the
            # attention projections leaves the MLP -- where most of the
            # parameters and most of the task-specific behaviour live -- frozen.
            target_modules=[
                "q_proj",
                "k_proj",
                "v_proj",
                "o_proj",
                "gate_proj",
                "up_proj",
                "down_proj",
            ],
            # NOT "embed_tokens"/"lm_head". v3 put LoRA on both and REGRESSED
            # the untouched operations from 60.4% to 51.2% (paired McNemar
            # p = 3.5e-05, 63 rows lost against 24 gained), concentrated as
            # label blurring in the amortization cluster -- 43 of the 63
            # newly-broken rows were ComputeDetailedAmortization and
            # ComputeAmortization, and 33 of 63 named the WRONG operation.
            #
            # The mechanism is not subtle: a LoRA adapter on the embedding is a
            # low-rank update applied to the WHOLE matrix, so training it to
            # place seventeen new rows also moves all 151,669 existing ones,
            # including every operation-name embedding the label space depends
            # on. The new rows needed gradient. The rest did not.
            #
            # They are trained instead by unfreezing the embedding matrix and
            # masking the gradient to the new rows only -- see below.
            lora_alpha=args.lora_alpha,
            lora_dropout=args.lora_dropout,  # 0 is Unsloth's optimised path
            bias="none",  # ditto
            use_gradient_checkpointing="unsloth",
            random_state=args.seed,
            max_seq_length=args.max_seq_length,
            use_rslora=False,
        )

        # ------------------------------------------------------------------
        # Train ONLY the newly added embedding rows.
        # ------------------------------------------------------------------
        # get_peft_model has just frozen every base parameter, so this must run
        # AFTER it or PEFT undoes it.
        #
        # The embedding is not a 4-bit Linear -- bitsandbytes quantizes Linear
        # layers, not embeddings -- so its weight is a real bf16 tensor that can
        # take a gradient directly. Unfreezing it and zeroing the gradient for
        # every pre-existing row trains the seventeen new ones at full rank
        # while leaving the other 151,669 bit-identical. That is strictly more
        # capacity for the new tokens than a LoRA adapter gave them, and
        # strictly less disturbance to everything else, which is the whole
        # point: v3 had it backwards on both counts.
        #
        # lm_head is TIED to the input embedding on Qwen3-0.6B (verified by
        # data_ptr() equality in extend_vocabulary), so one masked tensor
        # trains the read side and the write side together. If it were ever
        # untied, the output side would need the same treatment and the assert
        # below is what would catch it.
        if vocab_added:
            emb = model.get_input_embeddings().weight
            frozen_rows = tokenizer_rows_before_extension
            emb.requires_grad_(True)

            def _freeze_existing_rows(grad, _n=frozen_rows):
                # In place, and returning the same tensor: a clone here is
                # 151936 x 1024 floats on every single backward pass.
                grad[:_n].zero_()
                return grad

            emb.register_hook(_freeze_existing_rows)

            head = model.get_output_embeddings()
            tied = head is not None and head.weight.data_ptr() == emb.data_ptr()
            print(
                f"vocab: training rows [{frozen_rows}:{emb.shape[0]}] only "
                f"({emb.shape[0] - frozen_rows} row(s)); rows [0:{frozen_rows}] "
                f"gradient-masked; lm_head "
                f"{'tied -- trained by the same tensor' if tied else 'UNTIED -- NOT masked, see comment'}"
            )
            if not tied:
                raise SystemExit(
                    "lm_head is untied on this checkpoint, so masking the input "
                    "embedding alone leaves the OUTPUT rows for the new tokens "
                    "untrained -- the model could read them and not write them, "
                    "which on structured output is the half that matters. Add the "
                    "same mask to the output embedding before training this model."
                )
        trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
        total = sum(p.numel() for p in model.parameters())
        print(
            f"LoRA r={args.lora_r} alpha={args.lora_alpha}  "
            f"trainable {trainable/1e6:.2f}M / {total/1e6:.1f}M "
            f"({100*trainable/total:.2f}%)"
        )

    tokenizer = get_chat_template(tokenizer, chat_template="qwen3")

    def to_text(rows: list[dict]) -> Dataset:
        # Our rows are already role/content, so standardize_sharegpt (which maps
        # from/value -> role/content) is unnecessary; applying it would be a
        # no-op at best and a rename at worst.
        texts = [
            tokenizer.apply_chat_template(
                r["conversations"], tokenize=False, add_generation_prompt=False
            )
            for r in rows
        ]
        return Dataset.from_dict({"text": texts})

    train_ds, val_ds = to_text(train_rows), to_text(val_rows)

    # `max_seq_length` is set from the data rather than left at the default:
    # every example here is a short chat turn, and padding to 2048 would spend
    # most of each batch's compute on padding tokens.
    lengths = [len(tokenizer(t).input_ids) for t in train_ds["text"][:2000]]
    p99 = sorted(lengths)[int(len(lengths) * 0.99)]
    seq_len = min(args.max_seq_length, max(256, 1 << (p99 - 1).bit_length()))
    print(f"token length p99={p99} -> max_seq_length={seq_len}")

    trainer = SFTTrainer(
        model=model,
        tokenizer=tokenizer,
        train_dataset=train_ds,
        eval_dataset=val_ds,
        args=SFTConfig(
            output_dir=args.out,
            dataset_text_field="text",
            max_seq_length=seq_len,
            per_device_train_batch_size=args.batch_size,
            gradient_accumulation_steps=args.grad_accum,
            warmup_steps=10,
            num_train_epochs=args.epochs if args.max_steps < 0 else 1,
            max_steps=args.max_steps,
            learning_rate=args.lr,
            logging_steps=5,
            optim="adamw_8bit",
            weight_decay=0.001,
            lr_scheduler_type="linear",
            seed=args.seed,
            report_to="none",
            eval_strategy="steps" if args.max_steps < 0 else "no",
            eval_steps=200,
            save_strategy="steps" if args.max_steps < 0 else "no",
            save_steps=200,
            save_total_limit=2,
            # Packing concatenates short samples into full sequences. With
            # chat turns averaging ~100 tokens against a 512-token window this
            # is most of the available throughput, not a marginal gain.
            packing=True,
            bf16=True,
        ),
    )

    if args.profile:
        # NVTX ranges make the nsys timeline readable: without them the trace is
        # an undifferentiated wall of kernels and you cannot tell a dataloader
        # stall from a backward pass.
        torch.cuda.nvtx.range_push("train")

    t0 = time.time()
    stats = trainer.train()
    dt = time.time() - t0

    if args.profile:
        torch.cuda.nvtx.range_pop()

    steps = stats.global_step or 1
    print(f"\n{steps} steps in {dt:.1f}s  ({dt/steps:.3f} s/step)")
    print(f"peak GPU memory: {torch.cuda.max_memory_allocated()/1e9:.2f} GB")

    Path(args.out).mkdir(parents=True, exist_ok=True)

    # WHAT HAPPENS AFTER THIS FUNCTION RETURNS -- not automated by this
    # script, and easy to assume is further along than it is:
    #
    #   1. merged_16bit (below) writes bf16 weights to args.out. Those are an
    #      INTERMEDIATE artifact -- the input to the converter, not what ships,
    #      and not what the 95.0% bar above was re-verified against when the
    #      KV-cache/quantization gate was added.
    #   2. `scripts/convert_to_gguf.sh <args.out> <out.gguf> q8_0` -- SENSEN's
    #      converter (sensen::convert_safetensors_to_gguf), which writes Q8_0
    #      directly from the merged safetensors. One call, no f16 intermediate,
    #      0.765 s. This replaced a llama.cpp two-step on 2026-08-03; both score
    #      16/16 on the defect holdout, and sensen is the project standard for
    #      conversion as well as serving. This is the SAME quantization scheme
    #      production serves (Q8_0 weights, q8 KV cache at serve time) --
    #      evaluate the GGUF, not the bf16 directory above, or a passing number
    #      here can still ship a regression.
    #   3. The GGUF goes to a private HF repo; MODEL_URL/MODEL_SHA256 in
    #      Railway's build variables point the Docker build at it. Nothing in
    #      this repo does that upload automatically, and it is deliberately
    #      not this script's job -- see docs/STRATEGY_ASSISTANT_PIPELINE.md
    #      for the full merge/export/distribute/serve chain, including the
    #      Dockerfile secret-mount trap and the serving-time constraints
    #      (system prompt, n_gpu_layers=0, the four-turn clarification shape)
    #      that a passing eval number here does not, by itself, protect.

    # Export is tried three ways, weakest assumption last, and a failure to
    # export must never be silent: the previous version caught only
    # RuntimeError, so when the second attempt raised something else the process
    # died INSIDE this block. That left an output directory holding config.json
    # and no weights at all -- which looks like a successful run, because the
    # only thing that says otherwise is the train_meta.json a few lines below
    # never appearing. The last run shipped exactly that, and the checkpoint the
    # Trainer had written was the only reason nothing was lost.
    #
    # Root cause of the first two failures: Qwen3 ties lm_head to the input
    # embeddings, so save_pretrained walks every tensor calling storage_ptr() to
    # find duplicates -- and QAT weights are tensor SUBCLASSES with no plain
    # storage behind them. safetensors demands a real pointer; these have none.
    saved_by = None
    errors: list[str] = []

    def attempt(name: str, fn) -> bool:
        nonlocal saved_by
        if saved_by:
            return True
        try:
            fn()
            saved_by = name
            print(f"saved ({name}) -> {args.out}")
            return True
        except Exception as e:  # noqa: BLE001 - see below
            # Deliberately broad. Each strategy fails in its own library with
            # its own exception type, and the point of the chain is that ANY
            # failure falls through to the next attempt rather than killing the
            # run after 21 minutes of training.
            errors.append(f"{name}: {type(e).__name__}: {e}")
            print(f"  {name} failed: {type(e).__name__}: {e}")
            return False

    if qlora:
        # Adapters first, and on their own. They are a few MB, they are the only
        # thing training actually produced, and they save without touching the
        # tied-lm_head problem at all. Saving them before attempting the merge
        # means a merge failure costs a conversion step, not the run.
        adapter_dir = Path(args.out) / "adapter"
        model.save_pretrained(adapter_dir)
        tokenizer.save_pretrained(adapter_dir)
        print(f"saved LoRA adapter -> {adapter_dir}")

        # Then a merged 16-bit model, which is what the CPU serving path needs:
        # llama.cpp's GGUF converter reads a normal dense checkpoint, not a base
        # plus adapters. save_pretrained_merged does the dequantize-and-fold.
        attempt(
            "merged_16bit",
            lambda: model.save_pretrained_merged(args.out, tokenizer, save_method="merged_16bit"),
        )

        # ------------------------------------------------------------------
        # save_pretrained_merged DISCARDS in-place edits to the base weights.
        # ------------------------------------------------------------------
        # It reloads the base checkpoint from disk and folds the LoRA adapters
        # into it, so anything written directly into a base tensor -- the
        # seeded rows for new tokens, and everything the masked embedding
        # gradient trained -- never reaches the file.
        #
        # MEASURED, v4, before this fix-up existed: q_proj differed from base
        # by 1.7e-03 and up_proj by 3.7e-02 (the adapters merged correctly),
        # while embed_tokens differed by EXACTLY 0.000e+00 and the seventeen
        # new rows still carried the base's untouched reserved-row norm of
        # 0.36135 instead of the ~0.9 a seeded row has. The model trained with
        # seeded, masked-trainable rows and exported without them.
        #
        # This is silent in the worst way: the TOKENIZER is saved with the new
        # tokens, so text tokenises to ids whose embeddings are untrained
        # reserved rows. The artifact is not merely unimproved, it is
        # incoherent -- and it loads, and it decodes, and it looks fine.
        #
        # It also means the mean-seeding in extend_vocabulary had never once
        # reached an artifact before now. v3 appeared to work only because its
        # LoRA adapter on embed_tokens did merge; its seeding was discarded the
        # same way.
        if vocab_added:
            _patch_merged_embedding(Path(args.out), model)

    # 1. The intended path: auto-detects the QAT model and converts it.
    attempt("torchao", lambda: model.save_pretrained_torchao(args.out, tokenizer=tokenizer))

    # 2. Pickle instead of safetensors -- torch.save needs no storage pointer.
    attempt("torch.save", lambda: model.save_pretrained(args.out, safe_serialization=False))

    # 3. Last resort: materialise the tensors ourselves. `.data` on a QAT
    #    subclass yields the plain dequantized tensor, and untying lm_head by
    #    cloning removes the duplicate that started all of this.
    def raw_state_dict() -> None:
        flat = {}
        for k, v in model.state_dict().items():
            t = v.detach()
            t = t.dequantize() if hasattr(t, "dequantize") else t
            flat[k] = t.clone().contiguous().to(torch.bfloat16)
        torch.save(flat, Path(args.out) / "pytorch_model.bin")
        model.config.save_pretrained(args.out)

    attempt("raw-state-dict", raw_state_dict)

    if not saved_by:
        # Do not write train_meta.json: an unexported run is a failed run, and
        # the checkpoint directory is what has to be used instead.
        raise RuntimeError(
            "every export strategy failed; the Trainer checkpoints under "
            f"{args.out} are the only usable weights.\n  " + "\n  ".join(errors)
        )

    tokenizer.save_pretrained(args.out)

    (Path(args.out) / "train_meta.json").write_text(
        json.dumps(
            {
                "model": MODEL_ID,
                "method": args.method,
                "qat_scheme": None if qlora else QAT_SCHEME,
                "lora": (
                    {"r": args.lora_r, "alpha": args.lora_alpha, "dropout": args.lora_dropout}
                    if qlora
                    else None
                ),
                "lr": args.lr,
                "seq_len": seq_len,
                "saved_by": saved_by,
                "export_errors": errors,
                "steps": steps,
                "seconds": dt,
                "s_per_step": dt / steps,
                "peak_gpu_bytes": torch.cuda.max_memory_allocated(),
                "train_rows": len(train_rows),
                # Provenance for the vocabulary extension. Without these two a
                # vocab-extended run is INDISTINGUISHABLE from a plain one by
                # its own metadata -- and the two produce different artifacts
                # from the same recipe fields, because --extend-vocab also puts
                # LoRA on embed_tokens/lm_head and so moves trainable params
                # from ~2.5% to ~18%. A checkpoint whose provenance cannot say
                # which recipe produced it is how the wrong one gets rerun.
                "extend_vocab": bool(args.extend_vocab),
                "vocab_added": vocab_added,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
