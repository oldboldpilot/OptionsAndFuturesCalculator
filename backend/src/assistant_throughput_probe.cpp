/**
 * Generation throughput of the sensen serving path, CPU and GPU.
 *
 * Answers "how many tok/s does the assistant actually generate?" against the
 * SAME construction production uses -- `LLMPipeline::fromGGUF(...).numThreads(N)
 * .kvCacheMaxSeqLen(...)`, greedy, deterministic -- rather than a kernel
 * microbenchmark, so the number includes tokenisation, prefill, the sampler and
 * the KV cache, which is what a trader waits on.
 *
 *   assistant_throughput_probe <model.gguf> [--threads 1,2,4,8,16] [--tokens 128]
 *                              [--gpu-layers N] [--ctx 4096] [--reps 3]
 *
 * `--gpu-layers` is how the same binary measures the GPU path: production sets
 * `n_gpu_layers = 0` deliberately on its CPU build (a default-constructed
 * GenerationConfig arrives with SIZE_MAX and compute_backend=AUTO, which sensen
 * counted as a GPU request and which silently corrupted CPU decode -- see
 * assistant_service.cpp's own comment). Passing a non-zero value here offloads,
 * so a CUDA-enabled build reports the GPU rate through the identical loop.
 *
 * Reports the MEDIAN of `--reps` runs per configuration. A single timing on a
 * shared machine is noise; the median is the smallest honest summary.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

import sensen.llm_pipeline;

namespace {

/// The assistant's real system prompt shape: short instruction, short utterance.
/// Prompt length changes prefill cost, so a synthetic wall of text would report a
/// number no request ever sees.
constexpr std::string_view kPrompt =
    "<|im_start|>system\n"
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> when you "
    "have enough to act, or ask exactly one short question when you do not. You do "
    "not give trading advice.<|im_end|>\n"
    "<|im_start|>user\n"
    "Iron condor on SPY, 30 days out, one contract.<|im_end|>\n"
    "<|im_start|>assistant\n";

[[nodiscard]] auto parse_list(std::string_view csv) -> std::vector<std::size_t> {
    std::vector<std::size_t> out;
    std::size_t pos = 0;
    while (pos <= csv.size()) {
        const auto comma = csv.find(',', pos);
        const auto piece = csv.substr(pos, comma == std::string_view::npos ? csv.size() - pos
                                                                          : comma - pos);
        std::size_t v = 0;
        if (std::from_chars(piece.data(), piece.data() + piece.size(), v).ec == std::errc{} && v > 0) {
            out.push_back(v);
        }
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return out;
}

[[nodiscard]] auto median(std::vector<double> v) -> double {
    if (v.empty()) return 0.0;
    std::ranges::sort(v);
    return v[v.size() / 2];
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: assistant_throughput_probe <model.gguf> [--threads 1,2,4,8] "
                     "[--tokens 128] [--gpu-layers N] [--ctx 4096] [--reps 3]\n");
        return 2;
    }
    const std::string model_path = argv[1];
    std::vector<std::size_t> thread_list{1, 2, 4, 8};
    std::size_t max_new_tokens = 128;
    std::size_t gpu_layers = 0;
    std::size_t ctx = 4096;
    int reps = 3;

    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto next = [&]() -> std::string_view { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--threads") thread_list = parse_list(next());
        else if (a == "--tokens") max_new_tokens = std::strtoul(std::string{next()}.c_str(), nullptr, 10);
        else if (a == "--gpu-layers") gpu_layers = std::strtoul(std::string{next()}.c_str(), nullptr, 10);
        else if (a == "--ctx") ctx = std::strtoul(std::string{next()}.c_str(), nullptr, 10);
        else if (a == "--reps") reps = std::atoi(std::string{next()}.c_str());
    }
    if (thread_list.empty()) thread_list = {1, 2, 4, 8};

    std::printf("model      : %s\n", model_path.c_str());
    std::printf("max_new    : %zu tokens, greedy + deterministic (production settings)\n",
                max_new_tokens);
    std::printf("gpu_layers : %zu  (%s)\n", gpu_layers,
                gpu_layers == 0 ? "CPU -- production's setting" : "GPU offload");
    std::printf("ctx        : %zu, reps %d (median reported)\n\n", ctx, reps);
    std::printf("  threads | tok/s (median) | generated | wall ms\n");
    std::printf("  --------+----------------+-----------+--------\n");

    for (const auto threads : thread_list) {
        auto pipeline = sensen::LLMPipeline::fromGGUF(model_path)
                            .kvCacheMaxSeqLen(ctx)
                            .numThreads(threads)
                            .build();
        if (!pipeline) {
            std::fprintf(stderr, "  build() returned null for %s at %zu threads\n",
                         model_path.c_str(), threads);
            return 1;
        }

        sensen::GenerationConfig config;
        config.strategy = sensen::SamplingStrategy::GREEDY;
        config.deterministic = true;
        config.max_new_tokens = max_new_tokens;
        config.n_gpu_layers = gpu_layers;

        std::vector<double> rates;
        std::size_t generated = 0;
        double wall_ms = 0.0;

        // One untimed warm-up: the first pass pays page faults and any lazy
        // weight touch, which is a load cost, not a generation rate.
        (void)pipeline->generate(std::string{kPrompt}, config);

        for (int r = 0; r < reps; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto result = pipeline->generate(std::string{kPrompt}, config);
            const auto t1 = std::chrono::steady_clock::now();
            const double ms =
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
            generated = result.num_tokens_generated;
            wall_ms = ms;
            // Measure wall time here rather than trusting the pipeline's own
            // figure: this is what a caller experiences, and it cannot drift
            // from the reported rate.
            rates.push_back(generated > 0 && ms > 0.0 ? generated * 1000.0 / ms : 0.0);
        }

        std::printf("  %7zu | %14.1f | %9zu | %7.0f\n", threads, median(rates), generated, wall_ms);
        std::fflush(stdout);
    }
    return 0;
}
