// THROWAWAY: sensen tokenizer vs llama.cpp tokenizer on the exact chat-template
// strings the assistant uses, plus a spread of general text.
// Usage: ./tokenizer_parity_probe <model.gguf>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "llama.h"

import sensen.tokenizer;

namespace {
constexpr const char* kSystemPrompt =
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> "
    "when you have enough to act, or ask exactly one short question when you "
    "do not. You do not give trading advice.";

std::string build_prompt(const std::string& u) {
    return std::string("<|im_start|>system\n") + kSystemPrompt + "<|im_end|>\n<|im_start|>user\n" +
           u + "<|im_end|>\n<|im_start|>assistant\n";
}
void quiet(ggml_log_level, const char*, void*) {}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <model.gguf>\n", argv[0]); return 2; }
    llama_log_set(quiet, nullptr);
    llama_backend_init();
    llama_model_params mp = llama_model_default_params();
    mp.vocab_only = true;
    llama_model* m = llama_model_load_from_file(argv[1], mp);
    if (!m) { std::fprintf(stderr, "load failed\n"); return 1; }
    const llama_vocab* v = llama_model_get_vocab(m);

    auto tok = sensen::Tokenizer::fromGGUF(argv[1]).addBos(true).addEos(false).build();

    std::vector<std::string> cases = {
        build_prompt("Iron condor on SPY, 30 days out, one contract."),
        build_prompt("Bull call spread on QQQ expiring in 45 days, two contracts."),
        build_prompt("Straddle on NVDA, 14 days to expiration, one contract."),
        "<params>{\"symbol\":\"SPY\",\"asset_class\":\"EQUITY\",\"strategy\":\"iron_condor\","
        "\"expiration_days\":30,\"quantity\":1}</params>",
        "<think>\n\n</think>\n\n",
        "The quick brown fox jumps over the lazy dog 0123456789 !@#$%^&*()",
        "  leading and   repeated   spaces\tand\ttabs\nand newlines\n\n",
        "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c emoji \xf0\x9f\x98\x80\xf0\x9f\x93\x88",
        "-1234.5678e-9 and 1,000,000 and 0x1F",
    };

    int fails = 0;
    for (std::size_t c = 0; c < cases.size(); ++c) {
        const std::string& s = cases[c];
        std::vector<llama_token> lt(s.size() + 16);
        int n = llama_tokenize(v, s.c_str(), (int32_t)s.size(), lt.data(), (int32_t)lt.size(), true, true);
        if (n < 0) { lt.resize(-n); n = llama_tokenize(v, s.c_str(), (int32_t)s.size(), lt.data(), (int32_t)lt.size(), true, true); }
        lt.resize(n);
        auto st = tok->encode(s);

        bool same = st.size() == lt.size();
        std::size_t first_diff = 0;
        if (same) {
            for (std::size_t i = 0; i < st.size(); ++i)
                if ((int)st[i] != lt[i]) { same = false; first_diff = i; break; }
        } else {
            const std::size_t lim = std::min(st.size(), lt.size());
            first_diff = lim;
            for (std::size_t i = 0; i < lim; ++i)
                if ((int)st[i] != lt[i]) { first_diff = i; break; }
        }
        std::printf("case %zu: llama n=%d sensen n=%zu -> %s", c, n, st.size(),
                    same ? "MATCH\n" : "DIFF\n");
        if (!same) {
            ++fails;
            std::printf("   first divergence at index %zu\n   llama : ", first_diff);
            for (std::size_t i = first_diff; i < std::min(lt.size(), first_diff + 12); ++i)
                std::printf("%d ", lt[i]);
            std::printf("\n   sensen: ");
            for (std::size_t i = first_diff; i < std::min(st.size(), first_diff + 12); ++i)
                std::printf("%u ", st[i]);
            std::printf("\n");
        }
    }
    std::printf("TOKENIZER VERDICT: %s (%d/%zu cases differ)\n", fails == 0 ? "MATCH" : "DIFFERS",
                fails, cases.size());
    llama_model_free(m);
    llama_backend_free();
    return fails == 0 ? 0 : 1;
}
