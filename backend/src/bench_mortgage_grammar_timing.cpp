/**
 * Measures the REAL per-token cost of MortgageParamsGrammar::accept() (which
 * calls recompute(), which scans the full vocabulary) against the real
 * mortgage-assistant tokenizer vocabulary, on THIS machine -- not the
 * "0.253 ms on this machine" figure quoted in mortgage_grammar.cppm's own
 * comment, which cites an unspecified benchmark machine.
 *
 * Diagnostic-only, not linked into the production binary. Answers: how much
 * of ParseOperation's ~5s does grammar-constrained decoding actually cost,
 * separate from raw model decode (already measured by
 * assistant_throughput_probe)?
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import mortgage_grammar;
import sensen.llm_pipeline;
import sensen.tokenizer;

namespace mg = mortgage_calculator::assistant::grammar;

namespace {

[[nodiscard]] auto median_ms(std::vector<double> v) -> double {
    if (v.empty()) return 0.0;
    std::ranges::sort(v);
    return v[v.size() / 2];
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bench_mortgage_grammar_timing <model.gguf> [--accepts 60]\n");
        return 2;
    }
    const std::string model_path = argv[1];
    std::size_t n_accepts = 60;  // approx tokens in a real <params> JSON block
    for (int i = 2; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--accepts" && i + 1 < argc) {
            n_accepts = std::strtoul(argv[++i], nullptr, 10);
        }
    }

    std::printf("Loading pipeline (tokenizer only needed) from %s ...\n", model_path.c_str());
    auto pipeline = sensen::LLMPipeline::fromGGUF(model_path).numThreads(1).build();
    if (!pipeline) {
        std::fprintf(stderr, "failed to build pipeline from %s\n", model_path.c_str());
        return 1;
    }
    const sensen::Tokenizer& tok = pipeline->getTokenizer();
    const std::size_t vsz = tok.getVocabSize();
    std::printf("vocab size: %zu\n", vsz);

    const auto t_vocab0 = std::chrono::steady_clock::now();
    std::vector<std::string> vocab_text(vsz);
    for (std::size_t id = 0; id < vsz; ++id) {
        vocab_text[id] = tok.decodeToken(static_cast<std::uint32_t>(id));
    }
    const auto t_vocab1 = std::chrono::steady_clock::now();
    std::printf(
        "vocab materialisation (per grammar-pool slot, done once at startup): %.1f ms\n",
        std::chrono::duration<double, std::milli>(t_vocab1 - t_vocab0).count());

    const std::optional<std::uint32_t> eos = tok.getSpecialTokens().eos;

    auto schema = mg::Schema::build();
    if (!schema.has_value()) {
        std::fprintf(stderr, "schema build failed: %s\n", schema.error().c_str());
        return 1;
    }

    const auto t_ctor0 = std::chrono::steady_clock::now();
    mg::MortgageParamsGrammar grammar(*schema, vocab_text, eos);
    const auto t_ctor1 = std::chrono::steady_clock::now();
    std::printf(
        "grammar construction (1 recompute() over the whole vocab, per pool slot at startup): "
        "%.2f ms\n",
        std::chrono::duration<double, std::milli>(t_ctor1 - t_ctor0).count());

    // Arm on the activation marker so accept() below exercises the SAME path
    // production takes once <params> appears.
    const std::string_view marker = schema->activation_marker();
    if (!marker.empty()) {
        grammar.reset();
        if (!grammar.prime(marker)) {
            std::fprintf(stderr, "prime() on marker failed -- can't measure armed accept()\n");
            return 1;
        }
    }

    // Walk the mask to find a real currently-allowed token id at each step
    // (accept() requires mask_[token_id]==true), so this is a genuine armed
    // decode trajectory, not just re-priming the same state repeatedly.
    std::vector<double> accept_ms;
    accept_ms.reserve(n_accepts);
    std::size_t genuine_accepts = 0;
    for (std::size_t step = 0; step < n_accepts; ++step) {
        const auto ids = grammar.allowed_token_ids();
        if (ids.empty()) {
            std::printf("  (grammar reached a dead end / EOS-only state at step %zu -- stopping)\n",
                        step);
            break;
        }
        const std::uint32_t pick = ids.front();
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = grammar.accept(pick);
        const auto t1 = std::chrono::steady_clock::now();
        if (!ok) {
            std::printf("  (accept() rejected its own allowed token at step %zu -- stopping)\n",
                        step);
            break;
        }
        accept_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        ++genuine_accepts;
    }

    double total_ms = 0.0;
    for (double m : accept_ms) total_ms += m;
    std::printf("\n--- armed accept()/recompute() cost, %zu genuine steps ---\n", genuine_accepts);
    std::printf("median: %.3f ms/token\n", median_ms(accept_ms));
    std::printf("total:  %.1f ms for %zu tokens\n", total_ms, genuine_accepts);
    if (genuine_accepts > 0) {
        std::printf("mean:   %.3f ms/token\n", total_ms / static_cast<double>(genuine_accepts));
    }
    return 0;
}
