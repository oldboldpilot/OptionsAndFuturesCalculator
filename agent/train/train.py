#!/usr/bin/env python3
"""
Fine-tunes Qwen3-0.6B to turn chat into calculator parameters.

Follows the current Unsloth recipe for this model
(nb/Qwen3_(0.6B)-Reasoning-Conversational-ExecuTorch.ipynb), which for 0.6B is
FULL fine-tuning with quantization-aware training rather than LoRA:

    FastLanguageModel.from_pretrained(..., full_finetuning=True,
                                      qat_scheme="int8-int4")

QAT is the load-bearing choice here, not an optimisation. The model is destined
for CPU inference in the sensen service, so it will run with int4 linear weights
and int8 dynamically quantized activations whatever we do. Training with those
in the loop means the deployed model is what was trained, instead of a bf16
model degraded at the end -- and at 0.6B there is little headroom to absorb that
degradation. LoRA is skipped for the same reason the notebook skips it: at this
size a full fine-tune fits comfortably and converges better.

    python3 train.py --data ../dataset/data --out /scratch/agents/param-agent
"""
from __future__ import annotations

import argparse
import json
import os
import time
from pathlib import Path

# Must precede transformers/torch imports for the patches to apply.
from unsloth import FastLanguageModel
from unsloth.chat_templates import get_chat_template

import torch
from datasets import Dataset
from trl import SFTConfig, SFTTrainer

MODEL_ID = "unsloth/Qwen3-0.6B"
QAT_SCHEME = "int8-int4"


def load_jsonl(path: Path) -> list[dict]:
    return [json.loads(l) for l in path.read_text().splitlines() if l.strip()]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", default="../dataset/data")
    ap.add_argument("--out", default="/scratch/agents/param-agent")
    ap.add_argument("--max-seq-length", type=int, default=1024)
    ap.add_argument("--epochs", type=float, default=2.0)
    ap.add_argument("--batch-size", type=int, default=8)
    ap.add_argument("--grad-accum", type=int, default=4)
    ap.add_argument("--lr", type=float, default=5e-5)
    ap.add_argument("--seed", type=int, default=3407)
    # Profiling runs want a handful of steps, not a full epoch.
    ap.add_argument("--max-steps", type=int, default=-1)
    ap.add_argument("--profile", action="store_true",
                    help="Emit a torch profiler trace and NVTX ranges for nsys.")
    args = ap.parse_args()

    data = Path(args.data)
    train_rows = load_jsonl(data / "train.jsonl")
    val_rows = load_jsonl(data / "val.jsonl")
    print(f"train {len(train_rows)}  val {len(val_rows)}")

    model, tokenizer = FastLanguageModel.from_pretrained(
        model_name=MODEL_ID,
        max_seq_length=args.max_seq_length,
        dtype=torch.bfloat16,
        load_in_4bit=False,
        full_finetuning=True,
        qat_scheme=QAT_SCHEME,
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
    # Auto-detects the QAT model and performs the conversion.
    model.save_pretrained_torchao(args.out, tokenizer=tokenizer)
    print(f"saved -> {args.out}")

    (Path(args.out) / "train_meta.json").write_text(json.dumps({
        "model": MODEL_ID, "qat_scheme": QAT_SCHEME, "seq_len": seq_len,
        "steps": steps, "seconds": dt, "s_per_step": dt / steps,
        "peak_gpu_bytes": torch.cuda.max_memory_allocated(),
        "train_rows": len(train_rows),
    }, indent=2))


if __name__ == "__main__":
    main()
