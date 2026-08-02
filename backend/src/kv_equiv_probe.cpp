// kv_equiv_probe.cpp -- PAGED vs LINEAR KV cache equivalence, and greedy rollout.
//
// WHY THIS EXISTS
// ---------------
// `layer_parity_probe` and `numeric_audit_probe` both construct their
// AgentSession with `KVCacheStrategy::FULL`, so both validate the LinearKVCache
// path (`fused_decode_head_*`) against llama.cpp. Neither one touches
// `PagedKVCache` / `compute_attention_paged_flat`.
//
// That is a coverage hole, because production is the path they do NOT cover:
// AgentSession's constructor defaults to PAGED and
// backend/src/modules/assistant_service.cpp never overrides it. So the kernel
// the users actually run has, until now, had no parity gate at all.
//
// This probe closes the hole by transitivity. It runs the SAME prompt and the
// SAME greedy rollout through both cache classes in one process and compares
// them. Composed with the existing llama.cpp-vs-linear gate, agreement here is
// what makes a statement about the paged kernel:
//
//     paged == linear  (this probe)
//     linear == llama.cpp  (layer_parity_probe / numeric_audit_probe)
//     => paged == llama.cpp
//
// It is also the check that a KV-dtype change must not break: run it at
// SENSEN_KV_DTYPE=fp32 and again at fp16, and the two cache classes must agree
// with each other in BOTH configurations. If they agree at fp32 and diverge at
// fp16, exactly one of the two half kernels is wrong, which is a far sharper
// signal than a logit delta against llama.cpp that folds both in together.
//
// Usage: ./kv_equiv_probe <model.gguf> [n_rollout] [n_threads] [prompt_tokens]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;

namespace {

struct Stats {
    double max_abs = 0.0;
    double rel_l2 = 0.0;
    double cosine = 1.0;
};

Stats compare(const std::vector<float>& a, const std::vector<float>& b) {
    Stats s;
    if (a.size() != b.size() || a.empty()) {
        s.cosine = -2.0;  // sentinel: shape mismatch
        return s;
    }
    double num = 0.0;
    double den = 0.0;
    double da = 0.0;
    double db = 0.0;
    double dot = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = (double)a[i] - (double)b[i];
        s.max_abs = std::max(s.max_abs, std::abs(d));
        num += d * d;
        den += (double)a[i] * (double)a[i];
        da += (double)a[i] * (double)a[i];
        db += (double)b[i] * (double)b[i];
        dot += (double)a[i] * (double)b[i];
    }
    s.rel_l2 = (den > 0.0) ? std::sqrt(num / den) : 0.0;
    s.cosine = (da > 0.0 && db > 0.0) ? dot / (std::sqrt(da) * std::sqrt(db)) : 0.0;
    return s;
}

std::size_t argmax(const std::vector<float>& v) {
    return (std::size_t)(std::max_element(v.begin(), v.end()) - v.begin());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.gguf> [n_rollout] [n_threads] [prompt_tokens]\n",
                     argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int n_rollout = argc > 2 ? std::atoi(argv[2]) : 38;
    const int n_threads = argc > 3 ? std::atoi(argv[3]) : 8;
    const int n_prompt = argc > 4 ? std::atoi(argv[4]) : 64;

    const char* kv_env = std::getenv("SENSEN_KV_DTYPE");
    const char* tier_env = std::getenv("SENSEN_KV_TIER");

    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();
    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) {
        std::fprintf(stderr, "FATAL: sensen load: %s\n", smodel_e.error().message().c_str());
        return 1;
    }
    auto smodel = std::move(*smodel_e);
    sensen::InferenceThreadPool pool((std::size_t)n_threads);

    std::vector<std::uint32_t> toks;
    toks.reserve((std::size_t)n_prompt);
    for (int i = 0; i < n_prompt; ++i) {
        toks.push_back(static_cast<std::uint32_t>(1000 + (i * 7919) % 20000));
    }

    const std::size_t kv_max = (std::size_t)(n_prompt + n_rollout + 64);

    std::printf("model=%s\n", model_path.c_str());
    std::printf("env: SENSEN_KV_DTYPE=%s SENSEN_KV_TIER=%s\n",
                kv_env != nullptr ? kv_env : "(unset)", tier_env != nullptr ? tier_env : "(unset)");
    std::printf("prompt_tokens=%d rollout=%d threads=%d\n\n", n_prompt, n_rollout, n_threads);

    // ---- run a greedy rollout under one cache class, recording every step ----
    auto run = [&](sensen::KVCacheStrategy strat, std::vector<std::vector<float>>& logits_out,
                   std::vector<std::uint32_t>& tokens_out) {
        sensen::AgentSession agent(0, cfg.num_layers, cfg.num_heads, kv_max,
                                   cfg.head_dim_calculated(), strat, cfg.num_kv_heads);
        auto logits = smodel->forwardPrompt(toks, agent, pool);
        agent.current_pos = (std::size_t)n_prompt;
        logits_out.push_back(logits);
        std::uint32_t next = (std::uint32_t)argmax(logits);
        tokens_out.push_back(next);
        for (int i = 0; i < n_rollout; ++i) {
            // Drive the SAME entry point production drives: the assistant's
            // scheduler calls forwardBatch even at concurrency 1.
            sensen::AgentSession* a = &agent;
            auto res = smodel->forwardBatch(std::span<const std::uint32_t>{&next, 1},
                                            std::span<sensen::AgentSession* const>{&a, 1}, pool);
            agent.current_pos++;
            logits_out.push_back(res[0]);
            next = (std::uint32_t)argmax(res[0]);
            tokens_out.push_back(next);
        }
    };

    std::vector<std::vector<float>> lp;
    std::vector<std::vector<float>> lf;
    std::vector<std::uint32_t> tp;
    std::vector<std::uint32_t> tf;
    run(sensen::KVCacheStrategy::PAGED, lp, tp);
    run(sensen::KVCacheStrategy::FULL, lf, tf);

    // ---- compare ----
    std::printf("step |   paged tok |  linear tok | same |    max|diff| |      rel L2 |     cosine\n");
    std::printf("-----+-------------+-------------+------+--------------+-------------+-----------\n");
    bool all_same = true;
    double worst_rel = 0.0;
    double worst_cos = 1.0;
    for (std::size_t i = 0; i < lp.size() && i < lf.size(); ++i) {
        const Stats s = compare(lp[i], lf[i]);
        const bool same = (tp[i] == tf[i]);
        all_same = all_same && same;
        worst_rel = std::max(worst_rel, s.rel_l2);
        worst_cos = std::min(worst_cos, s.cosine);
        std::printf("%4zu | %11u | %11u | %4s | %12.4e | %11.4e | %.8f\n", i, tp[i], tf[i],
                    same ? "yes" : "NO", s.max_abs, s.rel_l2, s.cosine);
    }

    std::printf("\nARGMAX AGREEMENT : %s (%zu steps)\n", all_same ? "IDENTICAL" : "DIVERGED",
                lp.size());
    std::printf("WORST rel L2     : %.4e\n", worst_rel);
    std::printf("WORST cosine     : %.10f\n", worst_cos);
    std::printf("\nVERDICT: paged-vs-linear %s\n", all_same ? "EQUIVALENT" : "NOT EQUIVALENT");
    return all_same ? 0 : 1;
}
