/**
 * DIAGNOSTIC, NOT A SHIPPED FEATURE.
 *
 * Built to verify a system-prompt KV prefix-reuse fast path (share a
 * precomputed prefix's KV into a fresh AgentSession via
 * MultiLayerKVCache::share_from, then continue the forward pass over only
 * the request's own tokens via schedulerPrefillContinue, instead of
 * recomputing the fixed system-prompt prefix's KV on every request).
 *
 * `diagnose_chunking_invariance()` below isolates the underlying question
 * with NO caching/sharing involved at all: it splits ONE prompt's own
 * forward pass into `schedulerPrefill(first K tokens)` followed by
 * `schedulerPrefillContinue(remaining tokens)`, against the SAME single
 * agent, and compares the result to one `schedulerPrefill(all tokens)`
 * call.
 *
 * CORRECTED ROOT CAUSE (this header previously blamed sensen's Q8_0 GEMM
 * peel -- `GEMM::matvecQuantizedBatch`'s row-grouping-by-batch-offset --
 * and prescribed pinning the micro-kernel to a row's index modulo 8. That
 * diagnosis was WRONG and has been retracted; the fix it prescribed would
 * not have touched the real defect. Left here only as a pointer for anyone
 * who finds an old copy of this file: do not re-attempt it.
 *
 * The GEMM-peel theory was DISPROVEN, not merely superseded, by two
 * independent checks, and the negative result is the valuable part of this
 * investigation:
 *
 *   1. Disabling the entire Q8_0 batched-GEMM kernel family outright
 *      (`SENSEN_GEMM_Q8_PRECISE=1`, which reverts every batched matvec to
 *      the single-row scalar/AVX path sensen already ships as a reference)
 *      left the chunked-vs-one-shot divergence COMPLETELY UNCHANGED --
 *      same magnitude, same flipped tokens. If the GEMM peel were the
 *      cause, disabling it had to change something; it changed nothing.
 *   2. Direct bit-testing of `matvecQ8_0_Int8BatchSlice` across many row
 *      counts and batch splits found it bit-exactly batch-invariant: a
 *      given logical row's output does not depend on which other rows
 *      share its call, contradicting the "peel groups by offset-from-
 *      batch-start" theory outright.
 *
 * The REAL defect was a missing causal mask in
 * `MultiHeadAttention::compute_attention_paged_flat`
 * (`backend/sensen/src/multi_head_attention.cppm`, around line 4192): the
 * causal predicate was gated `causal_ && q_len == total_seq_len && j > i`,
 * so it applied ONLY on a fresh full prefill, where the local row index `i`
 * happens to coincide with the row's absolute sequence position. On a
 * CONTINUATION forward -- `q_len < total_seq_len`, exactly the shape
 * `schedulerPrefillContinue` produces for a chunked-prefill tail or a
 * shared-prefix continuation -- the whole causal-mask branch was skipped,
 * so a continuation row could attend to its own chunk-mates' FUTURE
 * tokens: non-causal attention within the chunk. The correct absolute
 * position, `q_pos = total_seq_len - q_len + i`, was already computed a few
 * lines above for the sliding-window-attention mask; the fix was gating the
 * causal predicate on that absolute position uniformly (`j > q_pos`)
 * instead of re-deriving the fresh-prefill special case. A structurally
 * identical, unreached-in-production sibling, `compute_attention_paged`
 * (the vector-of-vectors variant, zero callers anywhere in the tree,
 * confirmed via grep rather than assumed), carried the same defect and was
 * fixed the same way for consistency.
 *
 * Fixed in sensen commit `4ef7ebe7` ("fix(attention): apply paged-prefill
 * causal mask by absolute position"), pushed to both remotes.
 *
 * Verified there (not re-verified independently by this repo, which is
 * read-only against `backend/sensen`): 20/20 cases now bit-identical --
 * both production checkpoints (`mortgagefv-assistant-v2-q8_0.gguf`,
 * `strategy-assistant.gguf`) x {`SENSEN_KV_DTYPE=fp32`, `f16`} x 5 cases --
 * against 5/5 DIFFERING before, with two cases previously flipping the
 * greedy rollout's token (mortgage case 3: token 15 vs 19 at position 36;
 * strategy case 3: token 4825 vs 69 at position 20). Fresh-prefill and
 * decode paths are provably untouched by the fix (both are cases where
 * `q_pos == i` or the single-decode-token case, bit-identical by
 * construction), so the deployed models' quality record (the defect
 * holdout, the mortgage 25/90, the 95.0% params bar) stands.
 *
 * A SECOND, INDEPENDENT MECHANISM remains and is NOT fixed by the causal-
 * mask patch: this build's default KV-cache dtype on CPU is Q8 (see
 * `kv_half.cppm`'s `parseKvDtypeEnv()` -- an unset `SENSEN_KV_DTYPE`
 * resolves to `KvDtype::Q8`, not fp32/fp16, per that file's own "default
 * was flipped" comment), and `PagedKVCache::update()`
 * (`backend/sensen/src/kv_cache.cppm`, around lines 1077-1083) quantises a
 * Q8 KV block only when its 16th token has just landed (deferred
 * quantisation: a block stays an OPEN fp16 buffer until it is full, then is
 * quantised once, whole-block). A chunked forward (`schedulerPrefill(K)` +
 * `schedulerPrefillContinue(rest)`) and an equivalent one-shot
 * `schedulerPrefill(all)` therefore read DIFFERENT quantisation states of
 * the SAME logical tokens whenever the split point `K` does not fall on a
 * 16-token block boundary: the chunked call quantises (or leaves open) a
 * block based on where ITS OWN forward call happened to end, while the
 * one-shot call quantises based on the whole prompt's blocks. Both
 * production system prompts' natural prefix length (76 tokens for
 * mortgage, 60 for strategy -- both ≡ 12 mod 16) is NOT block-aligned, so
 * this mechanism alone predicts a residual divergence at the natural split
 * even with the causal-mask fix applied, and predicts BIT-IDENTITY at a
 * 16-aligned split. See `diagnose_chunking_invariance()`'s `--split`
 * override below, added specifically to exercise the aligned case, and this
 * repo's own session report for whether the measurement bore this out.
 *
 * Consequence, independent of both mechanisms above: ANY caller that splits
 * a forward pass across two calls against the same evolving KV cache -- not
 * just a hypothetical prefix cache, but the PRE-EXISTING
 * `schedulerPrefillContinue` API itself (its own doc comment describes it
 * as backing a genuine stateful multi-turn chat continuation, H5) -- must
 * either land on a block-aligned split under the default Q8 KV dtype, or
 * accept that a non-aligned split reads a different quantisation state than
 * an equivalent one-shot forward would have. That is a pre-existing,
 * narrower property of sensen's CPU serving path than the causal-mask bug
 * was; the prefix-cache feature this probe exists to validate works around
 * it by snapshotting the cached prefix at `floor(P/16)*16` tokens rather
 * than at the natural (unaligned) prefix length `P`.
 *
 * Not part of the production binary or the required test suite; a
 * diagnostic/regression tool, matching this repo's existing
 * decode_golden_probe.cpp / assistant_throughput_probe.cpp convention.
 *
 *   prefix_cache_verify_probe <model.gguf> --system mortgage|strategy \
 *       [--split N]
 *
 * `--split N` overrides the chunk point `diagnose_chunking_invariance()`
 * uses for its no-caching-involved chunked-vs-one-shot check (default:
 * this checkpoint's own natural system-prompt-prefix token count, the
 * pre-existing, non-block-aligned behaviour). Use it to exercise a
 * 16-aligned split the natural prefix length never lands on.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

import sensen.llm_pipeline;
import sensen.kv_cache;

namespace {

constexpr std::string_view kMortgageSystemPrompt =
    "You turn a homeowner's or investor's request into parameters for the "
    "Finance service's mortgage, time-value-of-money and cash-flow tools. "
    "Reply with a single JSON object inside <params></params> when you have "
    "enough to act, or ask exactly one short question when you do not. You "
    "do not give financial, tax or legal advice.";

constexpr std::string_view kStrategySystemPrompt =
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> "
    "when you have enough to act, or ask exactly one short question when you "
    "do not. You do not give trading advice.";

struct Case {
    std::string_view utterance;
    std::string_view prior_clarification;  // empty for first-turn cases
};

// Mortgage battery: params, clarification-round-trip, and an out-of-scope
// refusal-shaped utterance. Whatever outcome each actually produces, both
// code paths under test must produce the SAME one.
constexpr Case kMortgageCases[] = {
    {"What are the monthly payments on a $350,000 mortgage at 6.5% for 30 years?", ""},
    {"I want to refinance", ""},
    {"I want to refinance", "my current 30-year fixed at 7.2%, $280,000 balance, into a 15-year at 5.9%"},
    {"What's a good stock to buy right now?", ""},
    {"Depreciation schedule for a $500,000 rental property over 27.5 years straight-line.", ""},
};

// Strategy battery: params, clarification-round-trip, refusal-shaped.
constexpr Case kStrategyCases[] = {
    {"Iron condor on SPY, 30 days out, one contract.", ""},
    {"Long ES, 30 days, 1 contract.", ""},
    {"Long ES, 30 days, 1 contract.", "futures"},
    {"What's the weather like today?", ""},
    {"Bull call spread on AAPL expiring in 45 days, 3 contracts.", ""},
};

[[nodiscard]] auto build_prompt(std::string_view system_prompt, std::string_view utterance,
                                std::string_view prior_clarification) -> std::string {
    std::string prompt;
    prompt += "<|im_start|>system\n";
    prompt += system_prompt;
    prompt += "<|im_end|>\n";
    prompt += "<|im_start|>user\n";
    prompt += utterance;
    prompt += "<|im_end|>\n";
    if (!prior_clarification.empty()) {
        prompt += "<|im_start|>assistant\nPlaceholder?<|im_end|>\n";
        prompt += "<|im_start|>user\n";
        prompt += prior_clarification;
        prompt += "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

[[nodiscard]] auto generation_config(std::size_t max_new_tokens) -> sensen::GenerationConfig {
    sensen::GenerationConfig config;
    config.strategy = sensen::SamplingStrategy::GREEDY;
    config.max_new_tokens = max_new_tokens;
    config.deterministic = true;
    config.n_gpu_layers = 0;
    config.repetition_penalty = 1.0F;
    return config;
}

struct DecodeResult {
    std::vector<float> prefill_logits;
    std::vector<std::uint32_t> rollout;
};

[[nodiscard]] auto full_rollout(sensen::LLMPipeline& pipeline,
                                std::span<const std::uint32_t> prompt_tokens,
                                const sensen::GenerationConfig& params, std::uint32_t eos_id,
                                std::uint32_t im_end_id, std::size_t agent_id, bool use_cache,
                                sensen::AgentSession* prefix_agent,
                                std::span<const std::uint32_t> prefix_tokens) -> DecodeResult {
    DecodeResult result;
    auto agent = pipeline.schedulerMakeAgent(agent_id);
    if (agent == nullptr) {
        std::fprintf(stderr, "schedulerMakeAgent failed\n");
        std::exit(1);
    }

    std::vector<float> logits;
    if (use_cache) {
        auto& context = agent->getContext();
        context.assign(prompt_tokens.begin(),
                       prompt_tokens.begin() + static_cast<std::ptrdiff_t>(prefix_tokens.size()));
        agent->kv_cache->share_from(*prefix_agent->kv_cache);
        agent->setPosition(prefix_tokens.size());
        logits = pipeline.schedulerPrefillContinue(
            *agent, prompt_tokens.subspan(prefix_tokens.size()), params);
    } else {
        logits = pipeline.schedulerPrefill(*agent, prompt_tokens, params);
    }
    result.prefill_logits = logits;

    std::uint32_t next_token = pipeline.schedulerSample(logits, params, agent->getContext());

    for (std::size_t step = 0; step < params.max_new_tokens; ++step) {
        std::vector<std::uint32_t> tokens{next_token};
        std::vector<sensen::AgentSession*> agents{agent.get()};
        auto step_logits = pipeline.schedulerDecodeStep(std::span<const std::uint32_t>(tokens),
                                                        std::span<sensen::AgentSession* const>(agents));
        agent->incrementPosition();
        const std::uint32_t consumed = next_token;
        if (consumed == eos_id || consumed == im_end_id) {
            break;
        }
        agent->getContext().push_back(consumed);
        result.rollout.push_back(consumed);
        if (result.rollout.size() >= params.max_new_tokens) {
            break;
        }
        if (step_logits.empty() || step_logits.front().empty()) {
            break;
        }
        next_token = pipeline.schedulerSample(step_logits.front(), params, agent->getContext());
    }

    pipeline.schedulerEvict(*agent);
    return result;
}

}  // namespace

/// Diagnostic: isolates whether a "chunked" forward (schedulerPrefill over
/// the first K tokens, then schedulerPrefillContinue over the rest -- NO
/// cross-agent sharing at all, just splitting ONE prompt's own forward into
/// two calls) reproduces a single one-shot schedulerPrefill over the whole
/// prompt bit-for-bit. If this diverges, the KV-prefix-reuse feature is not
/// the cause of any divergence found elsewhere -- chunked continuation
/// itself is not batch-invariant on this path, independent of caching.
auto diagnose_chunking_invariance(sensen::LLMPipeline& pipeline,
                                                std::span<const std::uint32_t> prompt_tokens,
                                                const sensen::GenerationConfig& params,
                                                std::size_t split, std::size_t agent_id) -> void {
    auto one_shot = pipeline.schedulerMakeAgent(agent_id);
    const auto one_shot_logits = pipeline.schedulerPrefill(*one_shot, prompt_tokens, params);

    auto chunked = pipeline.schedulerMakeAgent(agent_id + 1);
    const auto first_logits =
        pipeline.schedulerPrefill(*chunked, prompt_tokens.subspan(0, split), params);
    (void)first_logits;
    const auto chunked_logits =
        pipeline.schedulerPrefillContinue(*chunked, prompt_tokens.subspan(split), params);

    const bool identical = one_shot_logits == chunked_logits;
    std::printf("  [chunk-invariance split=%zu/%zu] %s\n", split, prompt_tokens.size(),
               identical ? "IDENTICAL" : "DIFFERS");
    if (!identical) {
        std::size_t first_diff = 0;
        for (; first_diff < one_shot_logits.size() && first_diff < chunked_logits.size();
             ++first_diff) {
            if (one_shot_logits[first_diff] != chunked_logits[first_diff]) break;
        }
        std::printf("    first diff at index %zu: %.9g vs %.9g\n", first_diff,
                   static_cast<double>(one_shot_logits[first_diff]),
                   static_cast<double>(chunked_logits[first_diff]));
    }
    pipeline.schedulerEvict(*one_shot);
    pipeline.schedulerEvict(*chunked);
}

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: prefix_cache_verify_probe <model.gguf> --system mortgage|strategy "
                     "[--split N]\n");
        return 2;
    }
    const std::string model_path = argv[1];
    std::string which = "mortgage";
    std::optional<std::size_t> split_override;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--system" && i + 1 < argc) {
            which = argv[++i];
        } else if (std::string_view{argv[i]} == "--split" && i + 1 < argc) {
            const std::string_view raw{argv[++i]};
            std::size_t value = 0;
            const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
            if (result.ec != std::errc{} || value == 0) {
                std::fprintf(stderr, "invalid --split value: %.*s\n",
                            static_cast<int>(raw.size()), raw.data());
                return 2;
            }
            split_override = value;
        }
    }

    const bool is_mortgage = (which == "mortgage");
    const std::string_view system_prompt = is_mortgage ? kMortgageSystemPrompt : kStrategySystemPrompt;
    const std::span<const Case> cases = is_mortgage
                                            ? std::span<const Case>(kMortgageCases)
                                            : std::span<const Case>(kStrategyCases);
    const std::size_t max_new_tokens = is_mortgage ? 384 : 256;

    std::printf("Loading %s (%s system prompt) ...\n", model_path.c_str(), which.c_str());
    auto pipeline = sensen::LLMPipeline::fromGGUF(model_path).numThreads(4).build();
    if (!pipeline) {
        std::fprintf(stderr, "failed to build pipeline from %s\n", model_path.c_str());
        return 1;
    }

    const auto params = generation_config(max_new_tokens);
    const std::uint32_t eos_id = pipeline->schedulerEosToken();
    std::uint32_t im_end_id = eos_id;
    if (const auto encoded = pipeline->schedulerEncode("<|im_end|>"); encoded.size() == 1) {
        im_end_id = encoded.front();
    }

    // --- Build the cached prefix, exactly as ensure_prefix_cache() does. ---
    std::string fixed_prefix;
    fixed_prefix += "<|im_start|>system\n";
    fixed_prefix += system_prompt;
    fixed_prefix += "<|im_end|>\n";
    fixed_prefix += "<|im_start|>user\n";
    auto prefix_tokens = pipeline->schedulerEncode(fixed_prefix);
    std::printf("Fixed prefix: %zu tokens\n", prefix_tokens.size());

    constexpr std::size_t kPrefixAgentId = std::numeric_limits<std::size_t>::max() - 1;
    auto prefix_agent = pipeline->schedulerMakeAgent(kPrefixAgentId);
    if (prefix_agent == nullptr) {
        std::fprintf(stderr, "could not build prefix agent\n");
        return 1;
    }
    const auto prefix_logits = pipeline->schedulerPrefill(*prefix_agent, prefix_tokens, params);
    if (prefix_logits.empty()) {
        std::fprintf(stderr, "prefix prefill produced no logits\n");
        return 1;
    }

    int failures = 0;
    std::size_t next_agent_id = 0;

    for (const auto& c : cases) {
        const std::string prompt = build_prompt(system_prompt, c.utterance, c.prior_clarification);
        const auto prompt_tokens = pipeline->schedulerEncode(prompt);

        const bool prefix_matches =
            prompt_tokens.size() > prefix_tokens.size() &&
            std::equal(prefix_tokens.begin(), prefix_tokens.end(), prompt_tokens.begin());

        std::printf("\n--- utterance=%.60s%s prior=%.30s%s prefix_matches=%d ---\n",
                    c.utterance.data(), c.utterance.size() > 60 ? "..." : "",
                    c.prior_clarification.data(),
                    c.prior_clarification.size() > 30 ? "..." : "", prefix_matches ? 1 : 0);

        const std::size_t natural_split = prefix_tokens.size();
        const std::size_t requested_split = split_override.value_or(natural_split);
        if (requested_split == 0 || requested_split >= prompt_tokens.size()) {
            std::printf("  [chunk-invariance split=%zu/%zu] SKIPPED -- split out of range for "
                       "this case's %zu-token prompt\n",
                       requested_split, prompt_tokens.size(), prompt_tokens.size());
        } else {
            diagnose_chunking_invariance(*pipeline, prompt_tokens, params, requested_split,
                                         next_agent_id);
        }
        next_agent_id += 2;

        const auto before = full_rollout(*pipeline, prompt_tokens, params, eos_id, im_end_id,
                                         next_agent_id++, /*use_cache=*/false, nullptr, {});
        const auto after = full_rollout(*pipeline, prompt_tokens, params, eos_id, im_end_id,
                                        next_agent_id++, /*use_cache=*/prefix_matches, prefix_agent.get(),
                                        prefix_tokens);

        bool ok = true;
        if (before.prefill_logits.size() != after.prefill_logits.size()) {
            std::printf("  FAIL: prefill logits size differs (%zu vs %zu)\n",
                       before.prefill_logits.size(), after.prefill_logits.size());
            ok = false;
        } else if (before.prefill_logits != after.prefill_logits) {
            std::size_t first_diff = 0;
            for (; first_diff < before.prefill_logits.size(); ++first_diff) {
                if (before.prefill_logits[first_diff] != after.prefill_logits[first_diff]) break;
            }
            std::printf("  FAIL: prefill logits differ at index %zu (%.9g vs %.9g)\n", first_diff,
                       static_cast<double>(before.prefill_logits[first_diff]),
                       static_cast<double>(after.prefill_logits[first_diff]));
            ok = false;
        } else {
            std::printf("  prefill logits: bit-identical (%zu floats)\n",
                       before.prefill_logits.size());
        }

        if (before.rollout != after.rollout) {
            std::printf("  FAIL: rollout token sequence differs (lengths %zu vs %zu)\n",
                       before.rollout.size(), after.rollout.size());
            const std::size_t n = std::min(before.rollout.size(), after.rollout.size());
            for (std::size_t i = 0; i < n; ++i) {
                if (before.rollout[i] != after.rollout[i]) {
                    std::printf("    first divergence at token %zu: %u vs %u\n", i,
                               before.rollout[i], after.rollout[i]);
                    break;
                }
            }
            ok = false;
        } else {
            std::printf("  rollout: bit-identical (%zu tokens)\n", before.rollout.size());
            std::string text;
            for (const auto id : before.rollout) text += pipeline->getTokenizer().decodeToken(id);
            std::printf("  decoded: %.300s%s\n", text.c_str(), text.size() > 300 ? "..." : "");
        }

        if (!ok) {
            ++failures;
        }
    }

    pipeline->schedulerEvict(*prefix_agent);

    std::printf("\n=== %s: %zu cases, %d failure(s) ===\n", which.c_str(), cases.size(), failures);
    return failures == 0 ? 0 : 1;
}
