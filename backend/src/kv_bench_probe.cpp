// kv_bench_probe.cpp -- LONG-CONTEXT decode cost, isolated from prefill.
//
// WHAT THIS IS FOR
// ----------------
// The existing ~117-token benchmark cannot show a KV-cache change working: at
// that length the KV read stream is a couple of percent of per-token traffic
// and is inside the noise. KV read traffic grows LINEARLY with position, so the
// only honest way to justify halving the KV element width is to measure the
// per-token cost as a function of context length and show the slope change.
//
// So this probe reports DECODE cost per token at a chosen prompt length, with
// prefill excluded from the timing, at 512 / 1024 / 2048 tokens of context.
//
// It deliberately measures BOTH cache classes, because they are different
// kernels and only one of them is what production runs:
//
//   paged  -- KVCacheStrategy::PAGED -> PagedKVCache -> compute_attention_paged_flat.
//             This is what backend/src/modules/assistant_service.cpp actually
//             gets: AgentSession's ctor defaults to PAGED and the service never
//             overrides it.
//   full   -- KVCacheStrategy::FULL  -> LinearKVCache -> fused_decode_head_*.
//             What demo_qwen3 and observer_cost_probe use.
//
// The decode step also matches production shape: the assistant drives
// schedulerDecodeStep -> forwardBatch even at concurrency 1, so `paged` uses
// forwardBatch with a one-element agent span rather than forward().
//
// DRAM bytes are NOT measured in-process. Run this under:
//   perf stat -a -e amd_umc/umc_cas_cmd.rd/,amd_umc/umc_cas_cmd.wr/ -- ...
// and multiply CAS commands by 64 bytes. Doing it out-of-process is what keeps
// the counter honest about the whole address stream rather than the part this
// binary can see.
//
// Usage:
//   ./kv_bench_probe <model.gguf> <prompt_tokens> <decode_tokens> [n_threads] [paged|full]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

/// Peak and current resident set, in KiB, straight from the kernel. VmHWM is
/// the high-water mark, which is the number that matters for "can we afford to
/// raise the context cap" -- current RSS can be below it after any free.
struct Rss {
    long cur_kib = 0;
    long peak_kib = 0;
};

Rss read_rss() {
    Rss r;
    std::FILE* f = std::fopen("/proc/self/status", "re");
    if (f == nullptr) {
        return r;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            r.cur_kib = std::strtol(line + 6, nullptr, 10);
        } else if (std::strncmp(line, "VmHWM:", 6) == 0) {
            r.peak_kib = std::strtol(line + 6, nullptr, 10);
        }
    }
    std::fclose(f);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: %s <model.gguf> <prompt_tokens> <decode_tokens> "
                     "[n_threads] [paged|full]\n",
                     argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int n_prompt = std::atoi(argv[2]);
    const int n_decode = std::atoi(argv[3]);
    const int n_threads = argc > 4 ? std::atoi(argv[4]) : 8;
    const std::string mode = argc > 5 ? argv[5] : "paged";
    const bool paged = (mode != "full");

    const char* kv_env = std::getenv("SENSEN_KV_DTYPE");
    const char* tier_env = std::getenv("SENSEN_KV_TIER");

    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();

    const Rss rss_before_load = read_rss();

    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) {
        std::fprintf(stderr, "FATAL: sensen load: %s\n", smodel_e.error().message().c_str());
        return 1;
    }
    auto smodel = std::move(*smodel_e);
    sensen::InferenceThreadPool pool((std::size_t)n_threads);

    const Rss rss_after_load = read_rss();

    // The KV cache is allocated against this, per slot -- it is the number the
    // context cap controls, so it must be the cap under test, not a fixed 2048.
    const std::size_t kv_max = (std::size_t)(n_prompt + n_decode + 64);

    // Deterministic, arbitrary tokens. Content is irrelevant to timing as long
    // as it is identical across configurations, and it is: same seed, same
    // recurrence, no dependence on the model or the KV dtype.
    std::vector<std::uint32_t> toks;
    toks.reserve((std::size_t)n_prompt);
    for (int i = 0; i < n_prompt; ++i) {
        toks.push_back(static_cast<std::uint32_t>(1000 + (i * 7919) % 20000));
    }

    std::printf("model=%s\n", model_path.c_str());
    std::printf("geometry: layers=%zu kv_heads=%zu head_dim=%zu hidden=%zu\n", cfg.num_layers,
                cfg.num_kv_heads, cfg.head_dim_calculated(), cfg.hidden_dim);
    std::printf("config: prompt=%d decode=%d threads=%d cache=%s kv_max_seq_len=%zu\n", n_prompt,
                n_decode, n_threads, paged ? "paged" : "full", kv_max);
    std::printf("env: SENSEN_KV_DTYPE=%s SENSEN_KV_TIER=%s\n", kv_env != nullptr ? kv_env : "(unset)",
                tier_env != nullptr ? tier_env : "(unset)");

    // ---- one warm run, discarded: sizes every thread_local scratch buffer ----
    {
        sensen::AgentSession warm(0, cfg.num_layers, cfg.num_heads, kv_max,
                                  cfg.head_dim_calculated(),
                                  paged ? sensen::KVCacheStrategy::PAGED
                                        : sensen::KVCacheStrategy::FULL,
                                  cfg.num_kv_heads);
        (void)smodel->forwardPrompt(toks, warm, pool);
        warm.current_pos = (std::size_t)n_prompt;
        for (int i = 0; i < 4; ++i) {
            const std::uint32_t t = 5000;
            if (paged) {
                sensen::AgentSession* a = &warm;
                (void)smodel->forwardBatch(std::span<const std::uint32_t>{&t, 1},
                                           std::span<sensen::AgentSession* const>{&a, 1}, pool);
            } else {
                (void)smodel->forward(t, warm, pool);
            }
            warm.current_pos++;
        }
    }

    // ---- the measured run ----
    sensen::AgentSession agent(1, cfg.num_layers, cfg.num_heads, kv_max,
                               cfg.head_dim_calculated(),
                               paged ? sensen::KVCacheStrategy::PAGED
                                     : sensen::KVCacheStrategy::FULL,
                               cfg.num_kv_heads);

    const auto t_pf = Clock::now();
    (void)smodel->forwardPrompt(toks, agent, pool);
    const double prefill_s = seconds_since(t_pf);
    agent.current_pos = (std::size_t)n_prompt;

    std::vector<double> per_token;
    per_token.reserve((std::size_t)n_decode);

    for (int i = 0; i < n_decode; ++i) {
        const std::uint32_t t = static_cast<std::uint32_t>(5000 + (i * 31) % 4000);
        const auto t0 = Clock::now();
        if (paged) {
            sensen::AgentSession* a = &agent;
            (void)smodel->forwardBatch(std::span<const std::uint32_t>{&t, 1},
                                       std::span<sensen::AgentSession* const>{&a, 1}, pool);
        } else {
            (void)smodel->forward(t, agent, pool);
        }
        per_token.push_back(seconds_since(t0));
        agent.current_pos++;
    }

    const Rss rss_end = read_rss();
    const std::size_t kv_bytes = agent.getMemoryUsage();

    std::vector<double> sorted = per_token;
    std::sort(sorted.begin(), sorted.end());
    const double best = sorted.front();
    const double med = sorted[sorted.size() / 2];
    double sum = 0.0;
    for (double s : sorted) {
        sum += s;
    }
    const double mean = sum / static_cast<double>(sorted.size());

    std::printf("\nprefill  : %8.3f s for %d tokens (%.1f tok/s)\n", prefill_s, n_prompt,
                (double)n_prompt / prefill_s);
    std::printf("decode   : best %8.3f ms   median %8.3f ms   mean %8.3f ms   per token\n",
                best * 1e3, med * 1e3, mean * 1e3);
    std::printf("decode   : %.2f tok/s (median)\n", 1.0 / med);
    std::printf("kv cache : %zu bytes reported by AgentSession::getMemoryUsage()  (%.2f MiB)\n",
                kv_bytes, (double)kv_bytes / (1024.0 * 1024.0));
    std::printf("kv/token : %.1f KiB at kv_max_seq_len=%zu\n",
                (double)kv_bytes / 1024.0 / (double)kv_max, kv_max);
    std::printf("rss      : before-load %ld KiB  after-load %ld KiB  end %ld KiB  peak %ld KiB\n",
                rss_before_load.cur_kib, rss_after_load.cur_kib, rss_end.cur_kib,
                rss_end.peak_kib);
    std::printf("rss      : peak %.1f MiB\n", (double)rss_end.peak_kib / 1024.0);

    // Machine-readable line, so a sweep script does not have to parse prose.
    std::printf("\nCSV,%s,%s,%d,%d,%s,%.6f,%.6f,%.6f,%zu,%ld\n",
                kv_env != nullptr ? kv_env : "unset", tier_env != nullptr ? tier_env : "unset",
                n_prompt, n_decode, paged ? "paged" : "full", best * 1e3, med * 1e3, mean * 1e3,
                kv_bytes, rss_end.peak_kib);
    return 0;
}
