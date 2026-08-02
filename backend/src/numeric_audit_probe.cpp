// numeric_audit_probe.cpp -- THROWAWAY correctness audit harness.
//
// Loads the SAME Q8_0 GGUF twice in one process: once through upstream
// llama.cpp (reference) and once through sensen's LlamaModel CPU path, feeds
// both the SAME token ids, and compares full-vocab logits.
//
// Nothing here is product code; it exists only to answer "is sensen's CPU
// inference numerically the same thing llama.cpp computes?".
//
// Usage: ./numeric_audit_probe <model.gguf> [n_rollout] [n_threads]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "llama.h"

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;

namespace {

constexpr const char* kSystemPrompt =
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> "
    "when you have enough to act, or ask exactly one short question when you "
    "do not. You do not give trading advice.";

std::string build_prompt(const std::string& utterance) {
    std::string p;
    p += "<|im_start|>system\n";
    p += kSystemPrompt;
    p += "<|im_end|>\n";
    p += "<|im_start|>user\n";
    p += utterance;
    p += "<|im_end|>\n";
    p += "<|im_start|>assistant\n";
    return p;
}

void quiet_log(ggml_log_level, const char*, void*) {}

struct Stats {
    double max_abs = 0.0;
    int max_abs_idx = -1;
    double mean_abs = 0.0;
    double rms = 0.0;
    double max_rel = 0.0;   // relative to |ref| where |ref| > 1.0
    double corr = 0.0;
    double ref_range = 0.0;
};

Stats compare(const std::vector<float>& a, const std::vector<float>& b) {
    Stats s;
    const std::size_t n = std::min(a.size(), b.size());
    double sum_abs = 0.0, sum_sq = 0.0;
    double mean_a = 0.0, mean_b = 0.0;
    float lo = a[0], hi = a[0];
    for (std::size_t i = 0; i < n; ++i) {
        const double d = std::abs(double(a[i]) - double(b[i]));
        if (d > s.max_abs) { s.max_abs = d; s.max_abs_idx = int(i); }
        sum_abs += d;
        sum_sq += d * d;
        if (std::abs(double(a[i])) > 1.0) {
            const double r = d / std::abs(double(a[i]));
            if (r > s.max_rel) s.max_rel = r;
        }
        mean_a += a[i];
        mean_b += b[i];
        lo = std::min(lo, a[i]);
        hi = std::max(hi, a[i]);
    }
    s.mean_abs = sum_abs / double(n);
    s.rms = std::sqrt(sum_sq / double(n));
    s.ref_range = double(hi) - double(lo);
    mean_a /= double(n);
    mean_b /= double(n);
    double cov = 0, va = 0, vb = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - mean_a, db = b[i] - mean_b;
        cov += da * db; va += da * da; vb += db * db;
    }
    s.corr = cov / std::sqrt(va * vb);
    return s;
}

std::vector<int> topk(const std::vector<float>& v, int k) {
    std::vector<int> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::partial_sort(idx.begin(), idx.begin() + k, idx.end(),
                      [&](int x, int y) { return v[x] > v[y]; });
    idx.resize(k);
    return idx;
}

std::string piece(const llama_vocab* vocab, int tok) {
    char buf[256];
    int n = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
    if (n < 0) return "<?>";
    std::string s(buf, n);
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <model.gguf> [n_rollout] [n_threads]\n", argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int n_rollout = argc > 2 ? std::atoi(argv[2]) : 20;
    const int n_threads = argc > 3 ? std::atoi(argv[3]) : 8;

    // AUDIT_PAD: repeat filler in the user turn to push the prompt long, so RoPE
    // is exercised at high positions rather than only at ~80.
    std::string utterance = "Iron condor on SPY, 30 days out, one contract.";
    if (const char* pad = std::getenv("AUDIT_PAD")) {
        const int reps = std::atoi(pad);
        std::string filler;
        // AUDIT_SHIFT prepends N filler words so a given token INDEX lands on
        // different content -- separates "length-128 boundary" from "that token".
        const int shift = std::getenv("AUDIT_SHIFT") ? std::atoi(std::getenv("AUDIT_SHIFT")) : 0;
        for (int i = 0; i < shift; ++i) filler += "alpha ";
        for (int i = 0; i < reps; ++i)
            filler += "The trader also considered volatility, skew, gamma and theta at length. ";
        utterance = filler + utterance;
    }
    const std::string prompt = build_prompt(utterance);
    const bool do_positions = std::getenv("AUDIT_POSITIONS") != nullptr;

    // ---------------- llama.cpp reference ----------------
    llama_log_set(quiet_log, nullptr);
    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    llama_model* lmodel = llama_model_load_from_file(model_path.c_str(), mp);
    if (!lmodel) { std::fprintf(stderr, "FATAL: llama load failed\n"); return 1; }
    const llama_vocab* vocab = llama_model_get_vocab(lmodel);
    const int n_vocab = llama_vocab_n_tokens(vocab);

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 1024;
    cp.n_batch = 1024;
    cp.n_ubatch = 1024;
    cp.n_seq_max = 1;
    cp.n_threads = n_threads;
    cp.n_threads_batch = n_threads;
    llama_context* lctx = llama_init_from_model(lmodel, cp);
    if (!lctx) { std::fprintf(stderr, "FATAL: llama ctx failed\n"); return 1; }

    std::vector<llama_token> toks(prompt.size() + 16);
    int nt = llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(), toks.data(),
                            (int32_t)toks.size(), true, true);
    if (nt < 0) { toks.resize(-nt); nt = llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(),
                                                        toks.data(), (int32_t)toks.size(), true, true); }
    toks.resize(nt);

    std::printf("== TOKENS (llama.cpp, add_bos=%d) n=%d ==\n",
                (int)llama_vocab_get_add_bos(vocab), nt);
    for (int i = 0; i < nt; ++i) std::printf("%d ", toks[i]);
    std::printf("\n\n");

    llama_batch batch = llama_batch_init(1024, 0, 1);
    auto reset = [&] { batch.n_tokens = 0; };
    auto push = [&](llama_token t, int pos, bool want) {
        int i = batch.n_tokens;
        batch.token[i] = t; batch.pos[i] = pos; batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0; batch.logits[i] = want ? 1 : 0;
        batch.n_tokens++;
    };

    reset();
    for (int i = 0; i < nt; ++i) push(toks[i], i, do_positions ? true : (i == nt - 1));
    if (llama_decode(lctx, batch) != 0) { std::fprintf(stderr, "FATAL: llama prefill\n"); return 1; }
    // Keep every position's reference logits when the per-position sweep is on.
    std::vector<std::vector<float>> ref_all;
    if (do_positions) {
        ref_all.resize(nt);
        for (int i = 0; i < nt; ++i) {
            const float* q = llama_get_logits_ith(lctx, i);
            ref_all[i].assign(q, q + n_vocab);
        }
    }
    const float* lp = llama_get_logits_ith(lctx, batch.n_tokens - 1);
    std::vector<float> ref_logits(lp, lp + n_vocab);

    // ---------------- sensen ----------------
    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();
    std::printf("== SENSEN CONFIG ==\narch=%s layers=%zu hidden=%zu heads=%zu kv_heads=%zu "
                "head_dim=%zu(calc=%zu) vocab=%zu inter=%zu rope_base=%.1f norm_eps=%g\n\n",
                cfg.architecture_name.c_str(), cfg.num_layers, cfg.hidden_dim, cfg.num_heads,
                cfg.num_kv_heads, cfg.head_dim, cfg.head_dim_calculated(), cfg.vocab_size,
                cfg.intermediate_dim, (double)cfg.rope_freq_base, (double)cfg.norm_eps);

    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) { std::fprintf(stderr, "FATAL: sensen load: %s\n",
                                   smodel_e.error().message().c_str()); return 1; }
    auto smodel = std::move(*smodel_e);

    sensen::InferenceThreadPool pool((std::size_t)n_threads);
    sensen::AgentSession agent(0, cfg.num_layers, cfg.num_heads, 1024,
                               cfg.head_dim_calculated(), sensen::KVCacheStrategy::FULL,
                               cfg.num_kv_heads);

    std::vector<std::uint32_t> stoks(toks.begin(), toks.end());
    // AUDIT_BOS=<id>: prepend a token to SENSEN's input only, reproducing what
    // LLMPipeline's Tokenizer(.addBos(true)) actually feeds the model in production.
    if (const char* b = std::getenv("AUDIT_BOS")) {
        stoks.insert(stoks.begin(), (std::uint32_t)std::atoi(b));
        std::printf("!! sensen input prefixed with token %s (n=%zu vs llama %d)\n\n", b,
                    stoks.size(), nt);
    }
    auto sen_logits = smodel->forwardPrompt(stoks, agent, pool);

    std::printf("== LOGITS SIZE == llama=%d sensen=%zu\n\n", n_vocab, sen_logits.size());
    if (sen_logits.size() != (std::size_t)n_vocab) {
        std::printf("!! SIZE MISMATCH -- comparing min(n)\n");
    }

    const Stats st = compare(ref_logits, sen_logits);
    std::printf("== PREFILL LAST-POSITION LOGITS ==\n");
    std::printf("ref logit range (max-min) : %.4f\n", st.ref_range);
    std::printf("max |diff|                : %.6f   (at token id %d)\n", st.max_abs, st.max_abs_idx);
    std::printf("mean |diff|               : %.6f\n", st.mean_abs);
    std::printf("rms  diff                 : %.6f\n", st.rms);
    std::printf("max rel diff (|ref|>1)    : %.6f\n", st.max_rel);
    std::printf("pearson corr              : %.9f\n", st.corr);

    auto rt = topk(ref_logits, 10);
    auto stk = topk(sen_logits, 10);
    std::printf("\nargmax: llama=%d (%s)  sensen=%d (%s)  -> %s\n", rt[0], piece(vocab, rt[0]).c_str(),
                stk[0], piece(vocab, stk[0]).c_str(), rt[0] == stk[0] ? "AGREE" : "DISAGREE");
    std::printf("\n  rank | llama tok (logit)                | sensen tok (logit)\n");
    for (int i = 0; i < 10; ++i) {
        std::printf("  %4d | %6d %-16s %9.4f | %6d %-16s %9.4f %s\n", i,
                    rt[i], piece(vocab, rt[i]).c_str(), (double)ref_logits[rt[i]],
                    stk[i], piece(vocab, stk[i]).c_str(), (double)sen_logits[stk[i]],
                    rt[i] == stk[i] ? "" : "  <-- DIFF");
    }
    int top5_match = 0;
    for (int i = 0; i < 5; ++i) if (rt[i] == stk[i]) top5_match++;
    std::printf("\ntop-5 exact-order agreement: %d/5\n", top5_match);
    // set agreement (ignoring order)
    int top5_set = 0;
    for (int i = 0; i < 5; ++i)
        if (std::find(stk.begin(), stk.begin() + 5, rt[i]) != stk.begin() + 5) top5_set++;
    std::printf("top-5 set agreement        : %d/5\n\n", top5_set);

    // ---------------- per-position sweep (RoPE / attention across all positions) ----
    if (do_positions) {
        std::printf("== PER-POSITION PREFILL SWEEP (sensen re-prefills each prefix) ==\n");
        std::printf("  pos | max|diff| | corr        | llama argmax | sensen argmax | match\n");
        int mismatches = 0;
        double worst = 0.0;
        const char* se = std::getenv("AUDIT_STRIDE");
        const int stride = se ? std::max(1, std::atoi(se)) : std::max(1, nt / 40);
        const char* lo_e = std::getenv("AUDIT_LO");
        const char* hi_e = std::getenv("AUDIT_HI");
        const int lo = lo_e ? std::atoi(lo_e) : 1;
        const int hi = hi_e ? std::min(nt, std::atoi(hi_e)) : nt;
        for (int L = lo; L <= hi; ++L) {
            if (L % stride != 0 && L != hi && L != lo) continue;
            sensen::AgentSession a(1, cfg.num_layers, cfg.num_heads, 4096,
                                   cfg.head_dim_calculated(), sensen::KVCacheStrategy::FULL,
                                   cfg.num_kv_heads);
            std::vector<std::uint32_t> pre(toks.begin(), toks.begin() + L);
            auto sl = smodel->forwardPrompt(pre, a, pool);
            // CONTROL: llama.cpp re-prefilled on the same prefix alone, so a
            // spike can be attributed to sensen rather than to the reference.
            llama_memory_clear(llama_get_memory(lctx), true);
            reset();
            for (int i = 0; i < L; ++i) push(toks[i], i, i == L - 1);
            (void)llama_decode(lctx, batch);
            const float* q2 = llama_get_logits_ith(lctx, batch.n_tokens - 1);
            std::vector<float> ref_solo(q2, q2 + n_vocab);
            const Stats ctrl = compare(ref_all[L - 1], ref_solo);

            const Stats ps = compare(ref_all[L - 1], sl);
            const Stats ps2 = compare(ref_solo, sl);
            const int ra = topk(ref_all[L - 1], 1)[0];
            const int sa = topk(sl, 1)[0];
            if (ra != sa) mismatches++;
            if (ps.max_abs > worst) worst = ps.max_abs;
            std::printf("  %4d | %9.5f | %.9f | %12d | %13d | %s | llamaSolo-vs-batch %8.5f |"
                        " sensen-vs-solo %8.5f\n", L - 1, ps.max_abs, ps.corr, ra, sa,
                        ra == sa ? "yes" : "NO", ctrl.max_abs, ps2.max_abs);
        }
        std::printf("per-position argmax mismatches: %d ; worst max|diff| = %.6f\n\n",
                    mismatches, worst);
    }

    // ---------------- multi-token greedy rollout ----------------
    std::printf("== GREEDY ROLLOUT (%d tokens, each model fed ITS OWN argmax) ==\n", n_rollout);
    std::vector<int> ref_seq, sen_seq;
    std::vector<double> step_maxabs;
    std::vector<int> step_ref_argmax, step_sen_argmax;

    // seed both from their own prefill logits
    int ref_tok = rt[0];
    int sen_tok = stk[0];
    agent.current_pos = stoks.size();
    ref_seq.push_back(ref_tok);
    sen_seq.push_back(sen_tok);
    step_ref_argmax.push_back(ref_tok);
    step_sen_argmax.push_back(sen_tok);
    step_maxabs.push_back(st.max_abs);

    int npast = nt;
    for (int s = 1; s < n_rollout; ++s) {
        reset();
        push(ref_tok, npast, true);
        if (llama_decode(lctx, batch) != 0) { std::fprintf(stderr, "decode fail step %d\n", s); break; }
        const float* p = llama_get_logits_ith(lctx, 0);
        std::vector<float> rl(p, p + n_vocab);

        auto sl = smodel->forward((std::uint32_t)sen_tok, agent, pool);
        agent.current_pos++;
        npast++;

        const Stats ss = compare(rl, sl);
        step_maxabs.push_back(ss.max_abs);
        auto ra = topk(rl, 1)[0];
        auto sa = topk(sl, 1)[0];
        step_ref_argmax.push_back(ra);
        step_sen_argmax.push_back(sa);
        ref_seq.push_back(ra);
        sen_seq.push_back(sa);
        ref_tok = ra;
        sen_tok = sa;
        if (llama_vocab_is_eog(vocab, ra) && llama_vocab_is_eog(vocab, sa)) break;
    }

    std::printf(" step | llama tok           | sensen tok          | max|dlogit| | match\n");
    bool all_match = true;
    for (std::size_t i = 0; i < step_ref_argmax.size(); ++i) {
        const bool m = step_ref_argmax[i] == step_sen_argmax[i];
        if (!m) all_match = false;
        std::printf(" %4zu | %6d %-13s | %6d %-13s | %11.5f | %s\n", i,
                    step_ref_argmax[i], piece(vocab, step_ref_argmax[i]).c_str(),
                    step_sen_argmax[i], piece(vocab, step_sen_argmax[i]).c_str(),
                    step_maxabs[i], m ? "yes" : "NO");
    }
    std::printf("\nROLLOUT: %s (%zu steps)\n", all_match ? "IDENTICAL" : "DIVERGED",
                step_ref_argmax.size());

    std::string ref_text, sen_text;
    for (int t : ref_seq) { char b[256]; int n = llama_token_to_piece(vocab, t, b, sizeof(b), 0, true); if (n>0) ref_text.append(b,n); }
    for (int t : sen_seq) { char b[256]; int n = llama_token_to_piece(vocab, t, b, sizeof(b), 0, true); if (n>0) sen_text.append(b,n); }
    std::printf("\nllama text : %s\n", ref_text.c_str());
    std::printf("sensen text: %s\n", sen_text.c_str());

    llama_batch_free(batch);
    llama_free(lctx);
    llama_model_free(lmodel);
    llama_backend_free();
    return 0;
}
