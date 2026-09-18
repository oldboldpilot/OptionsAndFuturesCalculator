# Model pipeline and operational scripts code map
@author Olumuyiwa Oluwasanmi

> **How to read a citation in this document.** Every `path:line` here was checked
> mechanically: the file exists and the line is inside it. The line is a pointer to
> a NEIGHBOURHOOD, not a guarantee — a sample of 1,167 citations found the named
> symbol within eight lines of the cited line in about four cases in five, and the
> residue is mostly prose adjacency rather than error. Trust the path and the symbol
> name; re-grep the symbol if the line looks wrong. An attempt to auto-correct the
> residue by moving line numbers made it worse and was reverted, which is why this
> note exists instead of a tighter number.

## Scope

This document provides exhaustive reference documentation for the machine learning data and training pipeline, evaluation harnesses, operational maintenance scripts, backend C++ diagnostic probes, and the end-to-end integration smoke client across the repository:

- **ML Dataset Generators**:
  - `agent/dataset/build_dataset.py` (Options & Futures strategy extraction dataset)
  - `agent/dataset/build_mortgage_dataset.py` (Mortgage & fixed-income operation dataset)
- **Training and Profiling Pipelines**:
  - `agent/train/train.py` (QLoRA / SFT training harness)
  - `agent/profile/profile.sh` (Nsight timeline & kernel profiler)
  - `/scratch/agents/run_qlora.sh` (Dedicated remote GPU host training driver)
- **Model Evaluation Harnesses**:
  - `agent/train/evaluate.py` (In-process PyTorch/CUDA evaluation on merged bf16 weights)
  - `agent/train/eval_grpc.py` (Live gRPC evaluation of strategy assistant against running engine)
  - `agent/train/eval_grpc_mortgage.py` (Live gRPC evaluation of mortgage assistant with GP-ARA stderr capture)
- **Operational and Utility Scripts (`scripts/`)**:
  - All 24 operational shell, Python, Node.js, and maintenance scripts.
- **Backend C++ Diagnostic Probes (`backend/src/*_probe.cpp`)**:
  - All 16 standalone parity, throughput, numerical audit, and KV-cache benchmark binaries.
- **Backend Smoke Test Client**:
  - `backend/src/smoke_client.cpp` (Native gRPC multi-mode integration tester and mathematical identity verifier).

---

## ML Dataset Generators

### `agent/dataset/build_dataset.py`

#### Purpose
Generates synthetic multi-turn conversational datasets (`train.jsonl`, `val.jsonl`) for fine-tuning a language model to extract parameters for options and futures trading strategies into structured JSON enclosed in `<params></params>` tags (`agent/dataset/build_dataset.py:1-25`). It generates single-turn extractions, bare futures directives, options-on-futures combinations, ambiguous root clarifications, multi-turn modifications, out-of-scope refusals, and chit-chat declensions.

#### Generator Functions and Mix Weights
The generator samples from a weighted discrete distribution (`mix` table, `agent/dataset/build_dataset.py:696-708`):

| Generator Function | Weight | Line | Output Type / Task Description |
| :--- | :---: | :--- | :--- |
| `make_extraction` | 10 | `agent/dataset/build_dataset.py:228-278` | Single-turn parameter extraction for equity and index options/futures. |
| `make_bare_futures_direction` | 2 | `agent/dataset/build_dataset.py:281-329` | Single-turn direct futures order ("buy 2 ES", "short crude oil"). |
| `make_futures_options_extraction` | 3 | `agent/dataset/build_dataset.py:332-411` | Options on commodity/index futures with explicit root and asset class. |
| `make_share_purchase_refusal` | 1 | `agent/dataset/build_dataset.py:414-458` | Refusal for cash equity stock purchase ("buy 100 shares AAPL"). |
| `make_ambiguous_root_clarification`| 2 | `agent/dataset/build_dataset.py:461-482` | 2-turn dialog clarifying ambiguous roots (`ES`, `CL`) between options and futures. |
| `make_clarification` | 3 | `agent/dataset/build_dataset.py:485-520` | 2-turn dialog where assistant asks for missing critical parameter (e.g. expiry/direction). |
| `make_modification` | 3 | `agent/dataset/build_dataset.py:523-558` | 2-turn dialog where user adjusts quantity, expiry, or strike of prior strategy. |
| `make_refusal` | 1 | `agent/dataset/build_dataset.py:561-610` | Refusal on out-of-scope domains (crypto, forex, sports betting, tax advice). |
| `make_chitchat` | 1 | `agent/dataset/build_dataset.py:613-640` | Polite refusal/redirection on general conversational chatter. |
| `make_unknown_strategy` | 1 | `agent/dataset/build_dataset.py:643-671` | Clarification/refusal when user requests unsupported strategy name. |
| `make_unsupported_future` | 1 | `agent/dataset/build_dataset.py:674-693` | Refusal when user names unmapped/unsupported futures commodity root. |

*Total mix weight: 28.*

#### CLI Arguments and Defaults
Defined in `agent/dataset/build_dataset.py:734-766`:
- `--output-dir`: Directory for generated JSONL files (default: `"agent/dataset/data"`, line 737).
- `--train-size`: Target count of training rows (default: `4000`, line 743).
- `--val-ratio`: Fraction of generated rows reserved for validation (default: `0.05`, line 748).
- `--seed`: Random seed for reproducible generation (default: `42`, line 753).
- `--balance-roots`: Boolean flag to ensure uniform distribution across commodity roots (default: `False`, line 759).

#### Load-Bearing Invariants
1. **Quantity Derivation Rule** (`agent/dataset/build_dataset.py:231-235`): The template string is selected *before* the quantity is sampled. A quantity other than 1 is drawn only if `{q}` appears as a format placeholder in the template. If `{q}` is absent, quantity is pinned to 1. This prevents label-utterance divergence where the label specifies `quantity: N` while the user's prompt mentions no contract count.
2. **Ambiguous Root Handling** (`agent/dataset/build_dataset.py:461-482`): Roots `"ES"` (E-mini S&P vs Eversource Energy) and `"CL"` (Crude Oil vs Colgate-Palmolive) trigger `make_ambiguous_root_clarification()`. The assistant's first turn asks whether the user intends the future or the equity/option, and only emits `<params>` in the second turn after the user clarifies.
3. **Deduplication** (`agent/dataset/build_dataset.py:716`): Exact match deduplication on the initial user utterance string guarantees that the train and validation splits contain no shared user prompts.

---

### `agent/dataset/build_mortgage_dataset.py`

#### Purpose
Generates synthetic multi-turn ShareGPT-formatted datasets (`train.jsonl`, `val.jsonl`, `meta.json`) for fine-tuning a model to map natural-language mortgage and fixed-income queries into structured RPC invocations declared in `backend/proto/finance.proto` (`agent/dataset/build_mortgage_dataset.py:1-40`).

#### Proto AST Parsing and In-Scope Operations
The script mechanically parses `backend/proto/finance.proto` without depending on external compiler tools (`parse_finance_proto`, `agent/dataset/build_mortgage_dataset.py:144-193`):
- **In-Scope Sections** (`agent/dataset/build_mortgage_dataset.py:199-202`):
  - `"Time Value of Money"`
  - `"Mortgages and HELOC"`
  - `"Cash-Flow Analysis"`
  - `"Depreciation"`
  - `"Real Estate Investment"`
- **Excluded RPCs** (`agent/dataset/build_mortgage_dataset.py:226-228`):
  - `ConvertInterestRate`
  - `ComputeFisherRate`
- **Operation Excluded Fields (`OP_EXCLUDED_FIELDS`)** (`agent/dataset/build_mortgage_dataset.py:269-283`):
  - `ComputeXirr`: `{"rate", "guess"}`
  - `ComputeXnpv`: `{"guess"}`
  - `ComputeRate`: `{"guess"}`
  - `ComputeIrr`: `{"guess"}`

#### Operation Generators and Mix Weights
The generator samples across 22 distinct tasks (`mix` table, `agent/dataset/build_mortgage_dataset.py:2347-2377`):

| Generator Function | Weight | Target RPC / Task | Line |
| :--- | :---: | :--- | :--- |
| `gen_payment` | 12 | `ComputePayment` | `agent/dataset/build_mortgage_dataset.py:749-808` |
| `gen_amortization` | 12 | `ComputeAmortization` | `agent/dataset/build_mortgage_dataset.py:1001-1113` |
| `gen_closing_costs` | 10 | `ComputeClosingCosts` | `agent/dataset/build_mortgage_dataset.py:1270-1404` |
| `gen_refinance` | 8 | `ComputeRefinance` | `agent/dataset/build_mortgage_dataset.py:1406-1560` |
| `gen_recast` | 6 | `ComputeMortgageRecast` | `agent/dataset/build_mortgage_dataset.py:1562-1631` |
| `gen_heloc` | 6 | `ComputeHeloc` | `agent/dataset/build_mortgage_dataset.py:1633-1720` |
| `gen_rent_vs_buy` | 6 | `ComputeRentVsBuy` | `agent/dataset/build_mortgage_dataset.py:2209-2282` |
| `gen_rental_roi` | 5 | `ComputeRentalRoi` | `agent/dataset/build_mortgage_dataset.py:2284-2345` |
| `gen_present_value` | 4 | `ComputePresentValue` | `agent/dataset/build_mortgage_dataset.py:810-847` |
| `gen_future_value` | 4 | `ComputeFutureValue` | `agent/dataset/build_mortgage_dataset.py:849-896` |
| `gen_periods` | 4 | `ComputePeriods` | `agent/dataset/build_mortgage_dataset.py:898-946` |
| `gen_rate` | 4 | `ComputeRate` | `agent/dataset/build_mortgage_dataset.py:948-999` |
| `gen_payoff_timing` | 4 | `ComputePayoffTiming` | `agent/dataset/build_mortgage_dataset.py:1722-1794` |
| `gen_cumulative` | 4 | `ComputeCumulative` | `agent/dataset/build_mortgage_dataset.py:1796-1863` |
| `gen_depreciation` | 4 | `ComputeDepreciation` | `agent/dataset/build_mortgage_dataset.py:2134-2207` |
| `gen_npv` | 3 | `ComputeNpv` | `agent/dataset/build_mortgage_dataset.py:1865-1923` |
| `gen_xnpv` | 3 | `ComputeXnpv` | `agent/dataset/build_mortgage_dataset.py:1925-1974` |
| `gen_irr` | 3 | `ComputeIrr` | `agent/dataset/build_mortgage_dataset.py:1976-2025` |
| `gen_xirr` | 3 | `ComputeXirr` | `agent/dataset/build_mortgage_dataset.py:2027-2085` |
| `gen_payback` | 3 | `ComputePaybackPeriod` | `agent/dataset/build_mortgage_dataset.py:2087-2132` |
| `gen_clarification` | 12 | Multi-turn missing parameter question | `agent/dataset/build_mortgage_dataset.py:1115-1189` |
| `gen_modification` | 12 | Multi-turn parameter override | `agent/dataset/build_mortgage_dataset.py:1191-1268` |

*Total mix weight: 132.*

#### CLI Arguments and Defaults
Defined in `agent/dataset/build_mortgage_dataset.py:2451-2487`:
- `--proto`: Path to input proto file (default: `"backend/proto/finance.proto"`, line 2453).
- `--output-dir`: Output folder for JSONL and metadata (default: `"agent/dataset/data_mortgage"`, line 2459).
- `--train-size`: Number of training examples (default: `5000`, line 2465).
- `--val-ratio`: Validation holdout split ratio (default: `0.05`, line 2470).
- `--seed`: RNG seed (default: `42`, line 2475).

#### Load-Bearing Invariants
1. **Money Phrasing and Cent Drift Prevention (`phrase_money`)** (`agent/dataset/build_mortgage_dataset.py:329-372`):
   Spoken currency phrases are strictly formatted from the exact decimal string representation produced by `money_str(v)`. This prevents fractional cents truncation and formatting drift (e.g., phrasing `$1,250.50` as "twelve hundred fifty dollars" without cents). If an utterance text drops the cents, the backend's fail-closed grounding gate `G3` rejects the emitted decimal as an `UngroundedValue`.
2. **Label Derivability Assertion** (`agent/dataset/build_mortgage_dataset.py:426-433`):
   Every generated `<params>` block undergoes an internal verification pass asserting that every emitted numeric parameter can be derived from the generated prompt via the candidate mappings (M0..M9).
3. **M9 Candidate Mapping Coverage** (`agent/dataset/build_mortgage_dataset.py:1460-1468`):
   Down payment amounts generated as percentages must satisfy $\text{down\_payment} = \text{purchase\_price} \times \text{down\_payment\_percent}$ to remain verifiable under backend grounding map M9.
4. **Prepaid Interest Days Convention** (`agent/dataset/build_mortgage_dataset.py:1322-1349`):
   Closing cost rows emit either explicit whole-day counts or default to 0 / 15 depending on the presence of specific month-end closing phrasing.
5. **Metadata Provenance** (`agent/dataset/build_mortgage_dataset.py:2417-2427`):
   Every run produces a `meta.json` file recording the git commit SHA, generator weights, random seed, and exact row counts.

---

## Training and Profiling Pipelines

### `agent/train/train.py`

#### Purpose
Executes Supervised Fine-Tuning (SFT) of base decoder language models (such as `Qwen/Qwen2.5-Coder-7B-Instruct`) on structured financial extraction corpora using HuggingFace Transformers, PEFT (QLoRA), and TRL (`SFTTrainer`) (`agent/train/train.py:1-40`).

#### CLI Arguments and Defaults
Defined in `agent/train/train.py:323-370`:
- `--base-model`: Model identifier (default: `"Qwen/Qwen2.5-Coder-7B-Instruct"`, line 325).
- `--data-dir`: Dataset directory holding `train.jsonl` and `val.jsonl` (default: `"agent/dataset/data"`, line 328).
- `--output-dir`: Adapter checkpoint directory (default: `"checkpoints/lora"`, line 331).
- `--merged-dir`: Directory for merged 16-bit safetensors (default: `"checkpoints/merged"`, line 334).
- `--epochs`: Training epochs (default: `2.0`, line 337). Note: Remote production runs override this to `4.0`.
- `--batch-size`: Per-device batch size (default: `2`, line 340).
- `--gradient-accumulation-steps`: Gradient accumulation count (default: `8`, line 343). Effective batch size = $2 \times 8 = 16$.
- `--learning-rate`: AdamW learning rate (default: `2e-4`, line 346).
- `--max-seq-length`: Maximum context length in tokens (default: `1024`, line 349).
- `--lora-r`: LoRA rank (default: `16`, line 352).
- `--lora-alpha`: LoRA scaling factor (default: `16`, line 355).
- `--lora-dropout`: LoRA dropout rate (default: `0.05`, line 358).
- `--full-finetune`: Flag disabling QLoRA to perform full-parameter fine-tuning (default: `False`, line 361).
- `--extend-vocab`: Flag enabling domain vocabulary expansion (default: `False`, line 364).
- `--device`: Target compute device (default: `"cuda"`, line 367).
- `--seed`: Training seed (default: `42`, line 370).

#### QLoRA Configuration and LoRA Targets
When `--full-finetune` is omitted, `train.py` configures 4-bit NormalFloat quantization via `BitsAndBytesConfig` (`agent/train/train.py:431-455`):
- `load_in_4bit=True`
- `bnb_4bit_quant_type="nf4"`
- `bnb_4bit_compute_dtype=torch.bfloat16`
- `bnb_4bit_use_double_quant=True`
- **LoRA Projections (7 target modules)**: `["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"]` (`agent/train/train.py:448-452`).

#### Vocabulary Extension Mechanism
When `--extend-vocab` is enabled (`agent/train/train.py:162-237`):
- Adds closing cost and real-estate terms (`closing_cost_vocab`, lines 164-185) to tokenizer via `tokenizer.add_tokens()`.
- Resizes embedding matrix via `model.resize_token_embeddings()` (lines 192-205).
- Hooks `_freeze_existing_rows` on `embed_tokens` (`agent/train/train.py:210-224`): Zeroes out gradient updates on all pre-existing vocabulary indices, ensuring base weights remain unaltered while only newly added domain tokens receive parameter updates.
- Weight Merging Hook `_patch_merged_embedding` (`agent/train/train.py:551-570`): Injects newly trained embeddings into base model weights prior to saving merged safetensors.

#### Load-Bearing Formatting Constraints
1. **System Prompt Byte-Exactness**: The system prompt injected during dataset packing must match byte-for-byte the constant `kSystemPrompt` defined in `backend/src/modules/assistant_service.cpp:119` or `backend/src/modules/mortgage_assistant_service.cpp:169`. Any deviation causes prompt-mismatch regression during inference.
2. **Four-Turn Clarification Shape**: Multi-turn rows follow the strict sequence: `User -> Assistant (question) -> User (clarification) -> Assistant (<params>)`.
3. **Catalogue Alignment**: Strategy and operation names must match the static string tables generated in `backend/src/modules/strategy_catalogue.cppm`.

---

### `agent/profile/profile.sh`

#### Purpose
Shell automation script for NVIDIA Nsight Systems (`nsys`) timeline profiling and Nsight Compute (`ncu`) kernel analysis on assistant inference workloads (`agent/profile/profile.sh:1-142`).

#### Profiling Modes and Flags
- **Nsight Systems Timeline Profiling** (`agent/profile/profile.sh:62-73`):
  ```bash
  nsys profile \
    --trace=cuda,nvtx,osrt,cudnn,cublas \
    --cuda-memory-usage=true \
    --sample=cpu \
    --backtrace=dwarf \
    --output="${OUT_DIR}/nsys_${TIMESTAMP}" \
    "$@"
  ```
  Captures unified host/device timeline, NVTX markers, OS runtime events, and CPU backtraces.
- **Nsight Compute Kernel Profiling** (`agent/profile/profile.sh:102-115`):
  ```bash
  ncu --set full \
    --target-processes all \
    --export "${OUT_DIR}/ncu_${TIMESTAMP}" \
    "$@"
  ```
  Measures Streaming Multiprocessor (SM) utilization, memory throughput, roofline models, and warp occupancy.

---

### Remote Host Script: `/scratch/agents/run_qlora.sh`

#### Purpose and Citations
The script `/scratch/agents/run_qlora.sh` resides on the dedicated remote GPU training host (`docs/STRATEGY_ASSISTANT_PIPELINE.md:93-106`, `docs/MORTGAGE_ASSISTANT_PIPELINE.md:152,797`). It acts as the driver script invoking `train.py` within the host's specialized CUDA virtual environment.

#### Known Invocations and Parameter Overrides
- Overrides `--epochs` from `2.0` (the default in `agent/train/train.py:337`) to `4.0`.
- Passes `--batch-size 2` and `--gradient-accumulation-steps 8`.
- Invokes model conversion via `convert_safetensors_to_gguf` upon completion.
- *UNVERIFIED: Full script contents and environment variables of `/scratch/agents/run_qlora.sh`, which resides outside the git repository on the dedicated remote GPU training host.*

---

## Evaluation Harnesses

### `agent/train/evaluate.py`

#### Purpose
Fast, in-process evaluation of merged 16-bit language model checkpoints directly on CUDA using HuggingFace Transformers (`agent/train/evaluate.py:1-30`). It serves as an immediate post-training sanity check on model weights before conversion to GGUF.

#### Execution Transport
Direct Python in-process execution using `AutoModelForCausalLM.from_pretrained(a.model, dtype=torch.bfloat16, device_map="cuda")` (`agent/train/evaluate.py:38`). Generation runs greedily with `max_new_tokens=96, do_sample=False` (`agent/train/evaluate.py:61`).

#### Metrics and Denominators
- **Parameters Exact Match** (`exact / total`, `agent/train/evaluate.py:74-76, 83`):
  Evaluated on parsed JSON objects extracted from `<params>...</params>`. Key order differences do not penalize accuracy.
  *Denominator (`total`)*: Count of validation examples where the ground truth (gold) target contains a `<params>` block.
- **Field-Level Accuracy** (`fields[k] / total`, `agent/train/evaluate.py:79-81, 85`):
  Measures accuracy across individual keys: `symbol`, `asset_class`, `strategy`, `expiration_days`, `quantity`.
  *Denominator (`total`)*: Count of examples where gold target contains `<params>`.
- **Non-Params Correctness** (`non_param_ok / non_param_total`, `agent/train/evaluate.py:70-73, 87-89`):
  Measures whether the model refrained from emitting `<params>` on questions or refusals.
  *Denominator (`non_param_total`)*: Count of validation examples where gold target contains prose/refusal (no `<params>`).

---

### `agent/train/eval_grpc.py`

#### Purpose
End-to-end evaluation of strategy assistant performance against a running `calculator_engine` server over native gRPC (`ParseStrategy` RPC on `StrategyAssistantStub`) (`agent/train/eval_grpc.py:1-56`). Unlike `evaluate.py`, it tests the actual production pipeline: quantized Q8_0 GGUF weights, in-process 8-bit KV cache (`SENSEN_KV_DTYPE=q8`), prompt assembly in `assistant_service.cpp`, and C++ verification gates.

#### Execution Transport
Native gRPC transport to `sensen.assistant.StrategyAssistant/ParseStrategy` (`agent/train/eval_grpc.py:106-127`). Multi-turn rows (5-turn conversation) are evaluated across two sequential RPC calls:
1. `ParseStrategy(utterance=user_turn, prior_clarification="")` $\rightarrow$ must return `clarification` (question).
2. `ParseStrategy(utterance=user_turn, prior_clarification=reply_turn)` $\rightarrow$ must return `params` matching gold.

#### Metrics and Denominators
- `params exact-match` (`exact / total`, `agent/train/eval_grpc.py:187-190, 207`): Denominator `total` is all rows where gold contains `<params>`.
- `non-params correct` (`non_param_ok / non_param_total`, `agent/train/eval_grpc.py:175-182, 211`): Denominator `non_param_total` is all rows where gold contains prose/refusal.
- `asked-when-ambiguous` (`asked_ok / asked_total`, `agent/train/eval_grpc.py:158-164, 214`): Denominator `asked_total` is all multi-turn clarification rows.

#### Infrastructure Refusal Detection
Guards against false evaluation scores when the test environment is unconfigured (`agent/train/eval_grpc.py:98-103, 220-231`):
- Monitored reasons: `_INFRA_REASONS = ("DATA_UNAVAILABLE", "MODEL_UNAVAILABLE")`.
- If any infrastructure refusals occur, the script logs an error and exits with code 1 (`agent/train/eval_grpc.py:231`), preventing missing market-data credentials or unset model paths from being scored as model accuracy regressions.

---

### `agent/train/eval_grpc_mortgage.py`

#### Purpose
Evaluates mortgage operation parameter extraction on a running `calculator_engine` via `sensen.finance.MortgageAssistant/ParseOperation` (`agent/train/eval_grpc_mortgage.py:1-40`). It features simultaneous tailing of the engine's `stderr` stream (`RawOutputTail`, lines 110-155) to capture raw model decodes prior to C++ GP-ARA verification.

#### Metrics and Denominators
Defined in `agent/train/eval_grpc_mortgage.py:444-499`:
1. **Raw Model Accuracy (Pre-Verification)**:
   - `params exact-match` (`raw_exact / raw_total`): Gold has params, model emitted identical JSON.
   - `emitted valid <params>` (`raw_emitted / raw_total`): Model emitted syntactically valid JSON params block.
   - `<params> but bad JSON` (`raw_block_invalid / raw_total`): Model emitted opening tag but malformed syntax.
   - `non-params correct` (`raw_nonparam_ok / raw_nonparam_total`): Gold is prose/refusal, model emitted no params.
   - `asked-when-ambiguous` (`asked_raw_ok / asked_total`): Ambiguous rows where model asked a question on turn 1.
   - `answered-when-stated` (`answered_raw_ok / answered_total`): Modification rows where model answered turn 1.
2. **Served Outcome (Post-GP-ARA)**:
   - `params`, `clarification`, `refusal` ratios over `total_served` (`agent/train/eval_grpc_mortgage.py:482-488`).
   - `served_exact / raw_total` (`:493`): Params that passed all verification gates and matched gold exactly.
   - Breakdown of refusal reason codes (`refusal_shapes`, lines 490-492).

#### Holdout Disjointness Assertion
Enforces dataset hygiene across corpus iterations (`agent/train/eval_grpc_mortgage.py:525-530, 561-584`):
- `--assert-disjoint-from <train.jsonl>`: Asserts that zero holdout evaluation rows exist within the specified training set.
- Default Behavior: If any row overlaps, raises `SystemExit` (lines 578-580). This fail-closed check prevents a known defect where training row contamination in holdout sets falsely inflated evaluation benchmarks. Overriding requires passing `--allow-contamination` (line 529).

---

## Operational Scripts (`scripts/`)

### Summary Table of All 24 Scripts

| Script File | Language / Runtime | Operational Role | Key Flags & Dependencies |
| :--- | :--- | :--- | :--- |
| `backup_to_nas.sh` | Bash | Rsync repository backup to mounted CIFS NAS storage. | `rsync -avz --delete --no-links`, excludes `node_modules`, `build`, `.git` |
| `code_policy_check.sh` | Bash | Audits C++ source files against `config/cpp_details.txt` (Rules 3, 31, 50, 55). | Scopes via `git ls-files`; checks raw `new`, `-ffast-math`, trailing return types |
| `code_review_adversarial.sh` | Bash | Multi-phase review gate: static diff analysis and automated multi-agent consensus. | Targets `HEAD`, `--staged`, `--unstaged`; queries 3 independent review CLIs |
| `code_update_sync.sh` | Bash | Synchronizes reviewed updates to git remotes (GitHub and Gitea). | Runs `code_review_adversarial.sh`, commits, executes `git push origin master` |
| `convert_to_gguf.sh` | Bash | Quantizes merged 16-bit safetensors into GGUF using `sensen` converter. | `<merged_dir> <out.gguf> [dtype]`; calls `sensen::convert_safetensors_to_gguf` |
| `eval_assistant_sensen.py` | Python 3 | Evaluates strategy assistant on running engine with raw vs RPC layer scoring. | `<holdout.jsonl> <engine.log> [--layer raw\|rpc]`; requires gRPC stubs |
| `gen_proto.sh` | Bash | Generates TypeScript gRPC-Web client stubs from backend protobuf files. | Uses `protoc` with `protoc-gen-grpc-web`; outputs to `frontend/src/grpc/` |
| `generate_strategy_catalogue.py` | Python 3 | Generates `backend/src/modules/strategy_catalogue.cppm` from `strategies.json`. | Reads `strategies.json`; supports `--check` for drift detection |
| `measure_serving_reproducibility.py`| Python 3 | Measures 2x2 matrix {local, prod} x {sequential, concurrent} on serving holdouts. | Uses `ThreadPoolExecutor`; parses protobuf `outcome` oneof arm |
| `mint_pro_gate_creds.mjs` | Node.js | Mints valid, expired, bad-signature, and free-tier license tokens for testing. | Imports `workers/billing/src/licence.ts`; requires `LICENCE_SIGNING_KEY` |
| `probe_finance_service.py` | Python 3 | Validates `sensen.finance.Finance` against independent Python math over gRPC-Web. | Hand-encoded protobuf framing; `decimal.Decimal` 18-place fixed-point checks |
| `probe_live_assistant.py` | Python 3 | Verifies live deployed strategy assistant over gRPC-Web text transport. | Tests Qwen3-0.6B Q8 KV cache, GP-ARA gate, and `ES` root disambiguation |
| `probe_live_engine.py` | Python 3 | Post-deploy check verifying calculator engine via calendar spread payoff curve. | Checks `StrategyResponse.curve_days_to_expiration` (field 15) |
| `probe_live_term_structure.py` | Python 3 | Verifies live futures term structure and quote feeds over gRPC-Web. | Tests `GetMarketChain`/`GetMarketQuote`; checks level floor and unmapped refusals |
| `probe_local_under_load.py` | Python 3 | Evaluates local engine determinism under concurrent background RPC traffic. | Spawns background worker threads issuing `ParseOperation` requests |
| `probe_mortgage_adversarial.py` | Python 3 | Exercises mortgage assistant GP-ARA gate against adversarial prompt injections. | Asserts `SAFE` and `GROUNDED` invariants over real gRPC `ParseOperation` |
| `probe_pro_gate.sh` | Bash | Executes pro-gate authorization test matrix over native gRPC. | Drives `smoke_client ... pro` with credentials minted by Node.js |
| `probe_pro_gate_web.py` | Python 3 | Executes pro-gate authorization test matrix over live gRPC-Web ingress. | Distinguishes `PERMISSION_DENIED` from network errors; verifies liveness first |
| `probe_replica_periodicity.py` | Python 3 | Probes production replicas to distinguish cyclic routing from numeric noise. | Calculates auto-correlation lag agreement across repeated requests |
| `railway_deploy.sh` | Bash | Builds tarball and uploads deployment package to Railway API with service check. | Checks `$EXPECTED_SERVICE_NAME`; verifies archive contents before upload |
| `sensen_module_closure.py` | Python 3 | Computes minimal transitive closure of C++23 modules for `sensen_slim`. | Scans `backend/src/` imports; supports `--check` for CMakeLists verification |
| `validate_integration.sh` | Bash | Validates cross-branch merges between SGEE and calculator backend. | Bypasses `ccache`; forces Ninja build; tests multi-threaded `TaskBroker` |
| `verify_closing_costs_live.py` | Python 3 | Checks live production `ComputeClosingCosts` endpoint against identity math. | Hits Envoy `grpc_json_transcoder` via HTTP POST; checks base arithmetic |

---

### Detailed Operational Script Workflows

#### 1. Deployment and Infrastructure

##### `scripts/railway_deploy.sh`
- **Destination Verification Guard** (`scripts/railway_deploy.sh:75, 123-133`):
  Enforces `EXPECTED_SERVICE_NAME="options-calculator-backend"`. The script inspects `~/.railway/config.json` and queries `railway status --json` to resolve the service ID currently linked in the directory.
  *Critical Failure Mode Prevented*: If a developer previously linked another service (such as an SGEE queue worker node) in their local environment, running deploy without this check would overwrite that service's container image and replica configuration with the calculator backend Dockerfile.
- **Archive Verification and Pipefail Safety** (`scripts/railway_deploy.sh:168-189`):
  Materializes the archive file list into a temporary file (`tar -tzf "$ARCHIVE" > "$LIST"`) and checks required entries via `grep -qxF`. Piped commands like `tar ... | grep -q` are avoided because `grep -q` exits upon the first match, triggering `SIGPIPE` in `tar`, which causes intermittent build failures under Bash `set -o pipefail`.
  *Required Files Checked*: `./railway.json`, `./backend/Dockerfile`, `./backend/CMakeLists.txt`, `./backend/src/main.cpp`, `./backend/sensen/src/options.cppm`, `./backend/sensen/src/portfolio.cppm`.
- **Upload Transport** (`scripts/railway_deploy.sh:199-218`):
  Transfers the tarball to Railway's project endpoint (`https://backboard.railway.com/project/${PROJECT}/environment/${ENVIRONMENT}/up?serviceId=${SERVICE}`) via `curl` with no client-side timeout.

##### `scripts/backup_to_nas.sh`
- **CIFS Mount Compatibility** (`scripts/backup_to_nas.sh:10-26`):
  Executes `rsync -avz --delete --no-links` targeting `/home/muyiwa/PrimaryNAS/DataFolder/PycharmProjects/OptionsAndFuturesCalculator`.
  *Load-Bearing Flag (`--no-links`)*: CIFS mounts reject POSIX symlinks with `errno 95` (Operation not supported) unless mounted with `mfsymlinks`. Without `--no-links`, dangling symlinks in vendored submodules (e.g. `backend/sensen/external/CosyVoice/third_party/Matcha-TTS/data`) cause rsync to abort with error code 23.
  *Exclusions*: `node_modules`, `.next`, `build`, `.git`, `.wrangler`.

##### `scripts/validate_integration.sh`
- **Multi-Branch Concurrent Validation** (`scripts/validate_integration.sh:9-24`):
  Merges concurrent SGEE feature branches (`import-std` guards, task-queue service, `ReplicatedQueueRuntime`, Raft snapshotting) into a unified test build.
  *Concurrency Hazard Guarded*: Verifies that the gRPC task broker service mutex serializes calls correctly against background driver threads ticking `TaskBroker` concurrently.
- **Compiler and Toolchain Pinning** (`scripts/validate_integration.sh:28-36`):
  Bypasses `/usr/bin/ccache` (which had previously served stale precompiled objects for modified `.cppm` modules) and enforces Ninja as the CMake generator (since Unix Makefiles do not support C++23 module dependency graphs).

##### `scripts/code_update_sync.sh`
- **Policy Synchronization Workflow** (`scripts/code_update_sync.sh:1-56`):
  Verifies the presence of `config/update_policy.txt`, runs `code_review_adversarial.sh`, stages modified files, commits changes, and synchronizes to GitHub (`origin master`) and local Gitea remotes.

---

#### 2. Code Quality, Review, and Policy

##### `scripts/code_policy_check.sh`
Audits first-party code against rules in `config/cpp_details.txt` using `git ls-files` to strictly constrain audit scope (`scripts/code_policy_check.sh:45-56`):
- **Rule 3 (No Raw Owning Pointers)** (`scripts/code_policy_check.sh:92-117`):
  Detects `new Type` allocations. Excludes comments, `NOLINT` annotations, and placement new. Includes a string-literal stripping pre-filter (`strip_string_literals_then_match`, lines 75-89) so user-facing prose containing the word "new" (e.g. `"upgrade for a new one"` or `"new instructions:"`) does not trigger false positive violations.
- **Rules 50 & 55 (No `-ffast-math`)** (`scripts/code_policy_check.sh:119-140`):
  Scans build configuration files (`CMakeLists.txt`, `Dockerfile`, scripts) for `-ffast-math`. Enforces strict IEEE-754 floating-point associativity for cross-host pricing parity and replay determinism.
- **Rule 31 (Trailing Return Types)** (`scripts/code_policy_check.sh:142-167`):
  Enforces `auto func(...) -> ReturnType` syntax on all function definitions, scanning forward up to 8 lines to handle wrapped signatures.

##### `scripts/code_review_adversarial.sh`
Multi-phase automated review gate (`scripts/code_review_adversarial.sh:1-60`):
- **Phase 1 (Static Checks)**: Audits staged or unstaged diffs for raw `new`, `-ffast-math`, and hardcoded credentials.
- **Phase 2 (Multi-Agent Consensus Gate)**: Evaluates the diff against `config/cpp_details.txt` and `config/update_policy.txt` using three independent review command-line tools in parallel (`AGY_CLI`, `CLAUDE_CLI`, `CURSOR_CLI`). Requires at least two affirmative approvals among responding reviewers.
- **Phase 2b (Language Server Diagnostic Veto)**: Executes AST-aware semantic checks (referencing symbol lookups, declaration/implementation drift, clangd diagnostics). A verified failure adds an absolute veto to the gate.

---

#### 3. Model Conversion and Module Management

##### `scripts/convert_to_gguf.sh`
- **Single-Step Conversion** (`scripts/convert_to_gguf.sh:12-34`):
  Uses `sensen::convert_safetensors_to_gguf` (`backend/sensen/src/model_converter.cppm`) to write Q8_0 GGUF binaries directly from merged 16-bit safetensors in 0.765 seconds (310 tensors). Replaces the multi-step `convert_hf_to_gguf.py` $\rightarrow$ `llama-quantize` process, eliminating the 1.2 GB intermediate f16 file.
  *Default Target*: `q8_0`. Supports `f16`, `q6_k`, `q4_k`, `q5_k`.

##### `scripts/sensen_module_closure.py`
- **Minimal Closure Computation** (`scripts/sensen_module_closure.py:7-22`):
  Computes the transitive module dependency graph of all C++23 modules imported by `backend/src/` from `backend/sensen/src/`. This enables `backend/CMakeLists.txt` to compile `sensen_slim` (only required module translation units) rather than compiling all 271 units in `libsensen.so` (saving build time and reducing binary footprint by ~93 MB).
  *Drift Check*: `--check` exits non-zero if `CMakeLists.txt` deviates from the computed closure.

##### `scripts/generate_strategy_catalogue.py`
- **Code Generation from JSON** (`scripts/generate_strategy_catalogue.py:1-30`):
  Reads `agent/dataset/strategies.json` and code-generates `backend/src/modules/strategy_catalogue.cppm`. Checked into source control so that Docker container builds (which only copy `backend/` into the build context) have access to the complete catalogue without copying `agent/`.
  *Drift Check*: `--check` verifies checked-in code matches the JSON specification.

##### `scripts/gen_proto.sh`
- **gRPC-Web TypeScript Client Generation** (`scripts/gen_proto.sh:1-40`):
  Invokes `protoc` with `protoc-gen-grpc-web` to generate TypeScript client stubs in `frontend/src/grpc/` from canonical proto definitions in `backend/proto/` (`calculator.proto`, `finance.proto`, `assistant.proto`, `mortgage_assistant.proto`). Generated stubs are committed to git to support serverless frontend builds on Cloudflare Pages.

---

#### 4. Credential and Secret Management

##### `scripts/mint_pro_gate_creds.mjs`
- **Pro-Gate Test Credential Matrix** (`scripts/mint_pro_gate_creds.mjs:1-45`):
  Directly imports `workers/billing/src/licence.ts` to generate signed license tokens (`{s, t, e, v}`) and Supabase JWTs. Generates valid, expired, corrupted-signature, and free-tier credentials using `LICENCE_SIGNING_KEY` and `SUPABASE_JWT_SECRET` for testing authentication and authorization gates across transports.

---

#### 5. Probing, Testing, and Benchmarking

##### `scripts/probe_finance_service.py`
- **Independent Math Verification** (`scripts/probe_finance_service.py:6-18`):
  Sends gRPC-Web requests to `sensen.finance.Finance` and verifies responses against independent Python `decimal.Decimal` calculations.
  *Endpoints Covered*: `ComputePayment`, `ComputeAmortization`, `ComputeRefinance`, `ComputeMortgageRecast`, `ComputeHeloc`, `ComputeClosingCosts`, `ComputeNpv`, `ComputeXnpv`, `ComputeIrr`, `ComputeXirr`, `ComputeDepreciation`.
  *Transport*: Hand-encoded protobuf wire framing over HTTP POST (`/sensen.finance.Finance/<Method>`).

##### `scripts/probe_live_assistant.py`
- **Live Strategy Assistant Verification** (`scripts/probe_live_assistant.py:1-40`):
  Exercises the deployed strategy assistant over gRPC-Web text transport (`grpc-web-text`), verifying Qwen3-0.6B inference with 8-bit quantized KV caching, GP-ARA verification gate enforcement, and ambiguous root disambiguation (`ES` resolving via category elimination).

##### `scripts/probe_live_engine.py`
- **Live Engine Deployment Verification** (`scripts/probe_live_engine.py:1-26`):
  Validates live production engine deployment (`https://api.optionsandfuturescalculator.com`) by requesting a calendar spread calculation and verifying the presence of `StrategyResponse.curve_days_to_expiration` (field 15), proving the payoff curve reflects the near-term expiration.

##### `scripts/probe_live_term_structure.py`
- **Futures Term Structure Ingress Check** (`scripts/probe_live_term_structure.py:1-28`):
  Tests `GetMarketChain` and `GetMarketQuote` over gRPC-Web against production. Verifies:
  1. Curve presence.
  2. Level correctness (asserts `ES` underlying price reflects index level ~7400 rather than equity ticker ~71).
  3. Order-book discipline (verifies unconfigured market feeds do not fabricate bid/ask/volume).
  4. Refusal on unmapped commodity roots.

##### `scripts/probe_mortgage_adversarial.py`
- **Adversarial Gate Validation** (`scripts/probe_mortgage_adversarial.py:1-37`):
  Issues adversarial queries to `ParseOperation` on a running engine to verify GP-ARA fail-closed properties:
  - `SAFE`: The response must never emit unverified `<params>` on adversarial prompts, injections, or out-of-scope requests (must return refusal or clarification).
  - `GROUNDED`: If `<params>` are emitted, all numeric fields must be grounded in prompt tokens via admissible candidate maps M1..M9.

##### `scripts/probe_pro_gate.sh` & `scripts/probe_pro_gate_web.py`
- **Dual-Transport Pro-Gate Verification**:
  - `probe_pro_gate.sh` (`:1-25`): Runs native gRPC tests using `smoke_client` against local/staging engines.
  - `probe_pro_gate_web.py` (`:1-28`): Runs tests over gRPC-Web against production. Executes an anonymous single-leg liveness control first to verify basic connectivity, and ensures only `grpc-status: 7` (`PERMISSION_DENIED`) is scored as an authorization refusal (distinguishing genuine authorization denials from network drops or 5xx server errors).

##### `scripts/measure_serving_reproducibility.py`
- **Factorial Serving Analysis** (`scripts/measure_serving_reproducibility.py:1-27`):
  Measures generation consistency across a 2x2 matrix: {local, production} $\times$ {sequential, concurrent} using a fixed 16-row evaluation holdout (`val2_closingcosts.jsonl`), scoring purely on response structure (`outcome` oneof arm).

##### `scripts/probe_replica_periodicity.py`
- **Multi-Replica Stability Probe** (`scripts/probe_replica_periodicity.py:1-15`):
  Evaluates production response variations across requests to determine if instability stems from per-replica state discrepancies (periodic response sequence across 3 replicas) or per-request numerical noise. Calculates auto-correlation lag agreement for lags 1 through 5.

##### `scripts/probe_local_under_load.py`
- **Batch Sharing and Determinism** (`scripts/probe_local_under_load.py:1-20`):
  Runs a sequential evaluation battery through the local engine while background threads generate concurrent RPC traffic (`ParseOperation`) to determine if continuous batching introduces numerical jitter.

##### `scripts/eval_assistant_sensen.py`
- **Two-Layer Evaluation Harness** (`scripts/eval_assistant_sensen.py:17-29`):
  - `--layer raw`: Scores raw model output logged to engine stderr prior to verification. Useful when market data credentials are unavailable.
  - `--layer rpc`: Scores full `ParseResponse` outcome including verification, aliasing, and refusal gates.

##### `scripts/verify_closing_costs_live.py`
- **Live Closing Cost Arithmetic Audit** (`scripts/verify_closing_costs_live.py:1-34`):
  Hits Envoy's `grpc_json_transcoder` (`POST /sensen.finance.Finance/ComputeClosingCosts`) with test payloads, validating that itemized fee lines sum to subtotal, individual fees scale against correct bases (loan amount vs purchase price), seller credits reduce cash-to-close without modifying the itemized subtotal, and excess credits trigger `FAILED_PRECONDITION`.

---

## Backend C++ Diagnostic Probes (`backend/src/*_probe.cpp`)

The `backend/src/` directory contains 16 standalone diagnostic, parity, and benchmarking executables:

| Source File | Executable / Focus | Primary Purpose and Verification Logic | Key Flags / Environment Variables |
| :--- | :--- | :--- | :--- |
| `assistant_load_probe.cpp` | `assistant_load_probe` | Measures per-request latency and throughput under concurrent client load (1, 2, 4, 8 threads). | Arguments: `host:port [concurrency]` (`:1-37`) |
| `assistant_throughput_probe.cpp`| `assistant_throughput_probe`| Measures greedy token generation rate (tok/s) across thread counts on CPU and GPU. | `--threads`, `--tokens`, `--gpu-layers`, `--ctx`, `--reps` (`:1-24`) |
| `decode_golden_probe.cpp` | `decode_golden_probe` | Bit-exactness regression gate; captures and checks exact token ID sequences under greedy decoding. | `--capture <file>`, `--check <file>`, `--tokens`, `--gpu-layers`, `--threads` (`:1-30`) |
| `dequant_parity_probe.cpp` | `dequant_parity_probe` | Bit-parity comparison between `sensen::GGUFParser::dequantizeTensor` and ggml's `to_float`. | Argument: `<model.gguf>` (`:1-30`) |
| `gguf_requantize_probe.cpp` | `gguf_requantize_probe` | Re-encodes GGUF tensors at different float types (`f32`, `f16`) via `llama_model_quantize`. | Arguments: `<in.gguf> <out.gguf> <ftype: 0=f32, 1=f16>` (`:1-22`) |
| `kv_bench_probe.cpp` | `kv_bench_probe` | Benchmarks decode latency isolated from prefill at extended context lengths (512, 1024, 2048 tokens). | Arguments: `<model.gguf> <prompt_tokens> <decode_tokens> [threads] [paged\|full]` (`:1-35`) |
| `kv_equiv_probe.cpp` | `kv_equiv_probe` | Asserts numerical equivalence between `PagedKVCache` and `LinearKVCache` across rollouts. | Arguments: `<model.gguf> [rollout] [threads] [prompt_tokens]`; `SENSEN_KV_DTYPE` (`:1-35`) |
| `kv_gran_probe.cpp` | `kv_gran_probe` | Measures activation distributions on `Kcur_rope` and `Vcur` to determine optimal int8 KV quantization granularity. | Evaluates `row-sym`, `row-asym`, `row32-sym`, `chan16-sym`, `chan16-asym` (`:1-35`) |
| `kv_q8_real_probe.cpp` | `kv_q8_real_probe` | Validates `quantise_group_q8` and `dequantise_row_q8` round-trip fidelity on real captured activations. | Arguments: `<model.gguf> [threads]` (`:1-35`) |
| `kv_range_probe.cpp` | `kv_range_probe` | Measures dynamic range and subnormal frequencies on post-RoPE K and V tensors to justify F16 vs BF16 cache layout. | Arguments: `<model.gguf> [threads]` (`:1-30`) |
| `layer_parity_probe.cpp` | `layer_parity_probe` | Compares intermediate activations layer-by-layer between `sensen` and `llama.cpp` using matched tensor names. | Uses `cb_eval` in llama.cpp and `sensen::observe::setObserver` (`:1-30`) |
| `llamacpp_probe.cpp` | `llamacpp_probe` | Tests batched vs serial decoding throughput in upstream `llama.cpp` under Clang/libc++ toolchain. | Arguments: `<batched\|serial> <model.gguf> <n_parallel> [n_threads]` (`:1-30`) |
| `numeric_audit_probe.cpp` | `numeric_audit_probe` | End-to-end full-vocabulary logit parity audit between `sensen` and `llama.cpp` on identical token prompts. | `AUDIT_SYSTEM_FILE`, `AUDIT_UTTERANCE_FILE`, `AUDIT_PAD`, `AUDIT_SHIFT` (`:1-30`) |
| `observer_cost_probe.cpp` | `observer_cost_probe` | Quantifies overhead of `sensen.tensor_observer` across detached, attached no-op, and counting modes. | Arguments: `<model.gguf> [iters] [threads] [prompt_tokens]` (`:1-30`) |
| `prefix_cache_verify_probe.cpp`| `prefix_cache_verify_probe`| Evaluates KV prefix-reuse fast paths (`MultiLayerKVCache::share_from`) and chunking invariance. | Evaluates one-shot vs chunked prefill; `SENSEN_GEMM_Q8_PRECISE` (`:1-35`) |
| `tokenizer_parity_probe.cpp` | `tokenizer_parity_probe` | Asserts exact token-for-token BPE parity between `sensen::tokenizer` and `llama.cpp` on assistant prompts. | Argument: `<model.gguf>` (`:1-30`) |

---

## Backend Smoke Test Client (`backend/src/smoke_client.cpp`)

### Purpose and Execution Modes
`backend/src/smoke_client.cpp` (3626 lines) is an end-to-end integration and mathematical verification client. It communicates over native gRPC (`grpc::CreateChannel`) without third-party test framework dependencies (`backend/src/smoke_client.cpp:1-50`).

Supported CLI invocations:
- `./smoke_client <host:port>`: Standard end-to-end smoke test suite executing all financial services, pricing engines, and mathematical identity checks.
- `./smoke_client <host:port> load [threads] [duration_sec]`: Multi-threaded concurrency and stress harness measuring throughput and error rates.
- `./smoke_client <host:port> pro <licence_file> <jwt_file> [expected_outcome]`: Pro-gate authorization verifier checking valid tokens, forged claims, and signature enforcement.
- `./smoke_client <host:port> bench [iterations]`: Latency benchmarking suite recording p50, p95, and p99 percentiles across operations.

### Mathematical Identity Verifications
The client proves computational correctness by asserting invariant mathematical identities rather than comparing against snapshot outputs:

#### 1. Black-Scholes Put-Call Parity and Greeks
Lines `backend/src/smoke_client.cpp:1106-1151`:
- **Parity Formulation**:
  $$C - P = S - K e^{-rT}$$
  Evaluated with $S=100.0$, $K=100.0$, $r=0.05$, $\sigma=0.2$, $T=1.0$. Asserts $|(C - P) - (S - K e^{-rT})| \le 10^{-9}$.
- **Delta Difference**:
  $$\Delta_{\text{call}} - \Delta_{\text{put}} = 1.0$$
  Asserts $|(\Delta_{\text{call}} - \Delta_{\text{put}}) - 1.0| \le 10^{-9}$.
- **Gamma and Vega Symmetry**:
  Asserts $|\Gamma_{\text{call}} - \Gamma_{\text{put}}| \le 10^{-12}$ and $|\nu_{\text{call}} - \nu_{\text{put}}| \le 10^{-12}$.

#### 2. Bond Price and Yield Inversion
Lines `backend/src/smoke_client.cpp:1153-1185`:
- Evaluates a 10-year, 5% semi-annual bond.
- Asserts that pricing a bond at yield $y$, then solving for yield at price $P$, recovers the original yield within numerical tolerance:
  $$\text{Yield}(\text{Price}(y)) = y \quad \text{and} \quad \text{Price}(\text{Yield}(P)) = P$$

#### 3. Annuity Closed-Form and Payment Decomposition
Lines `backend/src/smoke_client.cpp:1010-1036`:
- Computes monthly payment using the closed-form annuity formula:
  $$\text{PMT} = -\text{PV} \cdot \frac{r}{1 - (1 + r)^{-n}}$$
- Asserts that component payments reconstruct total payment on period 1:
  $$\text{IPMT}_1 + \text{PPMT}_1 = \text{PMT}$$
- Asserts period-1 interest matches outstanding balance times periodic rate:
  $$|\text{IPMT}_1| = \text{PV} \times r$$

#### 4. Amortization Schedule Closure
Lines `backend/src/smoke_client.cpp:1038-1104`:
- Evaluates a \$300,000, 6%, 360-month loan.
- Asserts schedule row balance closure across all 360 rows:
  $$|\text{start\_balance}_i - \text{principal\_paid}_i - \text{end\_balance}_i| \le 10^{-6}$$
- Asserts final balance retires cleanly ($|\text{end\_balance}_{359}| \le 0.01$).
- Asserts cumulative principal paid equals total amount borrowed ($\sum \text{principal} = \$300,000.00$).
- Asserts that adding an overpayment ($\$500/\text{month}$) strictly shortens the actual amortization term.

#### 5. Mortgage Recast Linearity in Principal
Lines `backend/src/smoke_client.cpp:1510-1595`:
- Recast payment is mathematically linear in principal balance for a fixed interest rate and remaining term. The client computes three independent recasts (Base loan $B$, Lump sum $L$, Reduced balance $B-L$):
  $$\text{Payment}(B) - \text{Payment}(B - L) = \text{Payment}(L)$$
  Asserts agreement within $10^{-4}$.
- Full-Payoff Boundary Condition: When lump sum equals remaining balance ($L = B$), asserts new monthly payment equals exactly $0.00$.

#### 6. Closing Costs Base Arithmetic and Credit Isolation
Lines `backend/src/smoke_client.cpp:800-865`:
- **Subtotal Identity**: Itemized fees sum exactly to itemized subtotal ($|\sum \text{fee}_i - \text{subtotal}| \le 0.005$).
- **Base Verification**: Recomputes every percentage-based fee against its specific legal base:
  - Origination Fee ($0.75\%$) $\rightarrow$ computed against Loan Amount ($\text{Price} \times (1 - \text{Down\%})$).
  - Title Settlement ($0.55\%$) and Transfer Tax ($0.50\%$) $\rightarrow$ computed against Home Purchase Price.
  - Property Tax Escrow $\rightarrow$ exactly $3/12$ of annual bill.
  - Prepaid Interest $\rightarrow$ $\text{Loan} \times \text{Rate} / 365 \times 15$.
- **Credit Identity**: Introducing a seller/lender credit reduces `total_cash_to_close` dollar-for-dollar while leaving `itemised_subtotal` unchanged.

---

## Test Coverage

| Test Target / Executable | Test Source Location | Exercised Components | Verified Behaviors |
| :--- | :--- | :--- | :--- |
| `smoke_client` | `backend/src/smoke_client.cpp` | `sensen.finance.*`, `calculator.OptionsCalculator`, Pro Gate | Verifies Black-Scholes parity, bond inversion, annuity closed forms, amortization closure, recast linearity, closing-cost base math, and pro-gate authorization over native gRPC. |
| `test_mortgage_verification` | `backend/tests/test_mortgage_verification.cpp` | `mortgage_verification.cppm` | Re-parses `finance.proto` to verify `kLabelSpace` and `kOperationIds` have zero drift; tests `classify_slot` totality; tests G1..G5 gates, candidate maps M1..M9, down payment M0 suppression, cadence inference, and convention values. |
| `test_mortgage_grammar` | `backend/tests/test_mortgage_grammar.cpp` | `mortgage_grammar.cppm`, `mortgage_verification.cppm` | Validates DFA state transitions, prefix rejection, enum validity, full JSON object recognition, acceptance of gold parameters from `agent/dataset/data_mortgage/val.jsonl`, and `sensen::IGrammar` interface compliance. |
| `test_assistant_verification` | `backend/tests/test_assistant_verification.cpp` | `assistant_verification.cppm`, `strategy_catalogue.cppm` | Verifies cross-field constraints across 5 output fields; tests ambiguous root detection and clarification formatting for `"ES"` and `"CL"`; tests strategy alias normalisation, lexical support rules, and bare direction recovery. |
| `test_assistant_service` | `backend/tests/test_assistant_service.cpp` | `assistant_service.cpp`, `assistant_verification.cppm` | End-to-end integration test of strategy assistant gRPC service layer, continuous batching, and verification gate handoff. |
| `test_mortgage_assistant_service`| `backend/tests/test_mortgage_assistant_service.cpp`| `mortgage_assistant_service.cpp`, `mortgage_verification.cppm`| Tests mortgage assistant gRPC service layer, constrained grammar decoding, and GP-ARA verification pipeline. |
| `probe_mortgage_adversarial.py` | `scripts/probe_mortgage_adversarial.py` | Deployed / running `calculator_engine` | Tests mortgage assistant GP-ARA gate under adversarial inputs; enforces `SAFE` (refusal/clarification) and `GROUNDED` invariants over real RPCs. |
| `probe_pro_gate.sh` | `scripts/probe_pro_gate.sh` | Engine Pro Gate (native gRPC) | Tests pro-gate enforcement matrix (valid, expired, bad-sig, free-tier, forged claims) via `smoke_client`. |
| `probe_pro_gate_web.py` | `scripts/probe_pro_gate_web.py` | Engine Pro Gate (gRPC-Web) | Tests pro-gate enforcement over public HTTP/gRPC-Web ingress; validates liveness and distinguishes permission denials from transport errors. |
| `eval_grpc.py` | `agent/train/eval_grpc.py` | Strategy Assistant on `calculator_engine` | Evaluates fine-tuned Q8_0 model with Q8 KV cache on running engine over gRPC; handles multi-turn clarifications; traps infrastructure refusals. |
| `eval_grpc_mortgage.py` | `agent/train/eval_grpc_mortgage.py` | Mortgage Assistant on `calculator_engine` | Evaluates mortgage assistant over gRPC; tails engine stderr to score raw model output; verifies holdout set disjointness via `--assert-disjoint-from`. |
| `evaluate.py` | `agent/train/evaluate.py` | Merged bf16 checkpoint on CUDA | In-process evaluation of merged model weights; measures params exact match, field-level accuracy, and non-params correctness. |
| `code_policy_check.sh` | `scripts/code_policy_check.sh` | First-party C++ source & build files | Audits C++ source code and build configs against Rules 3, 31, 50, and 55 in `config/cpp_details.txt`. |
| `code_review_adversarial.sh` | `scripts/code_review_adversarial.sh` | Staged / unstaged git diffs | Static checks on git diff combined with multi-agent consensus review and language server diagnostic analysis. |

---

## Open Questions

1. **Remote GPU Training Driver Script**:
   `UNVERIFIED: Full script contents and environment variables of /scratch/agents/run_qlora.sh, which resides outside the git repository on the dedicated remote GPU training host.`
   The repository documents that this script invokes `agent/train/train.py` with `--epochs 4.0`, `--batch-size 2`, and `--gradient-accumulation-steps 8` (`docs/STRATEGY_ASSISTANT_PIPELINE.md:93-106`, `docs/MORTGAGE_ASSISTANT_PIPELINE.md:152,797`), but the complete script file is not tracked in git.
