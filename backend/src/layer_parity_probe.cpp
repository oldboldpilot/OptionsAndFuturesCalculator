// layer_parity_probe.cpp -- LAYER-BY-LAYER, PRIMITIVE-BY-PRIMITIVE parity
// harness between sensen and llama.cpp.
//
// WHAT THIS IS FOR
// ----------------
// `numeric_audit_probe` compares FINAL LOGITS. Agreement there proves the
// composition is right; it does NOT prove each primitive is right, because two
// errors can cancel and a primitive can be wrong in a way this prompt never
// exercises. This probe compares every intermediate tensor of the forward pass,
// per layer, so the question "which primitive moved, and by how much?" has a
// mechanical answer.
//
// HOW IT WORKS
// ------------
// Both engines are driven in ONE process on BIT-IDENTICAL token ids:
//
//   llama.cpp  ->  `llama_context_params::cb_eval`, the upstream
//                  `ggml_backend_sched_eval_callback`, fires per graph node.
//   sensen     ->  `sensen::observe::setObserver`, the counterpart hook added
//                  in `sensen/src/tensor_observer.cppm`.
//
// Tensors are matched BY NAME. sensen deliberately uses llama.cpp's own names
// (`attn_norm`, `Qcur`, `ffn_gate`, `result_norm`, ...) so the join is a string
// compare rather than a hand-maintained mapping table.
//
// TWO THINGS ARE FORCED ON THE REFERENCE SIDE, both deliberate:
//   * Flash attention is DISABLED. llama.cpp's `ggml_flash_attn_ext` path never
//     materialises the score matrix, so `kq`/`kq_soft_max` simply do not exist
//     as graph nodes with it on. Disabling it is what makes attention scores
//     comparable at all.
//   * n_gpu_layers = 0, so every tensor is host memory.
//
// Usage:
//   ./layer_parity_probe <model.gguf> [n_threads]
//
// Env:
//   PARITY_TOL=<float>   relative-L2 threshold for the first-divergence verdict.
//                        Default 5e-3, derived from the measured F16 noise floor
//                        (worst primitive 2.3e-3), not picked for comfort.
//   PARITY_PROMPT=<str>  override the user utterance
//   PARITY_VERBOSE=1     print every tensor, not just the per-layer summary

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "ggml.h"
#include "llama.h"

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;
import sensen.tokenizer;
import sensen.tensor_observer;

namespace {

constexpr const char* kSystemPrompt =
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> "
    "when you have enough to act, or ask exactly one short question when you "
    "do not. You do not give trading advice.";

std::string build_prompt(const std::string& utterance) {
    return std::string("<|im_start|>system\n") + kSystemPrompt +
           "<|im_end|>\n<|im_start|>user\n" + utterance + "<|im_end|>\n<|im_start|>assistant\n";
}

void quiet_log(ggml_log_level, const char*, void*) {}

// ---------------------------------------------------------------------------
// Capture
// ---------------------------------------------------------------------------

/// One captured tensor, flattened to f32 and reduced to a (rows, cols) shape.
struct Capture {
    std::string key;  ///< "name-layer", or "name" for whole-model tensors.
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<float> data;
    int order = 0;  ///< Arrival order, used to report divergence in forward order.
};

/// llama.cpp side. `cb_eval` fires twice per node: ask=true to opt in, then
/// ask=false once the data is valid.
struct RefCollector {
    std::vector<Capture> caps;
    int counter = 0;
};

/// Strip llama.cpp's "-<layer>" suffix to get the bare primitive name.
std::string baseName(const std::string& key) {
    const std::size_t dash = key.rfind('-');
    if (dash == std::string::npos)
        return key;
    // Only a trailing all-digits suffix is a layer index.
    for (std::size_t i = dash + 1; i < key.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(key[i])))
            return key;
    return (dash + 1 < key.size()) ? key.substr(0, dash) : key;
}

/// Names we will actually join on. Anything else is skipped at capture time so
/// a long prompt does not have the entire graph copied out of ggml.
bool wanted(const std::string& base) {
    static const std::vector<std::string> v = {
        "inp_embd",  "attn_norm",   "Qcur",        "Kcur",     "Vcur",     "Qcur_normed",
        "Kcur_normed", "kq",        "kq_soft_max", "kqv",      "kqv_out",  "ffn_inp",
        "ffn_norm",  "ffn_gate",    "ffn_up",      "ffn_swiglu", "ffn_out", "l_out",
        "result_norm", "result_output"};
    return std::find(v.begin(), v.end(), base) != v.end();
}

bool ref_cb(ggml_tensor* t, bool ask, void* user_data) {
    auto* rc = static_cast<RefCollector*>(user_data);
    if (ask) {
        // Opt in only to the nodes we will join on. Returning false here tells
        // the scheduler not to bother calling back with the data.
        return t != nullptr && t->name[0] != '\0' && wanted(baseName(t->name));
    }
    if (t == nullptr || t->name[0] == '\0')
        return true;
    if (!wanted(baseName(t->name)))
        return true;

    const std::int64_t n = ggml_nelements(t);
    if (n <= 0)
        return true;

    Capture c;
    c.key = t->name;
    c.order = rc->counter++;

    // SHAPE CANONICALISATION -- this is subtle and getting it wrong silently
    // compares misaligned data rather than failing.
    //
    // ggml's ne[0] is the fastest-varying dimension. Most activation tensors are
    // TOKEN-OUTERMOST: [n_embd, n_tokens] for 2-D, and [head_dim, n_head,
    // n_tokens] for the reshaped Q/K tensors. Their flat order is therefore
    // token-major and lines up with sensen's [tokens, width] directly -- but
    // only if the reference is split as (rows=n_tokens, cols=width), NOT as
    // (rows=n/ne[0], cols=ne[0]). For a 3-D Q tensor the naive split gives
    // cols=head_dim and rows=n_head*n_tokens, which compares the wrong rows
    // against sensen entirely.
    //
    // The exceptions are the attention-score tensors, which are HEAD-outermost:
    // kq/kq_soft_max are [n_kv, n_tokens, n_head] and kqv is [head_dim,
    // n_tokens, n_head]. For those the ne[0] split is the correct one (and kqv
    // is separately permuted into token-major after capture).
    const std::string base = baseName(c.key);
    const bool head_outermost = (base == "kq" || base == "kq_soft_max" || base == "kqv");
    if (head_outermost) {
        c.cols = static_cast<std::size_t>(t->ne[0]);
        c.rows = c.cols ? static_cast<std::size_t>(n) / c.cols : 0;
    } else {
        // ggml_n_dims() DROPS trailing 1s, so a single-token tensor [n_embd, 1]
        // reports 1 dimension, not 2. Taking ne[nd-1] as the token count there
        // would yield tokens = n_embd -- silently transposing the comparison.
        // This is exactly the shape llama.cpp produces from the LAST layer
        // onward, where inp_out_ids reduces every tensor to the requested output
        // rows, so it is the common case rather than an edge case.
        const int nd = ggml_n_dims(t);
        const std::size_t tokens =
            (nd >= 2) ? static_cast<std::size_t>(t->ne[nd - 1]) : std::size_t{1};
        c.rows = tokens;
        c.cols = tokens ? static_cast<std::size_t>(n) / tokens : 0;
    }
    c.data.resize(static_cast<std::size_t>(n));

    if (t->type == GGML_TYPE_F32) {
        ggml_backend_tensor_get(t, c.data.data(), 0, static_cast<std::size_t>(n) * sizeof(float));
    } else if (t->type == GGML_TYPE_F16) {
        std::vector<ggml_fp16_t> tmp(static_cast<std::size_t>(n));
        ggml_backend_tensor_get(t, tmp.data(), 0, tmp.size() * sizeof(ggml_fp16_t));
        for (std::size_t i = 0; i < tmp.size(); ++i)
            c.data[i] = ggml_fp16_to_fp32(tmp[i]);
    } else {
        return true;  // quantized intermediates are not comparison targets
    }
    rc->caps.push_back(std::move(c));
    return true;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

struct Metrics {
    double max_abs = 0.0;
    double mean_abs = 0.0;
    double rel_l2 = 0.0;  ///< ||ref-got||_2 / ||ref||_2
    double cosine = 1.0;
    std::size_t n = 0;
    bool comparable = false;
    std::string note;
};

/**
 * Compare two (rows, cols) tensors that need not have identical shapes.
 *
 * Two shape mismatches are legitimate and expected, so both are handled rather
 * than treated as errors:
 *
 *  1. FEWER REFERENCE ROWS. llama.cpp applies `inp_out_ids` inside the LAST
 *     layer, so from `ffn_inp-27` onward its tensors carry only the rows whose
 *     logits were requested (here: the final token) while sensen still carries
 *     the whole sequence. sensen also norms only the last row in prefill. The
 *     common rows are therefore the TRAILING ones on both sides.
 *
 *  2. MORE REFERENCE COLUMNS. `kq`/`kq_soft_max` are sized to llama.cpp's
 *     PADDED KV length, not the true token count, so the reference has masked
 *     columns sensen never materialises. The comparable region is the leading
 *     `min(cols)` of each row.
 */
Metrics compare(const Capture& ref, const Capture& got) {
    Metrics m;
    if (ref.rows == 0 || got.rows == 0 || ref.cols == 0 || got.cols == 0)
        return m;

    const std::size_t rows = std::min(ref.rows, got.rows);
    const std::size_t cols = std::min(ref.cols, got.cols);
    const std::size_t ref_r0 = ref.rows - rows;  // trailing rows
    const std::size_t got_r0 = got.rows - rows;

    if (ref.rows != got.rows)
        m.note = "rows " + std::to_string(ref.rows) + "vs" + std::to_string(got.rows) +
                 " (compared last " + std::to_string(rows) + ")";
    if (ref.cols != got.cols) {
        if (!m.note.empty())
            m.note += "; ";
        m.note += "cols " + std::to_string(ref.cols) + "vs" + std::to_string(got.cols) +
                  " (compared first " + std::to_string(cols) + ")";
    }

    double sum_abs = 0.0, sum_sq_d = 0.0, sum_sq_r = 0.0, sum_sq_g = 0.0, dot = 0.0;
    for (std::size_t r = 0; r < rows; ++r) {
        const float* a = ref.data.data() + (ref_r0 + r) * ref.cols;
        const float* b = got.data.data() + (got_r0 + r) * got.cols;
        for (std::size_t c = 0; c < cols; ++c) {
            const double x = a[c];
            const double y = b[c];
            // -inf appears in masked score positions on both sides; a masked
            // entry carries no information, so skip rather than produce NaN.
            if (!std::isfinite(x) || !std::isfinite(y))
                continue;
            const double d = std::fabs(x - y);
            if (d > m.max_abs)
                m.max_abs = d;
            sum_abs += d;
            sum_sq_d += d * d;
            sum_sq_r += x * x;
            sum_sq_g += y * y;
            dot += x * y;
            ++m.n;
        }
    }
    if (m.n == 0)
        return m;
    m.comparable = true;
    m.mean_abs = sum_abs / static_cast<double>(m.n);
    m.rel_l2 = (sum_sq_r > 0.0) ? std::sqrt(sum_sq_d) / std::sqrt(sum_sq_r)
                                : (sum_sq_d > 0.0 ? 1.0 : 0.0);
    const double den = std::sqrt(sum_sq_r) * std::sqrt(sum_sq_g);
    m.cosine = (den > 0.0) ? dot / den : 1.0;
    return m;
}

/// The primitive vocabulary, in forward order. Drives row order in the table.
const std::vector<std::string>& primitiveOrder() {
    static const std::vector<std::string> v = {
        "attn_norm",   "Qcur",       "Kcur",        "Vcur",   "Qcur_normed", "Kcur_normed",
        "Qcur_rope",   "Kcur_rope",  "kq",          "kq_soft_max", "kqv",    "kqv_out",
        "attn_out",    "ffn_inp",    "ffn_norm",    "ffn_gate",    "ffn_up", "ffn_swiglu",
        "ffn_out",     "l_out"};
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.gguf> [n_threads]\n", argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int n_threads = argc > 2 ? std::atoi(argv[2]) : 8;
    const double tol = std::getenv("PARITY_TOL") ? std::atof(std::getenv("PARITY_TOL")) : 5e-3;
    const bool verbose = std::getenv("PARITY_VERBOSE") != nullptr;
    const std::string utterance = std::getenv("PARITY_PROMPT")
                                      ? std::getenv("PARITY_PROMPT")
                                      : "Iron condor on SPY, 30 days out, one contract.";
    const std::string prompt = build_prompt(utterance);

    // ---------------- llama.cpp reference ----------------
    llama_log_set(quiet_log, nullptr);
    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    llama_model* lmodel = llama_model_load_from_file(model_path.c_str(), mp);
    if (!lmodel) {
        std::fprintf(stderr, "FATAL: llama load failed\n");
        return 1;
    }
    const llama_vocab* vocab = llama_model_get_vocab(lmodel);

    // Tokenize FIRST, and prove both engines agree, before any numeric claim.
    std::vector<llama_token> toks(prompt.size() + 16);
    int nt = llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(),
                            (int32_t)toks.size(), true, true);
    if (nt < 0) {
        toks.resize(-nt);
        nt = llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(),
                            (int32_t)toks.size(), true, true);
    }
    toks.resize(nt);

    auto stok = sensen::Tokenizer::fromGGUF(model_path).addBos(true).addEos(false).build();
    auto sids = stok->encode(prompt);
    bool tok_match = (sids.size() == toks.size());
    std::size_t tok_first_diff = 0;
    if (tok_match) {
        for (std::size_t i = 0; i < sids.size(); ++i) {
            if (static_cast<int>(sids[i]) != toks[i]) {
                tok_match = false;
                tok_first_diff = i;
                break;
            }
        }
    }
    std::printf("== TOKEN IDS ==\n");
    std::printf("llama.cpp n=%d   sensen n=%zu   -> %s\n", nt, sids.size(),
                tok_match ? "BIT-IDENTICAL" : "DIFFER");
    if (!tok_match) {
        std::printf("!! token ids differ (first at index %zu). Every number below would be\n"
                    "!! meaningless, so the comparison is ABORTED.\n",
                    tok_first_diff);
        llama_model_free(lmodel);
        llama_backend_free();
        return 1;
    }
    std::printf("\n");

    RefCollector rc;
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 1024;
    cp.n_batch = 1024;
    cp.n_ubatch = 1024;
    cp.n_seq_max = 1;
    cp.n_threads = n_threads;
    cp.n_threads_batch = n_threads;
    // Without this, ggml_flash_attn_ext fuses the score matrix away and there is
    // no `kq`/`kq_soft_max` node to compare against at all.
    cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
    cp.cb_eval = ref_cb;
    cp.cb_eval_user_data = &rc;
    llama_context* lctx = llama_init_from_model(lmodel, cp);
    if (!lctx) {
        std::fprintf(stderr, "FATAL: llama ctx failed\n");
        return 1;
    }

    llama_batch batch = llama_batch_init(1024, 0, 1);
    batch.n_tokens = 0;
    for (int i = 0; i < nt; ++i) {
        batch.token[i] = toks[i];
        batch.pos[i] = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = (i == nt - 1) ? 1 : 0;
        batch.n_tokens++;
    }
    if (llama_decode(lctx, batch) != 0) {
        std::fprintf(stderr, "FATAL: llama prefill failed\n");
        return 1;
    }

    // Index the reference by name. llama.cpp calls cb() on "Qcur"/"Kcur" TWICE
    // per layer (once for the raw projection, once after RoPE) and both nodes
    // end up carrying the same name, so occurrences must be disambiguated by
    // arrival order: 1st -> Qcur, 2nd -> Qcur_rope.
    std::map<std::string, Capture> ref_by_key;
    std::map<std::string, int> seen;
    for (auto& c : rc.caps) {
        std::string key = c.key;
        const int occ = seen[key]++;
        if (occ == 1) {
            const std::size_t dash = key.rfind('-');
            const std::string base = (dash == std::string::npos) ? key : key.substr(0, dash);
            const std::string suf = (dash == std::string::npos) ? "" : key.substr(dash);
            if (base == "Qcur" || base == "Kcur")
                key = base + "_rope" + suf;
        }
        // First occurrence wins for any other repeated name (e.g. Vcur, which is
        // cb'd twice on what is the same underlying data).
        if (!ref_by_key.count(key))
            ref_by_key.emplace(key, c);
    }

    // ---------------- sensen ----------------
    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();
    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) {
        std::fprintf(stderr, "FATAL: sensen load: %s\n", smodel_e.error().message().c_str());
        return 1;
    }
    auto smodel = std::move(*smodel_e);

    sensen::InferenceThreadPool pool((std::size_t)n_threads);
    sensen::AgentSession agent(0, cfg.num_layers, cfg.num_heads, 1024, cfg.head_dim_calculated(),
                               sensen::KVCacheStrategy::FULL, cfg.num_kv_heads);

    // LAYOUT FIXUP: llama.cpp publishes "kqv" BEFORE its permute, so its layout
    // is [head_dim, n_tokens, n_head] -- head-major on the outer axis. sensen's
    // attention output buffer is token-major [n_tokens, n_head*head_dim].
    // Comparing the two raw would report a large divergence that is purely a
    // difference in memory order, not in arithmetic, so the reference is
    // permuted into sensen's order here. (Every other tensor pair already agrees
    // on layout; this is the one exception, and it is why the join cannot be a
    // blind flat memcmp.)
    {
        const std::size_t n_head = cfg.num_heads;
        for (auto& [k, c] : ref_by_key) {
            if (baseName(k) != "kqv")
                continue;
            const std::size_t hd = c.cols;
            if (n_head == 0 || hd == 0 || c.rows % n_head != 0)
                continue;
            const std::size_t n_tok = c.rows / n_head;
            std::vector<float> out(c.data.size());
            for (std::size_t h = 0; h < n_head; ++h)
                for (std::size_t t = 0; t < n_tok; ++t)
                    for (std::size_t d = 0; d < hd; ++d)
                        out[t * (n_head * hd) + h * hd + d] =
                            c.data[h * (n_tok * hd) + t * hd + d];
            c.data.swap(out);
            c.rows = n_tok;
            c.cols = n_head * hd;
        }
    }

    std::map<std::string, Capture> got_by_key;
    int got_counter = 0;
    sensen::observe::setObserver([&](const sensen::observe::TensorEvent& ev) {
        Capture c;
        c.key = std::string(ev.name);
        if (ev.layer >= 0)
            c.key += "-" + std::to_string(ev.layer);
        c.rows = ev.rows;
        c.cols = ev.cols;
        c.order = got_counter++;
        c.data.assign(ev.data.begin(), ev.data.end());
        got_by_key[c.key] = std::move(c);
    });

    std::vector<std::uint32_t> stoks(toks.begin(), toks.end());
    (void)smodel->forwardPrompt(stoks, agent, pool);
    sensen::observe::clearObserver();

    // ---------------- report ----------------
    const std::size_t n_layers = cfg.num_layers;
    std::printf("== CAPTURE ==\nllama.cpp nodes: %zu (%zu named+f32/f16 unique keys)\n"
                "sensen tensors : %zu\nlayers: %zu   tokens: %d   tolerance (rel L2): %.3g\n\n",
                rc.caps.size(), ref_by_key.size(), got_by_key.size(), n_layers, nt, tol);

    struct Row {
        std::string key;
        std::string prim;
        int layer;
        Metrics m;
    };
    std::vector<Row> rows;

    auto add = [&](const std::string& key, const std::string& prim, int layer) {
        auto ri = ref_by_key.find(key);
        auto gi = got_by_key.find(key);
        if (ri == ref_by_key.end() || gi == got_by_key.end())
            return false;
        rows.push_back(Row{key, prim, layer, compare(ri->second, gi->second)});
        return true;
    };

    add("inp_embd", "inp_embd", -1);
    for (std::size_t l = 0; l < n_layers; ++l)
        for (const auto& p : primitiveOrder())
            add(p + "-" + std::to_string(l), p, static_cast<int>(l));
    add("result_norm", "result_norm", -1);
    add("result_output", "result_output", -1);

    // Per-primitive table: one column per primitive, one row per layer, showing
    // relative L2. This is the shape that answers "is drift flat or growing?".
    std::printf("== RELATIVE L2 ERROR, LAYER x PRIMITIVE ==\n");
    std::printf("(blank = primitive not present on both sides)\n\n");
    std::vector<std::string> prims = primitiveOrder();
    std::printf("%-5s", "layer");
    for (const auto& p : prims)
        std::printf(" %11.11s", p.c_str());
    std::printf("\n");
    for (std::size_t l = 0; l < n_layers; ++l) {
        std::printf("%-5zu", l);
        for (const auto& p : prims) {
            const std::string key = p + "-" + std::to_string(l);
            auto it = std::find_if(rows.begin(), rows.end(),
                                   [&](const Row& r) { return r.key == key; });
            if (it == rows.end() || !it->m.comparable)
                std::printf(" %11s", "-");
            else
                std::printf(" %11.3e", it->m.rel_l2);
        }
        std::printf("\n");
    }
    std::printf("\n");

    // Whole-model anchors.
    std::printf("== WHOLE-MODEL TENSORS ==\n");
    std::printf("%-16s %12s %12s %12s %14s %10s\n", "tensor", "max|diff|", "mean|diff|", "rel L2",
                "cosine", "n");
    for (const auto& r : rows) {
        if (r.layer >= 0)
            continue;
        std::printf("%-16s %12.4e %12.4e %12.4e %14.10f %10zu %s\n", r.key.c_str(), r.m.max_abs,
                    r.m.mean_abs, r.m.rel_l2, r.m.cosine, r.m.n, r.m.note.c_str());
    }
    std::printf("\n");

    // Per-primitive worst case across all layers -- the "which primitive is the
    // problem" view, independent of depth.
    std::printf("== WORST LAYER PER PRIMITIVE ==\n");
    std::printf("%-14s %6s %12s %12s %12s %14s\n", "primitive", "layer", "max|diff|", "mean|diff|",
                "rel L2", "cosine");
    for (const auto& p : prims) {
        const Row* worst = nullptr;
        for (const auto& r : rows)
            if (r.prim == p && r.m.comparable && (!worst || r.m.rel_l2 > worst->m.rel_l2))
                worst = &r;
        if (!worst) {
            std::printf("%-14s %6s %12s %12s %12s %14s\n", p.c_str(), "-", "absent", "-", "-", "-");
            continue;
        }
        std::printf("%-14s %6d %12.4e %12.4e %12.4e %14.10f\n", p.c_str(), worst->layer,
                    worst->m.max_abs, worst->m.mean_abs, worst->m.rel_l2, worst->m.cosine);
    }
    std::printf("\n");

    if (verbose) {
        std::printf("== EVERY TENSOR ==\n");
        for (const auto& r : rows)
            std::printf("%-22s max=%.4e mean=%.4e relL2=%.4e cos=%.10f n=%zu %s\n", r.key.c_str(),
                        r.m.max_abs, r.m.mean_abs, r.m.rel_l2, r.m.cosine, r.m.n, r.m.note.c_str());
        std::printf("\n");
    }

    // Anything sensen publishes that the reference never produced (or vice
    // versa) is reported rather than silently dropped -- a missing primitive is
    // itself a finding.
    std::printf("== UNMATCHED ==\n");
    int unmatched = 0;
    for (const auto& [k, v] : got_by_key) {
        if (!ref_by_key.count(k)) {
            std::printf("  sensen-only: %s [%zu x %zu]\n", k.c_str(), v.rows, v.cols);
            ++unmatched;
        }
    }
    for (const auto& [k, v] : ref_by_key) {
        if (!got_by_key.count(k)) {
            std::printf("  llama.cpp-only: %s [%zu x %zu]\n", k.c_str(), v.rows, v.cols);
            ++unmatched;
        }
    }
    if (unmatched == 0)
        std::printf("  (none -- every tensor found a counterpart on both sides)\n");
    else
        std::printf("  NOTE: sensen's \"attn_out\" (post output-projection) and llama.cpp's\n"
                    "        \"kqv_out\" (PRE output-projection, despite the name) are different\n"
                    "        tensors and are deliberately NOT joined. The o-projection is still\n"
                    "        covered: its input is \"kqv\" and its output is folded into\n"
                    "        \"ffn_inp\", both of which are compared.\n");
    std::printf("\n");

    // ---------------- verdict ----------------
    const Row* first_bad = nullptr;
    for (const auto& r : rows) {
        if (!r.m.comparable)
            continue;
        if (r.m.rel_l2 > tol) {
            first_bad = &r;
            break;  // `rows` is built in forward order.
        }
    }
    double worst_rel = 0.0;
    const Row* worst_row = nullptr;
    for (const auto& r : rows)
        if (r.m.comparable && r.m.rel_l2 > worst_rel) {
            worst_rel = r.m.rel_l2;
            worst_row = &r;
        }

    std::printf("== VERDICT ==\n");
    if (!first_bad) {
        std::printf("EVERY PRIMITIVE AGREES to rel L2 <= %.3g across all %zu layers.\n", tol,
                    n_layers);
        if (worst_row)
            std::printf("Worst observed: %s at rel L2 %.4e (cosine %.10f).\n",
                        worst_row->key.c_str(), worst_row->m.rel_l2, worst_row->m.cosine);
    } else {
        std::printf("FIRST DIVERGENCE ABOVE TOLERANCE: %s\n", first_bad->key.c_str());
        std::printf("  primitive   : %s\n", first_bad->prim.c_str());
        std::printf("  layer       : %d\n", first_bad->layer);
        std::printf("  max |diff|  : %.6e\n", first_bad->m.max_abs);
        std::printf("  mean |diff| : %.6e\n", first_bad->m.mean_abs);
        std::printf("  rel L2      : %.6e  (tolerance %.3g)\n", first_bad->m.rel_l2, tol);
        std::printf("  cosine      : %.10f\n", first_bad->m.cosine);
        std::printf("\n  Reproduce just this tensor:\n");
        std::printf("    SENSEN_OBSERVE=%s SENSEN_OBSERVE_LAYER=%d <any sensen binary>\n",
                    first_bad->prim.c_str(), first_bad->layer);
        if (worst_row)
            std::printf("\n  Worst overall: %s at rel L2 %.4e.\n", worst_row->key.c_str(),
                        worst_row->m.rel_l2);
    }

    // Depth trend on the residual stream: flat drift is quantisation noise,
    // growing drift is a defect compounding. This is the distinction the whole
    // exercise exists to make, so it is computed rather than left to the reader.
    std::printf("\n== DRIFT ACROSS DEPTH (l_out residual stream) ==\n");
    std::vector<std::pair<int, double>> trend;
    for (const auto& r : rows)
        if (r.prim == "l_out" && r.m.comparable)
            trend.emplace_back(r.layer, r.m.rel_l2);
    if (trend.size() >= 2) {
        std::printf("  layer 0: %.4e   layer %d: %.4e   growth x%.2f\n", trend.front().second,
                    trend.back().first, trend.back().second,
                    trend.front().second > 0 ? trend.back().second / trend.front().second : 0.0);
        bool monotone = true;
        for (std::size_t i = 1; i < trend.size(); ++i)
            if (trend[i].second < trend[i - 1].second * 0.9)
                monotone = false;
        std::printf("  %s\n", monotone
                                  ? "monotonically non-decreasing -- consistent with accumulation"
                                  : "non-monotonic -- consistent with bounded numerical noise");
    } else {
        std::printf("  (insufficient l_out coverage)\n");
    }

    llama_batch_free(batch);
    llama_free(lctx);
    llama_model_free(lmodel);
    llama_backend_free();
    return first_bad ? 1 : 0;
}
