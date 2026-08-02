// observer_cost_probe.cpp -- measures what `sensen.tensor_observer` costs.
//
// The tensor-observer hook is compiled into every build, including release,
// and is never gated behind a macro. That is only defensible if the unattached
// path is genuinely free, so this measures it rather than asserting it.
//
// It drives sensen ALONE (no llama.cpp) so the number is not diluted by
// reference-engine work, in three configurations:
//
//   1. detached          -- no observer installed. This is the shipped path,
//                           and the one that must be indistinguishable from a
//                           build without the hook at all.
//   2. attached, no-op    -- an observer that does nothing but return. Isolates
//                           the dispatch + filter cost from the callback's own.
//   3. attached, counting -- an observer that touches every element. The
//                           realistic "I am actually debugging" cost.
//
// Comparing (1) against the same binary built WITHOUT the instrumentation is
// what proves the zero-cost claim; comparing (1) to (2)/(3) shows what you pay
// only while actually observing.
//
// Usage: ./observer_cost_probe <model.gguf> [iters] [n_threads] [prompt_tokens]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

import sensen.llama_model;
import sensen.gguf_parser;
import sensen.llm_interfaces;
import sensen.tensor_observer;

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <model.gguf> [iters] [n_threads] [prompt_tokens]\n", argv[0]);
        return 2;
    }
    const std::string model_path = argv[1];
    const int iters = argc > 2 ? std::atoi(argv[2]) : 12;
    const int n_threads = argc > 3 ? std::atoi(argv[3]) : 8;
    const int n_prompt = argc > 4 ? std::atoi(argv[4]) : 128;

    auto parser = sensen::GGUFParser::open(model_path).loadMetadata().loadTensorIndex().build();
    const auto& cfg = parser->getConfig();
    auto smodel_e = sensen::LlamaModel::from_parser(*parser, (std::size_t)n_threads);
    if (!smodel_e) {
        std::fprintf(stderr, "FATAL: sensen load: %s\n", smodel_e.error().message().c_str());
        return 1;
    }
    auto smodel = std::move(*smodel_e);
    sensen::InferenceThreadPool pool((std::size_t)n_threads);

    // A deterministic, arbitrary token sequence. Content is irrelevant to timing
    // as long as it is identical across configurations.
    std::vector<std::uint32_t> toks;
    toks.reserve((std::size_t)n_prompt);
    for (int i = 0; i < n_prompt; ++i)
        toks.push_back(static_cast<std::uint32_t>(1000 + (i * 7919) % 20000));

    auto run_once = [&]() {
        sensen::AgentSession agent(0, cfg.num_layers, cfg.num_heads, 2048,
                                   cfg.head_dim_calculated(), sensen::KVCacheStrategy::FULL,
                                   cfg.num_kv_heads);
        (void)smodel->forwardPrompt(toks, agent, pool);
    };

    auto measure = [&](const char* label) {
        run_once();  // warm up: first pass sizes every thread_local scratch buffer
        std::vector<double> samples;
        samples.reserve((std::size_t)iters);
        for (int i = 0; i < iters; ++i) {
            const auto t0 = Clock::now();
            run_once();
            samples.push_back(seconds_since(t0));
        }
        std::sort(samples.begin(), samples.end());
        const double best = samples.front();
        const double med = samples[samples.size() / 2];
        double sum = 0.0;
        for (double s : samples)
            sum += s;
        const double mean = sum / static_cast<double>(samples.size());
        // Report BEST as the headline: it is the least noise-contaminated
        // estimator on a shared machine, and this comparison is about a
        // per-tensor branch, not about tail latency.
        std::printf("%-22s best %8.4f s   median %8.4f s   mean %8.4f s   (%.1f tok/s best)\n",
                    label, best, med, mean, static_cast<double>(n_prompt) / best);
        return best;
    };

    std::printf("model=%s layers=%zu hidden=%zu prompt_tokens=%d iters=%d threads=%d\n\n",
                model_path.c_str(), cfg.num_layers, cfg.hidden_dim, n_prompt, iters, n_threads);

    // Number of emit sites actually executed per prefill, so the per-call cost
    // can be derived rather than guessed.
    std::size_t n_emits = 0;
    sensen::observe::setObserver([&](const sensen::observe::TensorEvent&) { ++n_emits; });
    run_once();
    sensen::observe::clearObserver();

    const double detached = measure("detached (shipped)");

    sensen::observe::setObserver([](const sensen::observe::TensorEvent&) {});
    const double noop = measure("attached, no-op");
    sensen::observe::clearObserver();

    volatile double sink = 0.0;
    sensen::observe::setObserver([&](const sensen::observe::TensorEvent& ev) {
        double s = 0.0;
        for (float x : ev.data)
            s += x;
        sink = s;
    });
    const double counting = measure("attached, full scan");
    sensen::observe::clearObserver();
    (void)sink;

    std::printf("\nemit sites executed per prefill: %zu\n", n_emits);
    std::printf("attached-noop  vs detached: %+.2f%%\n", 100.0 * (noop / detached - 1.0));
    std::printf("attached-scan  vs detached: %+.2f%%\n", 100.0 * (counting / detached - 1.0));
    std::printf(
        "\nNOTE: the zero-cost claim is about 'detached' vs a build with the hook\n"
        "      removed entirely. Run this binary before and after the instrumentation\n"
        "      to make that comparison; the two attached rows only bound what you pay\n"
        "      while actually observing.\n");
    return 0;
}
