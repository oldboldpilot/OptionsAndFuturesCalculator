// kv_gran_probe.cpp -- measurement probe for the 8-bit KV cache DESIGN question:
// what scale granularity does THIS model's K and V actually need?
//
// The published finding (KIVI et al.) is that K should be quantised PER-CHANNEL
// (persistent outlier channels, amplified through softmax) and V PER-TOKEN.
// This probe measures whether Qwen3-0.6B's cached tensors actually look like
// that, instead of taking it on faith:
//
//   1. Per layer, for Kcur_rope and Vcur: the per-CHANNEL absmax distribution
//      (channel = one column of the [tokens, num_kv_heads*head_dim] stream,
//      i.e. one (kv_head, dim) coordinate) versus the per-TOKEN-ROW absmax
//      distribution (row = one (token, kv_head) row of head_dim values --
//      the granularity the existing CUDA Int8PagedKVPoolManager uses).
//      Reported as max/median and p99/median ratios.
//   2. Outlier-channel PERSISTENCE: the top-16 channels per prompt, and how
//      many of them are shared across all prompts. A per-channel scheme is
//      only justified if the same channels are hot on every prompt.
//   3. Skew: per-channel and per-row (max+min)/(max-min) asymmetry, to decide
//      symmetric vs asymmetric (zero-point) quantisation.
//   4. Direct simulation of candidate int8 schemes on the exact values the
//      cache would store, reporting relative L2 per tensor per layer:
//        row-sym     : per (token, kv_head) row absmax, symmetric   (CUDA pool / QuantizedKVCache convention)
//        row-asym    : per (token, kv_head) row min/max affine u8
//        row32-sym   : per 32-element sub-block within a row, symmetric (llama.cpp Q8_0-KV convention)
//        chan16-sym  : per (channel, 16-token block) absmax, symmetric (KIVI-style, group = BLOCK_SIZE)
//        chan16-asym : per (channel, 16-token block) min/max affine u8
//
// Methodology matches kv_range_probe.cpp: attach the tensor observer, run the
// real assistant prompts through prefill + a few decode steps, accumulate the
// exact Kcur_rope / Vcur values that enter the cache.
//
// Usage: ./kv_gran_probe <model.gguf> [n_threads]

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;
import sensen.tokenizer;
import sensen.tensor_observer;

namespace {

constexpr std::size_t kTokenBlock = 16;  // matches sensen::BLOCK_SIZE (paged cache)
constexpr std::size_t kRowSub = 32;      // Q8_0-style sub-block within a row

std::string build_prompt(const std::string& utterance) {
    return std::string("<|im_start|>system\nYou turn a trader's request into "
                       "parameters for the Options & Futures Calculator. Reply with a single "
                       "JSON object inside <params></params> when you have enough to act, or "
                       "ask exactly one short question when you do not. You do not give "
                       "trading advice.<|im_end|>\n<|im_start|>user\n") +
           utterance + "<|im_end|>\n<|im_start|>assistant\n";
}

// One tensor's token stream for one prompt: rows of width `width` appended in
// cache order (prefill rows then decode rows).
struct Stream {
    std::size_t width = 0;
    std::vector<float> data;  // rows * width
    [[nodiscard]] std::size_t rows() const { return width ? data.size() / width : 0; }
};

// ---- int8 helpers -----------------------------------------------------------

inline float quant_sym(float x, float s) {
    if (s == 0.0F) return 0.0F;
    float q = std::round(x / s);
    q = std::max(-127.0F, std::min(127.0F, q));
    return q * s;
}

// affine u8: qhat = min + s*round((x-min)/s), s=(max-min)/255
inline float quant_affine(float x, float mn, float mx) {
    const float s = (mx - mn) / 255.0F;
    if (s == 0.0F) return mn;
    float q = std::round((x - mn) / s);
    q = std::max(0.0F, std::min(255.0F, q));
    return mn + q * s;
}

struct ErrAcc {
    double num = 0.0, den = 0.0;
    void add(float x, float xh) {
        const double d = double(x) - double(xh);
        num += d * d;
        den += double(x) * double(x);
    }
    [[nodiscard]] double relL2() const { return den > 0 ? std::sqrt(num / den) : 0.0; }
};

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

    // streams[prompt][layer][0=K,1=V]
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
        for (int i = 0; i < 24; ++i) {  // enough decode steps to cross a 16-token block boundary
            std::uint32_t t = static_cast<std::uint32_t>(5000 + i * 37);
            sensen::AgentSession* a = &agent;
            (void)smodel->forwardBatch(std::span<const std::uint32_t>{&t, 1},
                                       std::span<sensen::AgentSession* const>{&a, 1}, pool);
            agent.current_pos++;
        }
    }
    sensen::observe::clearObserver();

    // ------------------------------------------------------------------
    // Analysis
    // ------------------------------------------------------------------
    std::printf("model: %zu layers, %zu kv heads x %zu head_dim (width %zu), %zu prompts\n\n",
                n_layers, n_kv_heads, head_dim, width, n_prompts);

    const char* tname[2] = {"K", "V"};

    // Aggregates across layers for the summary.
    struct LayerSummary {
        double chan_ratio, chan_p99_ratio, row_ratio;
        double persist_frac;      // fraction of union-top16 channels hot in ALL prompts
        double chan_asym, row_asym;
        double e_row_sym, e_row_asym, e_row32_sym, e_chan16_sym, e_chan16_asym;
    };
    std::vector<std::array<LayerSummary, 2>> per_layer(n_layers);

    for (std::size_t L = 0; L < n_layers; ++L) {
        for (int w = 0; w < 2; ++w) {
            // --- channel stats over all prompts ---
            std::vector<float> chan_absmax(width, 0.0F);
            std::vector<float> chan_min(width, std::numeric_limits<float>::infinity());
            std::vector<float> chan_max(width, -std::numeric_limits<float>::infinity());
            // per-prompt channel absmax for persistence
            std::vector<std::vector<float>> pp_chan(n_prompts, std::vector<float>(width, 0.0F));
            // per-row absmax + min/max
            std::vector<float> row_absmax;  // one per (token, kv_head)
            double row_asym_sum = 0.0;
            std::size_t row_asym_n = 0;

            for (std::size_t p = 0; p < n_prompts; ++p) {
                const Stream& s = streams[p][L][(std::size_t)w];
                const std::size_t R = s.rows();
                for (std::size_t r = 0; r < R; ++r) {
                    const float* row = s.data.data() + r * width;
                    for (std::size_t h = 0; h < n_kv_heads; ++h) {
                        float am = 0.0F, mn = std::numeric_limits<float>::infinity(),
                              mx = -std::numeric_limits<float>::infinity();
                        for (std::size_t d = 0; d < head_dim; ++d) {
                            const float x = row[h * head_dim + d];
                            const float a = std::fabs(x);
                            am = std::max(am, a);
                            mn = std::min(mn, x);
                            mx = std::max(mx, x);
                        }
                        row_absmax.push_back(am);
                        if (mx > mn) {
                            row_asym_sum += std::fabs((mx + mn) / (mx - mn));
                            ++row_asym_n;
                        }
                    }
                    for (std::size_t c = 0; c < width; ++c) {
                        const float x = row[c];
                        const float a = std::fabs(x);
                        chan_absmax[c] = std::max(chan_absmax[c], a);
                        pp_chan[p][c] = std::max(pp_chan[p][c], a);
                        chan_min[c] = std::min(chan_min[c], x);
                        chan_max[c] = std::max(chan_max[c], x);
                    }
                }
            }

            auto ratio_stats = [](std::vector<float> v, double& r_max, double& r_p99) {
                std::sort(v.begin(), v.end());
                const float med = v[v.size() / 2];
                const float mx = v.back();
                const float p99 = v[(std::size_t)((double)(v.size() - 1) * 0.99)];
                r_max = med > 0 ? mx / med : 0.0;
                r_p99 = med > 0 ? p99 / med : 0.0;
            };
            double chan_ratio, chan_p99, row_ratio, row_p99;
            ratio_stats(chan_absmax, chan_ratio, chan_p99);
            ratio_stats(row_absmax, row_ratio, row_p99);

            // persistence: top-16 channels per prompt
            auto topk = [&](const std::vector<float>& v) {
                std::vector<std::size_t> idx(v.size());
                for (std::size_t i = 0; i < v.size(); ++i) idx[i] = i;
                std::partial_sort(idx.begin(), idx.begin() + 16, idx.end(),
                                  [&](std::size_t a, std::size_t b) { return v[a] > v[b]; });
                return std::set<std::size_t>(idx.begin(), idx.begin() + 16);
            };
            std::set<std::size_t> uni, inter = topk(pp_chan[0]);
            for (std::size_t p = 0; p < n_prompts; ++p) {
                auto t = topk(pp_chan[p]);
                uni.insert(t.begin(), t.end());
                std::set<std::size_t> tmp;
                for (std::size_t c : inter) {
                    if (t.count(c) != 0) tmp.insert(c);
                }
                inter = tmp;
            }
            const double persist = uni.empty() ? 0.0 : (double)inter.size() / 16.0;

            double chan_asym_sum = 0.0;
            std::size_t chan_asym_n = 0;
            for (std::size_t c = 0; c < width; ++c) {
                if (chan_max[c] > chan_min[c]) {
                    chan_asym_sum += std::fabs((chan_max[c] + chan_min[c]) / (chan_max[c] - chan_min[c]));
                    ++chan_asym_n;
                }
            }

            // --- quant simulations ---
            ErrAcc e_row_sym, e_row_asym, e_row32, e_c16_sym, e_c16_asym;
            for (std::size_t p = 0; p < n_prompts; ++p) {
                const Stream& s = streams[p][L][(std::size_t)w];
                const std::size_t R = s.rows();
                // row-granular schemes
                for (std::size_t r = 0; r < R; ++r) {
                    const float* row = s.data.data() + r * width;
                    for (std::size_t h = 0; h < n_kv_heads; ++h) {
                        const float* hr = row + h * head_dim;
                        float am = 0.0F, mn = hr[0], mx = hr[0];
                        for (std::size_t d = 0; d < head_dim; ++d) {
                            am = std::max(am, std::fabs(hr[d]));
                            mn = std::min(mn, hr[d]);
                            mx = std::max(mx, hr[d]);
                        }
                        const float srow = am / 127.0F;
                        for (std::size_t d = 0; d < head_dim; ++d) {
                            e_row_sym.add(hr[d], quant_sym(hr[d], srow));
                            e_row_asym.add(hr[d], quant_affine(hr[d], mn, mx));
                        }
                        // 32-elem sub-blocks
                        for (std::size_t b0 = 0; b0 < head_dim; b0 += kRowSub) {
                            float bam = 0.0F;
                            const std::size_t be = std::min(head_dim, b0 + kRowSub);
                            for (std::size_t d = b0; d < be; ++d)
                                bam = std::max(bam, std::fabs(hr[d]));
                            const float sb = bam / 127.0F;
                            for (std::size_t d = b0; d < be; ++d)
                                e_row32.add(hr[d], quant_sym(hr[d], sb));
                        }
                    }
                }
                // channel-granular schemes, groups of kTokenBlock tokens
                for (std::size_t g0 = 0; g0 < R; g0 += kTokenBlock) {
                    const std::size_t ge = std::min(R, g0 + kTokenBlock);
                    for (std::size_t c = 0; c < width; ++c) {
                        float am = 0.0F, mn = std::numeric_limits<float>::infinity(),
                              mx = -std::numeric_limits<float>::infinity();
                        for (std::size_t r = g0; r < ge; ++r) {
                            const float x = s.data[r * width + c];
                            am = std::max(am, std::fabs(x));
                            mn = std::min(mn, x);
                            mx = std::max(mx, x);
                        }
                        const float sc = am / 127.0F;
                        for (std::size_t r = g0; r < ge; ++r) {
                            const float x = s.data[r * width + c];
                            e_c16_sym.add(x, quant_sym(x, sc));
                            e_c16_asym.add(x, quant_affine(x, mn, mx));
                        }
                    }
                }
            }

            per_layer[L][(std::size_t)w] = LayerSummary{
                chan_ratio, chan_p99, row_ratio, persist,
                chan_asym_n ? chan_asym_sum / (double)chan_asym_n : 0.0,
                row_asym_n ? row_asym_sum / (double)row_asym_n : 0.0,
                e_row_sym.relL2(), e_row_asym.relL2(), e_row32.relL2(),
                e_c16_sym.relL2(), e_c16_asym.relL2()};
        }
    }

    for (int w = 0; w < 2; ++w) {
        std::printf("== %s (%s) ==\n", tname[w], w == 0 ? "Kcur_rope" : "Vcur");
        std::printf("%5s %9s %9s %9s %8s %9s %9s | %10s %10s %10s %10s %10s\n", "layer",
                    "chMax/med", "chP99/med", "rowMx/med", "persist", "chAsym", "rowAsym",
                    "row-sym", "row-asym", "row32-sym", "ch16-sym", "ch16-asym");
        LayerSummary agg{};
        double worst_row_sym = 0, worst_c16 = 0;
        for (std::size_t L = 0; L < n_layers; ++L) {
            const auto& r = per_layer[L][(std::size_t)w];
            std::printf("%5zu %9.2f %9.2f %9.2f %8.2f %9.3f %9.3f | %10.4e %10.4e %10.4e %10.4e %10.4e\n",
                        L, r.chan_ratio, r.chan_p99_ratio, r.row_ratio, r.persist_frac, r.chan_asym,
                        r.row_asym, r.e_row_sym, r.e_row_asym, r.e_row32_sym, r.e_chan16_sym,
                        r.e_chan16_asym);
            agg.chan_ratio += r.chan_ratio;
            agg.row_ratio += r.row_ratio;
            agg.persist_frac += r.persist_frac;
            agg.e_row_sym += r.e_row_sym;
            agg.e_row_asym += r.e_row_asym;
            agg.e_row32_sym += r.e_row32_sym;
            agg.e_chan16_sym += r.e_chan16_sym;
            agg.e_chan16_asym += r.e_chan16_asym;
            worst_row_sym = std::max(worst_row_sym, r.e_row_sym);
            worst_c16 = std::max(worst_c16, r.e_chan16_sym);
        }
        const double n = (double)n_layers;
        std::printf("  MEAN  chMax/med %.2f  rowMax/med %.2f  persist %.2f\n", agg.chan_ratio / n,
                    agg.row_ratio / n, agg.persist_frac / n);
        std::printf("  MEAN relL2: row-sym %.4e  row-asym %.4e  row32-sym %.4e  ch16-sym %.4e  ch16-asym %.4e\n",
                    agg.e_row_sym / n, agg.e_row_asym / n, agg.e_row32_sym / n, agg.e_chan16_sym / n,
                    agg.e_chan16_asym / n);
        std::printf("  WORST relL2: row-sym %.4e   ch16-sym %.4e\n\n", worst_row_sym, worst_c16);
    }
    return 0;
}
