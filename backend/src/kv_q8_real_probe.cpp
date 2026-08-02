// kv_q8_real_probe.cpp -- validates sensen.kv_q8's ACTUAL
// quantise_group_q8/dequantise_row_q8 round trip against REAL captured
// Kcur_rope / Vcur activations, not a synthetic proxy.
//
// WHY THIS EXISTS: WP1's synthetic round-trip test (test_kv_q8.cpp) measures
// relL2 ~1/(255*sqrt(12)) ~ 3.9e-3 for every per-block distribution shape
// tried, because that is the quantiser's OWN grid noise floor for data that
// fills its local block range roughly uniformly -- it is insensitive to
// synthetic shape by construction. Real K has persistent, temporally STABLE
// per-channel outliers (QK-norm's learned per-channel gain), so an outlier
// channel's 16-token block range is SMALL relative to |x| even though its
// magnitude is large -- it dominates the relL2 denominator while
// contributing little to the numerator, pulling the aggregate below the
// uniform floor. No synthetic i.i.d.-per-block generator can reproduce that
// without literally encoding a large-DC/small-variance channel structure.
// This probe measures the real thing instead of guessing at it.
//
// Reuses kv_gran_probe.cpp's harness pattern (same model load, same real
// assistant prompts, same tensor_observer attachment) -- it does not
// reinvent capture. What is NEW here is that after capture, this probe
// feeds the exact [n_tokens][dim] token-major F16-staged buffers straight
// into sensen::kvq8::quantise_group_q8 / dequantise_row_q8 (the ACTUAL WP1
// functions kv_cache.cppm calls), not a hand-rolled affine simulation.
//
// Grouping: quantise_group_q8's dim parameter need not equal one KV head's
// head_dim -- each channel is quantised fully independently of every other
// channel regardless of how many are passed in one call. Passing dim =
// n_kv_heads*head_dim (the full per-token row) in one call per 16-token
// block is mathematically identical, channel-by-channel, to calling it once
// per (head, block) with dim=head_dim -- it only changes the call-count.
//
// BLOCK_SIZE=16 (sensen::BLOCK_SIZE): a real cache never Q8-quantises a
// PARTIAL trailing block (kv_cache.cppm only promotes OPEN -> QUANTISED once
// the 16th token lands), so the trailing < 16 remainder here is dropped
// exactly like the real container drops it.
//
// Usage: SENSEN_KV_DTYPE=fp32 ./kv_q8_real_probe <model.gguf> [n_threads]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;
import sensen.tokenizer;
import sensen.tensor_observer;
import sensen.kv_half;
import sensen.kv_q8;

namespace {

constexpr std::size_t kBlockSize = 16;  // sensen::BLOCK_SIZE

std::string build_prompt(const std::string& utterance) {
    return std::string("<|im_start|>system\nYou turn a trader's request into "
                       "parameters for the Options & Futures Calculator. Reply with a single "
                       "JSON object inside <params></params> when you have enough to act, or "
                       "ask exactly one short question when you do not. You do not give "
                       "trading advice.<|im_end|>\n<|im_start|>user\n") +
           utterance + "<|im_end|>\n<|im_start|>assistant\n";
}

struct Stream {
    std::size_t width = 0;
    std::vector<float> data;  // rows * width, token-major (row r = token r's `width` values)
    [[nodiscard]] std::size_t rows() const { return width ? data.size() / width : 0; }
};

struct ErrAcc {
    double num = 0.0, den = 0.0;
    std::size_t n_elems = 0;
    std::size_t n_blocks = 0;
    void add(float x, float xh) {
        const double d = double(x) - double(xh);
        num += d * d;
        den += double(x) * double(x);
        ++n_elems;
    }
    [[nodiscard]] double relL2() const { return den > 0 ? std::sqrt(num / den) : 0.0; }
};

// Manual affine-u8 quantiser mirroring kv_gran_probe.cpp's quant_affine
// exactly (no f16 staging, no f16 scale narrow, raw fp32 throughout) --
// used ONLY as a cross-check oracle to isolate a capture bug from a
// quantise_group_q8-specific bug.
inline float quant_affine_oracle(float x, float mn, float mx) {
    const float s = (mx - mn) / 255.0F;
    if (s == 0.0F) return mn;
    float q = std::round((x - mn) / s);
    q = std::max(0.0F, std::min(255.0F, q));
    return mn + q * s;
}

// Runs the REAL sensen::kvq8::quantise_group_q8/dequantise_row_q8 round trip
// over every complete kBlockSize-token block in `s`, accumulating relL2.
// ALSO runs the manual quant_affine_oracle cross-check on the identical
// blocks into acc_oracle, so a capture bug (both wrong) can be told apart
// from a quantise_group_q8-specific bug (only acc wrong).
void simulateRealQ8(const Stream& s, ErrAcc& acc, ErrAcc& acc_oracle) {
    const std::size_t dim = s.width;
    if (dim == 0) return;
    std::vector<std::uint16_t> staged(kBlockSize * dim);
    std::vector<std::uint8_t> codes(kBlockSize * dim);
    std::vector<std::uint16_t> scales(dim);
    std::vector<std::uint16_t> mins(dim);
    std::vector<float> out(dim);

    const std::size_t n_full_blocks = s.rows() / kBlockSize;  // drop the partial tail, like the real cache.
    for (std::size_t b = 0; b < n_full_blocks; ++b) {
        const float* block = s.data.data() + b * kBlockSize * dim;
        for (std::size_t i = 0; i < kBlockSize * dim; ++i) {
            staged[i] = sensen::kvhalf::f32_to_f16(block[i]);
        }
        sensen::kvq8::quantise_group_q8(staged.data(), kBlockSize, dim, codes.data(), scales.data(),
                                        mins.data());
        for (std::size_t t = 0; t < kBlockSize; ++t) {
            sensen::kvq8::dequantise_row_q8(codes.data() + t * dim, scales.data(), mins.data(), dim,
                                            out.data());
            for (std::size_t d = 0; d < dim; ++d) {
                acc.add(block[t * dim + d], out[d]);
            }
        }
        ++acc.n_blocks;

        // Oracle cross-check on the SAME block, per channel min/max over the
        // SAME kBlockSize tokens.
        for (std::size_t d = 0; d < dim; ++d) {
            float mn = block[d], mx = block[d];
            for (std::size_t t = 1; t < kBlockSize; ++t) {
                const float v = block[t * dim + d];
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            for (std::size_t t = 0; t < kBlockSize; ++t) {
                const float v = block[t * dim + d];
                acc_oracle.add(v, quant_affine_oracle(v, mn, mx));
            }
        }
        ++acc_oracle.n_blocks;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.gguf> [n_threads]\n", argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int n_threads = argc > 2 ? std::atoi(argv[2]) : 8;

    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();
    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) {
        std::fprintf(stderr, "FATAL: sensen load: %s\n", smodel_e.error().message().c_str());
        return 1;
    }
    auto smodel = std::move(*smodel_e);
    sensen::InferenceThreadPool pool((std::size_t)n_threads);

    const std::size_t n_kv_heads = cfg.num_kv_heads;
    const std::size_t head_dim = cfg.head_dim_calculated();
    const std::size_t width = n_kv_heads * head_dim;
    const std::size_t n_layers = cfg.num_layers;

    const std::vector<std::string> utterances = {
        "Set up an iron condor on SPY 30 days out",
        "I want a bull call spread on NVDA, 2 contracts, expiring next month",
        "What's the max loss on a covered call for 100 shares of AAPL at 230 strike?",
        "Show me a crack spread on CL versus RB for the September contract",
        "cash and carry basis trade on gold, december delivery, 5 lots",
    };
    const std::size_t n_prompts = utterances.size();

    std::vector<std::vector<std::array<Stream, 2>>> streams(
        n_prompts, std::vector<std::array<Stream, 2>>(n_layers));

    std::size_t cur_prompt = 0;
    sensen::observe::setObserver([&](const sensen::observe::TensorEvent& ev) {
        int which = -1;
        if (ev.name == sensen::observe::names::kKcurRope) which = 0;
        else if (ev.name == sensen::observe::names::kVcur) which = 1;
        else return;
        if (ev.layer < 0 || (std::size_t)ev.layer >= n_layers) return;
        Stream& s = streams[cur_prompt][(std::size_t)ev.layer][(std::size_t)which];
        s.width = ev.cols;
        s.data.insert(s.data.end(), ev.data.begin(), ev.data.end());
    });

    auto stok = sensen::Tokenizer::fromGGUF(model_path).addBos(true).addEos(false).build();

    for (cur_prompt = 0; cur_prompt < n_prompts; ++cur_prompt) {
        const std::string prompt = build_prompt(utterances[cur_prompt]);
        const std::vector<std::uint32_t> toks = stok->encode(prompt);
        if (toks.empty()) {
            std::fprintf(stderr, "FATAL: no tokens\n");
            return 1;
        }
        sensen::AgentSession agent(0, cfg.num_layers, cfg.num_heads, 2048,
                                   cfg.head_dim_calculated(), sensen::KVCacheStrategy::PAGED,
                                   cfg.num_kv_heads);
        (void)smodel->forwardPrompt(toks, agent, pool);
        agent.current_pos = toks.size();
        // Enough decode steps to cross several 16-token block boundaries.
        for (int i = 0; i < 40; ++i) {
            std::uint32_t t = static_cast<std::uint32_t>(5000 + i * 37);
            sensen::AgentSession* a = &agent;
            (void)smodel->forwardBatch(std::span<const std::uint32_t>{&t, 1},
                                       std::span<sensen::AgentSession* const>{&a, 1}, pool);
            agent.current_pos++;
        }
    }
    sensen::observe::clearObserver();

    std::printf("model: %zu layers, %zu kv heads x %zu head_dim (width %zu), %zu prompts, "
               "BLOCK_SIZE=%zu\n\n",
               n_layers, n_kv_heads, head_dim, width, n_prompts, kBlockSize);

    ErrAcc k_total, v_total, k_oracle_total, v_oracle_total;
    std::printf("%5s %16s %16s %16s %16s\n", "layer", "K relL2(q8)", "K relL2(oracle)",
                "V relL2(q8)", "V relL2(oracle)");
    for (std::size_t L = 0; L < n_layers; ++L) {
        ErrAcc k_layer, v_layer, k_oracle, v_oracle;
        for (std::size_t p = 0; p < n_prompts; ++p) {
            simulateRealQ8(streams[p][L][0], k_layer, k_oracle);
            simulateRealQ8(streams[p][L][1], v_layer, v_oracle);
        }
        std::printf("%5zu %16.6e %16.6e %16.6e %16.6e   (K blocks=%zu, V blocks=%zu)\n", L,
                    k_layer.relL2(), k_oracle.relL2(), v_layer.relL2(), v_oracle.relL2(),
                    k_layer.n_blocks, v_layer.n_blocks);
        k_total.num += k_layer.num;
        k_total.den += k_layer.den;
        k_total.n_elems += k_layer.n_elems;
        k_total.n_blocks += k_layer.n_blocks;
        v_total.num += v_layer.num;
        v_total.den += v_layer.den;
        v_total.n_elems += v_layer.n_elems;
        v_total.n_blocks += v_layer.n_blocks;
        k_oracle_total.num += k_oracle.num;
        k_oracle_total.den += k_oracle.den;
        v_oracle_total.num += v_oracle.num;
        v_oracle_total.den += v_oracle.den;
    }

    std::printf("\n== AGGREGATE over all %zu layers ==\n", n_layers);
    std::printf("  K: relL2(quantise_group_q8)=%.6e   relL2(oracle quant_affine)=%.6e  (blocks=%zu, elements=%zu)\n",
               k_total.relL2(), k_oracle_total.relL2(), k_total.n_blocks, k_total.n_elems);
    std::printf("  V: relL2(quantise_group_q8)=%.6e   relL2(oracle quant_affine)=%.6e  (blocks=%zu, elements=%zu)\n",
               v_total.relL2(), v_oracle_total.relL2(), v_total.n_blocks, v_total.n_elems);
    // Target updated 2026-08-02: the module moved from a u8 zero-point to an
    // f16 min (affine x=s*code+m), specifically because the u8 zero-point
    // could not represent real K's large-stable-magnitude/narrow-local-range
    // outlier channels (see kv_q8.cppm's module header). The oracle
    // cross-check above (same real captured blocks, a min/max-direct affine
    // parameterisation with no representability cliff) measured K=1.26e-3,
    // V=3.66e-3 on this exact real data -- quantise_group_q8 should now land
    // near those values instead of the old zero-point scheme's target.
    std::printf("  target K ~1.26e-3, V ~3.66e-3 (+/-20%%): K band [%.4e, %.4e], V band [%.4e, %.4e]\n",
               1.26e-3 * 0.8, 1.26e-3 * 1.2, 3.66e-3 * 0.8, 3.66e-3 * 1.2);
    return 0;
}
