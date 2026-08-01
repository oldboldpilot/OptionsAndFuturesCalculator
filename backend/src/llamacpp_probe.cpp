// llamacpp_probe.cpp -- standalone proof that upstream llama.cpp (pinned via
// FetchContent in backend/CMakeLists.txt, see ENABLE_LLAMACPP_BACKEND) links
// and runs inside THIS build tree, under THIS project's toolchain
// (clang++ -stdlib=libc++, CANONICAL_FLAGS, GGML_NATIVE off).
//
// Adapted from the working prototype at
// /tmp/.../scratchpad/llamacpp_probe/src/parallel.cpp, which measured a 4.4x
// aggregate-throughput improvement for batched decode (one llama_context,
// n_seq_max = n_parallel, one shared llama_batch) over a serial mutex-around-
// one-context baseline at 8 requests / 16 threads. The batching mechanics
// below are unchanged from that prototype; what is added here is a
// byte-for-byte correctness check against the known-good output for
// sequence 0's utterance, which is also the prototype's own "single greedy
// generation" test case (see single.cpp in the same scratchpad directory).
//
// Usage:
//   ./llamacpp_probe <batched|serial> <model.gguf> <n_parallel> [n_threads]
//
// Prints machine-readable lines prefixed with "RESULT " to stdout, a final
// "CORRECTNESS: PASS|FAIL" line, and the full llama.cpp load/generation log
// to stderr (including the "llama_kv_cache: size = ..." line).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "llama.h"

namespace {

// Verbatim from backend/src/modules/assistant_service.cpp:62-66
// (kSystemPrompt). The fine-tuned extraction model was trained against this
// exact string as its system turn; copied byte-for-byte, not paraphrased --
// changing so much as a comma silently reverts the model to uninstructed
// base behaviour (see that file's comment on kSystemPrompt for why).
constexpr const char* kSystemPrompt =
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> "
    "when you have enough to act, or ask exactly one short question when you "
    "do not. You do not give trading advice.";

std::string build_prompt(const std::string& utterance) {
    std::string prompt;
    prompt += "<|im_start|>system\n";
    prompt += kSystemPrompt;
    prompt += "<|im_end|>\n";
    prompt += "<|im_start|>user\n";
    prompt += utterance;
    prompt += "<|im_end|>\n";
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

// Eight distinct prompts so a run with n_parallel < 8 still exercises
// genuinely different requests, not N copies of the same one. Sequence 0 is
// deliberately the one this file's correctness check verifies against.
const std::vector<std::string> kUtterances = {
    "Iron condor on SPY, 30 days out, one contract.",
    "Bull call spread on QQQ expiring in 45 days, two contracts.",
    "Straddle on NVDA, 14 days to expiration, one contract.",
    "Covered call on AAPL, 60 days out, five contracts.",
    "Bear put spread on TSLA, 21 days out, one contract.",
    "Strangle on BTC, 10 days out, one contract.",
    "Futures outright long on ES, 90 days out, two contracts.",
    "Call butterfly on GC, 30 days out, one contract.",
};

// The known-correct output for kUtterances[0], byte-for-byte, per the task
// brief this probe exists to satisfy.
constexpr const char* kExpectedSeq0Output =
    "<think>\n\n</think>\n\n<params>{\"symbol\":\"SPY\",\"asset_class\":\"EQUITY\","
    "\"strategy\":\"iron_condor\",\"expiration_days\":30,\"quantity\":1}</params>";

void llama_log_to_stderr(ggml_log_level level, const char* text, void* /*user_data*/) {
    std::fputs(text, stderr);
    (void)level;
}

long read_vmrss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            long kb;
            iss >> kb;
            return kb;
        }
    }
    return -1;
}

struct SeqState {
    int id = 0;
    std::string prompt;
    std::vector<llama_token> prompt_tokens;
    std::string output;
    int n_past = 0;       // number of tokens already in the KV cache for this seq
    int n_generated = 0;
    bool active = false;
    bool done = false;
    std::chrono::steady_clock::time_point t_start;
    std::chrono::steady_clock::time_point t_end;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <batched|serial> <model.gguf> <n_parallel> [n_threads]\n", argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    const char* model_path = argv[2];
    const int n_parallel = std::atoi(argv[3]);
    const int n_threads_arg = argc > 4 ? std::atoi(argv[4]) : 4;
    const int max_new_tokens = 160;
    const int n_ctx_per_seq = 512;  // 81-ish prompt tokens + <=160 gen, headroom to spare

    if (n_parallel < 1 || n_parallel > (int)kUtterances.size()) {
        std::fprintf(stderr, "FATAL: n_parallel must be in [1, %zu]\n", kUtterances.size());
        return 2;
    }

    llama_log_set(llama_log_to_stderr, nullptr);
    llama_backend_init();

    const auto t_load_start = std::chrono::steady_clock::now();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;
    llama_model* model = llama_model_load_from_file(model_path, mparams);
    if (model == nullptr) {
        std::fprintf(stderr, "FATAL: llama_model_load_from_file failed\n");
        return 1;
    }
    const llama_vocab* vocab = llama_model_get_vocab(model);

    // n_seq_max is always n_parallel even in "serial" mode: serial mode's
    // point is "one context, requests handled one at a time", not "a context
    // that could not physically hold more than one sequence". n_ctx is sized
    // for n_parallel concurrent sequences either way so the two modes are
    // comparable at the same context configuration.
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = (uint32_t)(n_parallel * n_ctx_per_seq);
    cparams.n_batch = (uint32_t)std::max(512, n_parallel * 128);
    cparams.n_ubatch = cparams.n_batch;
    cparams.n_seq_max = (uint32_t)n_parallel;
    cparams.n_threads = n_threads_arg;
    cparams.n_threads_batch = n_threads_arg;
    cparams.no_perf = false;

    llama_context* ctx = llama_init_from_model(model, cparams);
    if (ctx == nullptr) {
        std::fprintf(stderr, "FATAL: llama_init_from_model failed\n");
        return 1;
    }

    const auto t_load_end = std::chrono::steady_clock::now();
    const double load_s = std::chrono::duration<double>(t_load_end - t_load_start).count();
    const long rss_after_load_kb = read_vmrss_kb();
    std::fprintf(stderr, "[load] %.3f s, VmRSS after load = %ld kB\n", load_s, rss_after_load_kb);

    llama_sampler* sampler = llama_sampler_init_greedy();  // stateless -> safe to share across seqs
    const llama_token eot = llama_vocab_eot(vocab);
    const llama_token eos = llama_vocab_eos(vocab);
    const bool add_bos = llama_vocab_get_add_bos(vocab);

    std::vector<SeqState> seqs(n_parallel);
    for (int s = 0; s < n_parallel; ++s) {
        seqs[s].id = s;
        seqs[s].prompt = build_prompt(kUtterances[s]);
        std::vector<llama_token> toks(seqs[s].prompt.size() + 16);
        int n = llama_tokenize(vocab, seqs[s].prompt.c_str(), (int32_t)seqs[s].prompt.size(),
                                toks.data(), (int32_t)toks.size(), /*add_special=*/true,
                                /*parse_special=*/true);
        if (n < 0) {
            toks.resize(-n);
            n = llama_tokenize(vocab, seqs[s].prompt.c_str(), (int32_t)seqs[s].prompt.size(),
                                toks.data(), (int32_t)toks.size(), true, true);
        }
        toks.resize(n);
        seqs[s].prompt_tokens = std::move(toks);
    }

    llama_batch batch = llama_batch_init((int32_t)cparams.n_batch, 0, n_parallel);

    const auto wall_start = std::chrono::steady_clock::now();
    long rss_peak_kb = rss_after_load_kb;

    auto reset_batch = [&]() { batch.n_tokens = 0; };
    auto add_to_batch = [&](llama_token tok, llama_pos pos, llama_seq_id seq_id, bool want_logits) {
        int i = batch.n_tokens;
        batch.token[i] = tok;
        batch.pos[i] = pos;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = seq_id;
        batch.logits[i] = want_logits ? 1 : 0;
        batch.n_tokens++;
    };

    if (mode == "batched") {
        // ---- Prefill: every sequence's whole prompt in ONE llama_decode call ----
        std::vector<int> last_row_of_seq(n_parallel, -1);
        reset_batch();
        for (int s = 0; s < n_parallel; ++s) {
            auto& sq = seqs[s];
            sq.t_start = std::chrono::steady_clock::now();
            for (size_t j = 0; j < sq.prompt_tokens.size(); ++j) {
                const bool last = (j + 1 == sq.prompt_tokens.size());
                add_to_batch(sq.prompt_tokens[j], (llama_pos)j, (llama_seq_id)s, last);
                if (last) last_row_of_seq[s] = batch.n_tokens - 1;
            }
            sq.n_past = (int)sq.prompt_tokens.size();
            sq.active = true;
        }
        if (llama_decode(ctx, batch) != 0) {
            std::fprintf(stderr, "FATAL: prefill llama_decode failed\n");
            return 1;
        }
        int n_active = n_parallel;
        for (int s = 0; s < n_parallel; ++s) {
            llama_token tok = llama_sampler_sample(sampler, ctx, last_row_of_seq[s]);
            llama_sampler_accept(sampler, tok);
            if (llama_vocab_is_eog(vocab, tok) || tok == eot || tok == eos) {
                seqs[s].done = true;
                seqs[s].active = false;
                seqs[s].t_end = std::chrono::steady_clock::now();
                --n_active;
                continue;
            }
            char buf[256];
            int len = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
            seqs[s].output.append(buf, len);
            seqs[s].n_generated++;
            seqs[s].n_past++;
            // stash the token to feed on the next batch
            seqs[s].prompt_tokens.push_back(tok);  // reuse the vector as "last token holder"
        }

        // ---- Decode loop: one llama_decode per step, ALL active seqs in one batch ----
        for (int step = 0; step < max_new_tokens && n_active > 0; ++step) {
            reset_batch();
            std::vector<int> row_of_seq(n_parallel, -1);
            for (int s = 0; s < n_parallel; ++s) {
                if (!seqs[s].active) continue;
                llama_token last_tok = seqs[s].prompt_tokens.back();
                add_to_batch(last_tok, (llama_pos)(seqs[s].n_past - 1), (llama_seq_id)s, true);
                row_of_seq[s] = batch.n_tokens - 1;
            }
            if (batch.n_tokens == 0) break;
            if (llama_decode(ctx, batch) != 0) {
                std::fprintf(stderr, "FATAL: decode step %d failed\n", step);
                break;
            }
            long rss_now = read_vmrss_kb();
            if (rss_now > rss_peak_kb) rss_peak_kb = rss_now;

            for (int s = 0; s < n_parallel; ++s) {
                if (!seqs[s].active) continue;
                llama_token tok = llama_sampler_sample(sampler, ctx, row_of_seq[s]);
                llama_sampler_accept(sampler, tok);
                if (llama_vocab_is_eog(vocab, tok) || tok == eot || tok == eos ||
                    seqs[s].n_generated >= max_new_tokens) {
                    seqs[s].done = true;
                    seqs[s].active = false;
                    seqs[s].t_end = std::chrono::steady_clock::now();
                    --n_active;
                    continue;
                }
                char buf[256];
                int len = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
                seqs[s].output.append(buf, len);
                seqs[s].n_generated++;
                seqs[s].n_past++;
                seqs[s].prompt_tokens.back() = tok;
            }
        }
        for (auto& sq : seqs) {
            if (!sq.done) { sq.t_end = std::chrono::steady_clock::now(); sq.done = true; }
        }
    } else if (mode == "serial") {
        // One request fully finishes (prefill + all decode steps) before the
        // next one starts -- what a mutex around a single context gives you.
        for (int s = 0; s < n_parallel; ++s) {
            auto& sq = seqs[s];
            sq.t_start = std::chrono::steady_clock::now();
            reset_batch();
            int last_row = -1;
            for (size_t j = 0; j < sq.prompt_tokens.size(); ++j) {
                bool last = (j + 1 == sq.prompt_tokens.size());
                add_to_batch(sq.prompt_tokens[j], (llama_pos)j, (llama_seq_id)s, last);
                if (last) last_row = batch.n_tokens - 1;
            }
            sq.n_past = (int)sq.prompt_tokens.size();
            if (llama_decode(ctx, batch) != 0) {
                std::fprintf(stderr, "FATAL: prefill (serial, seq %d) failed\n", s);
                return 1;
            }
            llama_token tok = llama_sampler_sample(sampler, ctx, last_row);
            llama_sampler_accept(sampler, tok);
            for (int step = 0; step < max_new_tokens; ++step) {
                if (llama_vocab_is_eog(vocab, tok) || tok == eot || tok == eos) break;
                char buf[256];
                int len = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
                sq.output.append(buf, len);
                sq.n_generated++;
                reset_batch();
                add_to_batch(tok, (llama_pos)sq.n_past, (llama_seq_id)s, true);
                sq.n_past++;
                if (llama_decode(ctx, batch) != 0) {
                    std::fprintf(stderr, "FATAL: decode (serial, seq %d, step %d) failed\n", s, step);
                    break;
                }
                long rss_now = read_vmrss_kb();
                if (rss_now > rss_peak_kb) rss_peak_kb = rss_now;
                tok = llama_sampler_sample(sampler, ctx, 0);
                llama_sampler_accept(sampler, tok);
            }
            sq.t_end = std::chrono::steady_clock::now();
        }
    } else {
        std::fprintf(stderr, "FATAL: unknown mode '%s'\n", mode.c_str());
        return 2;
    }

    const auto wall_end = std::chrono::steady_clock::now();
    const double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();

    int total_generated = 0;
    for (auto& sq : seqs) total_generated += sq.n_generated;
    const double agg_tok_s = wall_s > 0 ? total_generated / wall_s : 0.0;

    std::fprintf(stdout, "RESULT mode=%s n_parallel=%d wall_s=%.3f agg_tok_s=%.2f "
                          "total_tokens=%d load_s=%.3f vmrss_after_load_kb=%ld vmrss_peak_kb=%ld\n",
                 mode.c_str(), n_parallel, wall_s, agg_tok_s, total_generated, load_s,
                 rss_after_load_kb, rss_peak_kb);

    for (auto& sq : seqs) {
        const double lat_s = std::chrono::duration<double>(sq.t_end - sq.t_start).count();
        std::fprintf(stdout, "RESULT   seq=%d latency_s=%.3f n_generated=%d tok_s=%.2f\n",
                     sq.id, lat_s, sq.n_generated, sq.n_generated / (lat_s > 0 ? lat_s : 1));
        std::fprintf(stderr, "----- seq %d output -----\n%s\n-------------------------\n",
                     sq.id, sq.output.c_str());
    }
    std::fprintf(stderr, "[config] add_bos=%s n_ctx=%u n_batch=%u n_seq_max=%u\n",
                 add_bos ? "true" : "false", cparams.n_ctx, cparams.n_batch, cparams.n_seq_max);

    // Byte-for-byte correctness check against the known-good output for
    // sequence 0's utterance ("Iron condor on SPY, 30 days out, one
    // contract."), per the task brief this probe exists to satisfy.
    const bool correct = (seqs[0].output == kExpectedSeq0Output);
    std::fprintf(stdout, "CORRECTNESS: %s\n", correct ? "PASS" : "FAIL");
    if (!correct) {
        std::fprintf(stdout, "  expected: %s\n  actual:   %s\n",
                     kExpectedSeq0Output, seqs[0].output.c_str());
    }

    llama_batch_free(batch);
    llama_sampler_free(sampler);
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();
    return correct ? 0 : 1;
}
