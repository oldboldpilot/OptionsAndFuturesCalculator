/**
 * DIAGNOSTIC, NOT A SHIPPED FEATURE.
 *
 * Built to verify a system-prompt KV prefix-reuse fast path (share a
 * precomputed prefix's KV into a fresh AgentSession via
 * MultiLayerKVCache::share_from, then continue the forward pass over only
 * the request's own tokens via schedulerPrefillContinue, instead of
 * recomputing the fixed system-prompt prefix's KV on every request). That
 * change was NOT shipped -- see the investigation this probe now exists to
 * document.
 *
 * `diagnose_chunking_invariance()` below isolates the root cause with NO
 * caching/sharing involved at all: it splits ONE prompt's own forward pass
 * into `schedulerPrefill(first K tokens)` followed by
 * `schedulerPrefillContinue(remaining tokens)`, against the SAME single
 * agent, and compares the result to one `schedulerPrefill(all tokens)`
 * call. On this build (CPU-only, Q8_0-quantized model -- both production
 * assistants' checkpoints), THEY DIFFER, and by an amount large enough to
 * flip the greedy argmax and diverge the full generated token sequence.
 *
 * Root cause, traced into sensen (`backend/sensen`, READ-ONLY for this
 * repo): `GEMM::matvecQuantizedBatch`'s Q8_0 fast path
 * (`matvecQ8_0_Int8BatchSlice`, `src/gemm_kernels.cppm`) peels the M
 * activation rows of a batched forward pass into groups of 8, then 4, then
 * 2, then 1, STARTING FROM ROW 0 OF THE BATCH -- so which micro-kernel
 * template (`q8_0_Int8Batch_VNNI<8,8>` vs `<4,4>` vs `<2,2>` vs the
 * single-row `matvecQ8_0_Int8Slice`) computes a given LOGICAL row depends
 * on the TOTAL row count M of whichever call it was part of, not on that
 * row's own position. A prefix's tail rows near an 8-row boundary are
 * therefore computed by a different micro-kernel (different SIMD
 * accumulation/rounding) when forwarded alone (M = prefix length) than
 * when forwarded as the leading rows of a longer one-shot prompt (M =
 * whole prompt). This is the CPU/Q8_0 analogue of the batch-invariant-GEMM-
 * dispatch class of bug this codebase has already fixed repeatedly on the
 * CUDA decode/prefill paths (see CLAUDE.md's history) -- but
 * `GEMM::matvecQuantizedBatch` has no `deterministic`-gated batch-invariant
 * mode, and `TransformerBlock::forward_prefill`'s call chain
 * (`MultiHeadAttention::forward_prefill_flat`, `FeedForwardNetwork::
 * forward_batch_flat`) carries no `deterministic` parameter at all, so
 * `GenerationConfig::deterministic=true` (which both assistants already
 * set) has ZERO effect on this CPU-only build. sensen also ships a
 * purpose-built `sensen.batch_invariant` module ("bitwise batch-invariant
 * GEMM/softmax/attention for reproducible serving") that is NOT wired into
 * this call chain at all -- its only production consumer today is an
 * unrelated CUDA dispatch-resolution pin in llama_model.cpp.
 *
 * Consequence: ANY caller that splits a forward pass across two calls
 * against the same evolving KV cache -- not just a hypothetical prefix
 * cache, but the PRE-EXISTING `schedulerPrefillContinue` API itself (its
 * own doc comment describes it as backing a genuine stateful multi-turn
 * chat continuation, H5) -- can silently diverge from what a single
 * one-shot forward over the equivalent full history would have produced,
 * on this CPU/Q8_0 configuration. That is a pre-existing, broader property
 * of sensen's CPU serving path this investigation surfaced, not something
 * introduced here.
 *
 * What sensen would need before a prefix-KV-reuse (or any chunked-forward)
 * optimization is safe on this path: `matvecQ8_0_Int8BatchSlice` (or a
 * `deterministic`-gated sibling of it, mirroring the CUDA fixes'
 * `m_disp`-pinning pattern) needs to compute a given row's output the same
 * way regardless of which other rows share its batch -- e.g. by fixing the
 * micro-kernel grouping to the row's OWN index modulo 8 rather than an
 * offset from the batch start, or by exposing `sensen.batch_invariant`'s
 * primitives through this call chain -- verified against exactly the
 * `diagnose_chunking_invariance` check this probe already runs.
 *
 * Not part of the production binary or the required test suite; a
 * diagnostic/regression tool, matching this repo's existing
 * decode_golden_probe.cpp / assistant_throughput_probe.cpp convention. Once
 * sensen closes this gap, re-running this probe (it should then report
 * IDENTICAL for every case) is the acceptance check before reattempting the
 * prefix-cache feature.
 *
 *   prefix_cache_verify_probe <model.gguf> --system mortgage|strategy
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
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
                     "usage: prefix_cache_verify_probe <model.gguf> --system mortgage|strategy\n");
        return 2;
    }
    const std::string model_path = argv[1];
    std::string which = "mortgage";
    for (int i = 2; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--system" && i + 1 < argc) {
            which = argv[++i];
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

        diagnose_chunking_invariance(*pipeline, prompt_tokens, params, prefix_tokens.size(),
                                     next_agent_id);
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
