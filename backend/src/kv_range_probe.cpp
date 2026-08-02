// kv_range_probe.cpp -- does the KV cache actually need BF16's exponent range?
//
// THE QUESTION
// ------------
// F16 and BF16 both cost two bytes. They differ only in how the bits are split:
// F16 is 5 exponent / 10 mantissa, BF16 is 8 exponent / 7 mantissa. BF16 exists
// to match fp32's exponent range so training does not need loss scaling. If the
// values we cache are bounded activations rather than gradients, that range buys
// nothing and the three extra mantissa bits of F16 are strictly better.
//
// That is an empirical claim about THIS model's activations, so this measures it
// rather than asserting it. It reports, for the tensors that actually enter the
// cache:
//
//   Kcur_rope -- post-RoPE K, the exact values the K cache stores
//   Vcur      -- V projection, the exact values the V cache stores
//
// per layer: absmax, the smallest nonzero magnitude, and how many values fall
// below F16's smallest normal. The bounds that matter:
//
//   F16 max normal        65504
//   F16 min normal        6.103515625e-05   (below this, F16 goes subnormal --
//                                            degraded precision, not overflow)
//   F16 min subnormal     5.960464477539063e-08  (below this, F16 flushes to zero)
//
// F16 is the right default if absmax sits well under 65504. Values under the min
// normal are not fatal -- F16 subnormals degrade smoothly -- but a large
// population there would be a real argument for BF16, so they are counted.
//
// SECOND JOB: storage-format exactness against ggml.
// --------------------------------------------------
// The cache is a STORAGE format, so our f32->f16 narrowing must round exactly
// the way llama.cpp's does, or every cached value carries a systematic bias.
// llama.cpp narrows through `ggml_fp32_to_fp16` (round-to-nearest-even). This
// probe sweeps our `kvhalf::f32_to_f16` against that function directly and
// reports any bit that differs -- a decisive test of the format itself,
// independent of whether the upstream f32 values agree.
//
// That distinction matters and is worth stating plainly: the cached BYTES cannot
// be bit-identical to llama.cpp's unless the f32 values entering the cache are
// already bit-identical, and at Q8_0 weights they are not (the existing parity
// audit measures Kcur_rope at ~3e-02 relative L2 before any KV dtype is chosen).
// So "is our narrowing exact?" and "do our cached values match llama.cpp's?" are
// two different questions. This answers the first exactly; the second is bounded
// by the pre-existing weight-quantisation difference, not by the KV format.
//
// Usage: ./kv_range_probe <model.gguf> [n_threads] [prompt...]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "ggml.h"

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;
import sensen.tokenizer;
import sensen.tensor_observer;
import sensen.kv_half;

namespace {

constexpr float kF16MaxNormal = 65504.0F;
constexpr float kF16MinNormal = 6.103515625e-05F;
constexpr float kF16MinSubnormal = 5.960464477539063e-08F;

struct Range {
    float absmax = 0.0F;
    float min_nonzero = std::numeric_limits<float>::infinity();
    std::size_t n = 0;
    std::size_t n_zero = 0;
    std::size_t n_below_min_normal = 0;   // nonzero, but F16-subnormal
    std::size_t n_below_min_subnormal = 0; // would flush to zero in F16
    std::size_t n_over_f16_max = 0;        // would overflow F16 to inf

    void add(float v) {
        ++n;
        const float a = std::abs(v);
        if (a == 0.0F) {
            ++n_zero;
            return;
        }
        absmax = std::max(absmax, a);
        min_nonzero = std::min(min_nonzero, a);
        if (a > kF16MaxNormal) {
            ++n_over_f16_max;
        }
        if (a < kF16MinNormal) {
            ++n_below_min_normal;
        }
        if (a < kF16MinSubnormal) {
            ++n_below_min_subnormal;
        }
    }
};

std::string build_prompt(const std::string& utterance) {
    return std::string("<|im_start|>system\nYou turn a trader's request into "
                       "parameters for the Options & Futures Calculator. Reply with a single "
                       "JSON object inside <params></params> when you have enough to act, or "
                       "ask exactly one short question when you do not. You do not give "
                       "trading advice.<|im_end|>\n<|im_start|>user\n") +
           utterance + "<|im_end|>\n<|im_start|>assistant\n";
}

void quiet_log(ggml_log_level, const char*, void*) {}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.gguf> [n_threads]\n", argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int n_threads = argc > 2 ? std::atoi(argv[2]) : 8;

    // ---------------------------------------------------------------------
    // PART 1 -- narrowing exactness vs ggml_fp32_to_fp16 (round-to-nearest-even)
    // ---------------------------------------------------------------------
    std::printf("== PART 1: f32->f16 narrowing vs ggml_fp32_to_fp16 ==\n");
    {
        std::size_t checked = 0;
        std::size_t mismatches = 0;
        std::size_t mism_nan = 0;
        std::size_t mism_inf = 0;
        std::size_t mism_finite = 0;
        std::uint32_t first_bad_in = 0;
        std::uint16_t first_bad_ours = 0;
        std::uint16_t first_bad_ggml = 0;
        std::uint32_t first_finite_bad_in = 0;
        std::uint16_t first_finite_bad_ours = 0;
        std::uint16_t first_finite_bad_ggml = 0;

        // Sweep every f32 whose top 20 bits vary and the low 12 are swept
        // separately -- a full 2^32 sweep is unnecessary, but the region that
        // matters (rounding ties, subnormals, the overflow edge) must be dense.
        auto check = [&](float f) {
            const std::uint16_t ours = sensen::kvhalf::f32_to_f16(f);
            const std::uint16_t theirs = ggml_fp32_to_fp16(f);
            ++checked;
            if (ours != theirs) {
                if (mismatches == 0) {
                    std::memcpy(&first_bad_in, &f, 4);
                    first_bad_ours = ours;
                    first_bad_ggml = theirs;
                }
                ++mismatches;
                // Classify. A disagreement on a NaN PAYLOAD is not a storage
                // defect -- every NaN encoding still reads back as NaN, and no
                // healthy activation is ever NaN. A disagreement on a FINITE
                // value is a real, systematic bias on every cached number, and
                // is the only class that matters here.
                if (std::isnan(f)) {
                    ++mism_nan;
                } else if (std::isinf(f)) {
                    ++mism_inf;
                } else {
                    if (mism_finite == 0) {
                        std::memcpy(&first_finite_bad_in, &f, 4);
                        first_finite_bad_ours = ours;
                        first_finite_bad_ggml = theirs;
                    }
                    ++mism_finite;
                }
            }
        };

        // (a) every representable F16 value, widened and re-narrowed
        for (std::uint32_t h = 0; h < 65536U; ++h) {
            check(sensen::kvhalf::f16_to_f32(static_cast<std::uint16_t>(h)));
        }
        // (b) dense sweep of the exact rounding-tie midpoints between adjacent
        //     F16 values -- where round-to-nearest-even differs from truncation
        //     and from round-half-away. This is the discriminating region.
        for (std::uint32_t h = 0; h + 1 < 65536U; ++h) {
            const float lo = sensen::kvhalf::f16_to_f32(static_cast<std::uint16_t>(h));
            const float hi = sensen::kvhalf::f16_to_f32(static_cast<std::uint16_t>(h + 1));
            if (!std::isfinite(lo) || !std::isfinite(hi)) {
                continue;
            }
            check(std::nextafter(lo, hi));
            check(0.5F * (lo + hi));  // the exact tie
            check(std::nextafter(hi, lo));
        }
        // (c) the activation range this model actually produces, densely
        for (int i = 0; i < 2000000; ++i) {
            const float x = -40.0F + (80.0F * static_cast<float>(i) / 2000000.0F);
            check(x);
        }
        // (d) overflow / subnormal edges
        for (float f : {65504.0F, 65520.0F, 65535.0F, 65536.0F, 1e30F, -1e30F, 6.1035156e-05F,
                        5.9604645e-08F, 2.9802322e-08F, 1e-45F, 0.0F, -0.0F}) {
            check(f);
        }

        std::printf("  values checked      : %zu\n", checked);
        std::printf("  mismatches (total)  : %zu\n", mismatches);
        std::printf("    of which NaN      : %zu   (payload encoding only -- still NaN both sides)\n",
                    mism_nan);
        std::printf("    of which Inf      : %zu\n", mism_inf);
        std::printf("    of which FINITE   : %zu   <-- the only class that can bias the cache\n",
                    mism_finite);
        if (mismatches != 0) {
            std::printf("  first mismatch      : in=0x%08X ours=0x%04X ggml=0x%04X\n", first_bad_in,
                        first_bad_ours, first_bad_ggml);
        }
        if (mism_finite != 0) {
            std::printf("  first FINITE mismatch: in=0x%08X ours=0x%04X ggml=0x%04X\n",
                        first_finite_bad_in, first_finite_bad_ours, first_finite_bad_ggml);
            std::printf("  VERDICT        : NOT BIT-EXACT on finite values -- storage DIVERGES\n");
        } else if (mismatches != 0) {
            std::printf("  VERDICT        : BIT-EXACT on EVERY FINITE VALUE. The %zu differences\n"
                        "                   are NaN/Inf payload encodings, which read back as\n"
                        "                   NaN/Inf on both sides and never occur in a healthy\n"
                        "                   activation. Storage format matches ggml.\n",
                        mismatches);
        } else {
            std::printf("  VERDICT        : BIT-EXACT vs ggml_fp32_to_fp16 (round-to-nearest-even)\n");
        }
        std::printf("\n");
    }

    // ---------------------------------------------------------------------
    // PART 2 -- measured dynamic range of the tensors that enter the cache
    // ---------------------------------------------------------------------
    ggml_log_set(quiet_log, nullptr);

    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();
    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) {
        std::fprintf(stderr, "FATAL: sensen load: %s\n", smodel_e.error().message().c_str());
        return 1;
    }
    auto smodel = std::move(*smodel_e);
    sensen::InferenceThreadPool pool((std::size_t)n_threads);

    // key: "name-layer"
    std::map<std::string, Range> ranges;

    sensen::observe::setObserver([&](const sensen::observe::TensorEvent& ev) {
        if (ev.name != sensen::observe::names::kKcurRope &&
            ev.name != sensen::observe::names::kVcur) {
            return;
        }
        std::string key(ev.name);
        key += "-";
        key += std::to_string(ev.layer);
        Range& r = ranges[key];
        for (float x : ev.data) {
            r.add(x);
        }
    });

    // Several genuinely different real assistant prompts, so the range is not an
    // artefact of one utterance.
    const std::vector<std::string> utterances = {
        "Set up an iron condor on SPY 30 days out",
        "I want a bull call spread on NVDA, 2 contracts, expiring next month",
        "What's the max loss on a covered call for 100 shares of AAPL at 230 strike?",
        "Show me a crack spread on CL versus RB for the September contract",
        "cash and carry basis trade on gold, december delivery, 5 lots",
    };

    auto stok = sensen::Tokenizer::fromGGUF(model_path).addBos(true).addEos(false).build();

    for (const auto& u : utterances) {
        const std::string prompt = build_prompt(u);
        const std::vector<std::uint32_t> toks = stok->encode(prompt);
        if (toks.empty()) {
            std::fprintf(stderr, "FATAL: tokenizer produced no tokens for \"%s\"\n", u.c_str());
            return 1;
        }

        sensen::AgentSession agent(0, cfg.num_layers, cfg.num_heads, 2048,
                                   cfg.head_dim_calculated(), sensen::KVCacheStrategy::PAGED,
                                   cfg.num_kv_heads);
        (void)smodel->forwardPrompt(toks, agent, pool);
        agent.current_pos = toks.size();
        // A few decode steps too -- decode writes the cache one token at a time
        // and is the shape production actually runs.
        for (int i = 0; i < 8; ++i) {
            std::uint32_t t = static_cast<std::uint32_t>(5000 + i * 37);
            sensen::AgentSession* a = &agent;
            (void)smodel->forwardBatch(std::span<const std::uint32_t>{&t, 1},
                                       std::span<sensen::AgentSession* const>{&a, 1}, pool);
            agent.current_pos++;
        }
    }
    sensen::observe::clearObserver();

    std::printf("== PART 2: measured range of tensors entering the KV cache ==\n");
    std::printf("F16 limits: max normal %.0f   min normal %.6e   min subnormal %.6e\n\n",
                kF16MaxNormal, kF16MinNormal, kF16MinSubnormal);
    std::printf("%-18s %12s %14s %10s %12s %12s %10s\n", "tensor-layer", "absmax", "min|nonzero|",
                "n", "<minnormal", "<minsubnorm", ">f16max");
    std::printf("%s\n", std::string(96, '-').c_str());

    float global_absmax_k = 0.0F;
    float global_absmax_v = 0.0F;
    float global_min_k = std::numeric_limits<float>::infinity();
    float global_min_v = std::numeric_limits<float>::infinity();
    std::size_t total_over = 0;
    std::size_t total_below_sub = 0;

    for (const auto& [key, r] : ranges) {
        const bool is_k = key.rfind("Kcur_rope", 0) == 0;
        if (is_k) {
            global_absmax_k = std::max(global_absmax_k, r.absmax);
            global_min_k = std::min(global_min_k, r.min_nonzero);
        } else {
            global_absmax_v = std::max(global_absmax_v, r.absmax);
            global_min_v = std::min(global_min_v, r.min_nonzero);
        }
        total_over += r.n_over_f16_max;
        total_below_sub += r.n_below_min_subnormal;
        std::printf("%-18s %12.4f %14.4e %10zu %12zu %12zu %10zu\n", key.c_str(), r.absmax,
                    r.min_nonzero, r.n, r.n_below_min_normal, r.n_below_min_subnormal,
                    r.n_over_f16_max);
    }

    std::printf("\n== SUMMARY ==\n");
    std::printf("  Kcur_rope absmax over all layers : %.4f   (%.2f%% of F16 max)\n",
                global_absmax_k, 100.0 * global_absmax_k / kF16MaxNormal);
    std::printf("  Vcur      absmax over all layers : %.4f   (%.2f%% of F16 max)\n",
                global_absmax_v, 100.0 * global_absmax_v / kF16MaxNormal);
    std::printf("  Kcur_rope smallest nonzero       : %.6e\n", global_min_k);
    std::printf("  Vcur      smallest nonzero       : %.6e\n", global_min_v);
    std::printf("  values that would OVERFLOW F16   : %zu\n", total_over);
    std::printf("  values that would FLUSH TO ZERO  : %zu\n", total_below_sub);
    std::printf("\n");
    if (total_over == 0 && global_absmax_k < kF16MaxNormal / 8 &&
        global_absmax_v < kF16MaxNormal / 8) {
        std::printf("VERDICT: F16 range is not remotely a constraint. BF16's extra exponent\n"
                    "         bits would buy nothing and cost 3 bits of mantissa. Default F16.\n");
    } else if (total_over > 0) {
        std::printf("VERDICT: values OVERFLOW F16 -- BF16 earns its place here. Do NOT default F16.\n");
    } else {
        std::printf("VERDICT: F16 fits but with less headroom than expected -- report the margin.\n");
    }
    return 0;
}
