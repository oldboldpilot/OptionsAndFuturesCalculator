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
-> quantize (llama.cpp convert + llama-quantize, bf16 -> Q8_0 GGUF, ~639 MB) ->
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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="../dataset/data")
    ap.add_argument("--out", default="/scratch/agents/param-agent")
    ap.add_argument("--max-seq-length", type=int, default=1024)
    ap.add_argument("--epochs", type=float, default=2.0)
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
            lora_alpha=args.lora_alpha,
            lora_dropout=args.lora_dropout,  # 0 is Unsloth's optimised path
            bias="none",  # ditto
            use_gradient_checkpointing="unsloth",
            random_state=args.seed,
            max_seq_length=args.max_seq_length,
            use_rslora=False,
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
    #      INTERMEDIATE artifact -- a convenient format for llama.cpp's
    #      converter, not what ships and not what the 95.0% bar above was
    #      re-verified against when the KV-cache/quantization gate was added.
    #   2. llama.cpp: `convert_hf_to_gguf.py args.out --outfile model-f16.gguf
    #      --outtype f16`, then `llama-quantize model-f16.gguf model-Q8_0.gguf
    #      Q8_0`. This is the SAME quantization scheme production serves
    #      (Q8_0 weights, q8 KV cache at serve time) -- evaluate the GGUF, not
    #      the bf16 directory above, or a passing number here can still ship
    #      a regression.
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
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
