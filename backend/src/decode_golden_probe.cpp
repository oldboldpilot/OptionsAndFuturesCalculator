/**
 * Bit-exactness gate for changes to the decode path.
 *
 * Captures, and later re-checks, the EXACT token-id sequence sensen produces
 * for a fixed prompt battery under greedy deterministic decoding. Any change
 * that claims to be numerically identical -- a weight repacking, a new SIMD
 * dispatch path, a captured-and-replayed CUDA graph -- must reproduce this
 * file byte for byte.
 *
 *   decode_golden_probe <model.gguf> --capture golden.txt [--tokens 320]
 *   decode_golden_probe <model.gguf> --check   golden.txt
 *
 * Both modes accept `--gpu-layers N` and `--threads N`, so ONE definition of
 * "correct" covers the CPU layout work and the GPU graph work rather than each
 * inventing its own tolerance.
 *
 * ---------------------------------------------------------------------------
 * WHY TOKEN IDS, AND WHY A LONG RUN
 *
 * Comparing decoded TEXT hides real differences: two token sequences can render
 * to the same string. Comparing a single matmul against a tolerance has the
 * opposite problem -- it answers "close enough?" when the claim under test is
 * "identical", and a tolerance wide enough to pass is wide enough to admit a
 * genuinely wrong kernel.
 *
 * Greedy argmax is a discontinuity, which is exactly what makes it a good
 * detector. A one-ulp logit difference changes nothing on the step it occurs;
 * carried through the KV cache it eventually lands on a step where two
 * candidates are near-tied, flips the argmax, and the sequences diverge
 * permanently and visibly. A few hundred steps across several prompts is
 * therefore a far sharper instrument than any single-kernel comparison, and it
 * tests the assembled path a request actually takes.
 *
 * The failure this is built to catch is not a crash. A mis-packed weight tile
 * or a CUDA graph replayed against a stale sequence length produces fluent,
 * confident, WRONG text -- output that passes every smoke test that only asks
 * whether the engine answered.
 *
 * A divergence report names the prompt and the token index, because "which
 * token first differed" localises a layout bug to a layer far faster than a
 * whole-file diff does.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

import sensen.llm_pipeline;

namespace {

/// A fixed battery rather than one prompt: different prompts exercise different
/// expert/attention paths and different sequence lengths, and a layout bug that
/// only shows past a certain context length is a real and common shape.
constexpr std::string_view kPrompts[] = {
    "<|im_start|>system\nYou turn a trader's request into parameters for the Options & "
    "Futures Calculator. Reply with a single JSON object inside <params></params> when you "
    "have enough to act, or ask exactly one short question when you do not. You do not give "
    "trading advice.<|im_end|>\n<|im_start|>user\nIron condor on SPY, 30 days out, one "
    "contract.<|im_end|>\n<|im_start|>assistant\n",

    "<|im_start|>system\nYou turn a trader's request into parameters for the Options & "
    "Futures Calculator. Reply with a single JSON object inside <params></params> when you "
    "have enough to act, or ask exactly one short question when you do not. You do not give "
    "trading advice.<|im_end|>\n<|im_start|>user\nCalendar spread on CL, 45 days.<|im_end|>\n"
    "<|im_start|>assistant\n",

    "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain, "
    "in detail and step by step, how a bull call spread differs from a covered call, "
    "including the payoff at expiry and the margin treatment of each.<|im_end|>\n"
    "<|im_start|>assistant\n",
};

struct Options {
    std::string model;
    std::string golden;
    bool capture = false;
    std::size_t tokens = 320;
    std::size_t threads = 4;
    std::size_t gpu_layers = 0;
    std::size_t ctx = 4096;
    // WU-1(c): on-device-sampling greedy decode (forwardBatchDecodeGreedy_cuda[_graph])
    // and host-sampling decode (forwardBatchDecode_cuda[_graph]) are DIFFERENT kernel
    // paths -- see LLMPipeline::generate's agent.on_device_sampling conjunction, which
    // requires (among other things) `!params.logprobs`. Without this flag the battery
    // only ever exercises the greedy graph; --host-sampling sets logprobs=true, which
    // breaks that conjunction and routes the identical prompt battery through the
    // logits graph instead, WITHOUT changing which token is chosen (strategy stays
    // GREEDY, so sampleGuided's argmax pick is the same argmax sample() would have
    // picked -- see docs/superpowers/specs/2026-08-04-sensen-gpu-decode-launch-collapse.md
    // §5.1). Default false: does not change default behaviour.
    bool host_sampling = false;
};

/// FNV-1a over the id sequence. Printed alongside the ids purely so a human can
/// compare two runs at a glance; the ids themselves remain the actual gate,
/// because a hash tells you THAT something diverged and never where.
[[nodiscard]] auto digest(const std::vector<std::uint32_t>& ids) noexcept -> std::uint64_t {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto id : ids) {
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<std::uint8_t>(id >> (8 * b));
            h *= 1099511628211ULL;
        }
    }
    return h;
}

[[nodiscard]] auto run_one(sensen::LLMPipeline& pipeline, std::string_view prompt,
                           const Options& opt) -> std::vector<std::uint32_t> {
    sensen::GenerationConfig config;
    config.strategy = sensen::SamplingStrategy::GREEDY;
    config.deterministic = true;
    config.max_new_tokens = opt.tokens;
    config.n_gpu_layers = opt.gpu_layers;
    // See Options::host_sampling: breaks the on_device_sampling conjunction so this
    // battery exercises forwardBatchDecode_cuda[_graph] (the logits/host-sampling
    // route) instead of forwardBatchDecodeGreedy_cuda[_graph] (the on-device-greedy
    // route) -- the greedy token choice itself is unchanged.
    if (opt.host_sampling) config.logprobs = true;
    return pipeline.generate(prompt, config).token_ids;
}

/// Serialised as one line per prompt so a divergence is a one-line diff, and so
/// the file stays reviewable in a pull request.
auto write_golden(const std::string& path, const std::vector<std::vector<std::uint32_t>>& runs)
    -> bool {
    std::ofstream out{path};
    if (!out) return false;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        out << i << ' ' << runs[i].size() << ' ' << digest(runs[i]);
        for (const auto id : runs[i]) out << ' ' << id;
        out << '\n';
    }
    return static_cast<bool>(out);
}

[[nodiscard]] auto read_golden(const std::string& path)
    -> std::vector<std::vector<std::uint32_t>> {
    std::vector<std::vector<std::uint32_t>> runs;
    std::ifstream in{path};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls{line};
        std::size_t index = 0;
        std::size_t count = 0;
        std::uint64_t stored_digest = 0;
        if (!(ls >> index >> count >> stored_digest)) continue;
        std::vector<std::uint32_t> ids;
        ids.reserve(count);
        std::uint32_t id = 0;
        while (ls >> id) ids.push_back(id);
        runs.push_back(std::move(ids));
    }
    return runs;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: decode_golden_probe <model.gguf> --capture|--check <golden.txt>\n"
                     "       [--tokens 320] [--threads 4] [--gpu-layers 0] [--ctx 4096]\n"
                     "       [--host-sampling]\n");
        return 2;
    }
    Options opt;
    opt.model = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view a = argv[i];
        const auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--capture") { opt.capture = true; opt.golden = next(); }
        else if (a == "--check") { opt.capture = false; opt.golden = next(); }
        else if (a == "--tokens") opt.tokens = std::strtoul(next().c_str(), nullptr, 10);
        else if (a == "--threads") opt.threads = std::strtoul(next().c_str(), nullptr, 10);
        else if (a == "--gpu-layers") opt.gpu_layers = std::strtoul(next().c_str(), nullptr, 10);
        else if (a == "--ctx") opt.ctx = std::strtoul(next().c_str(), nullptr, 10);
        else if (a == "--host-sampling") opt.host_sampling = true;
    }
    if (opt.golden.empty()) {
        std::fprintf(stderr, "decode_golden_probe: --capture or --check needs a file path\n");
        return 2;
    }

    auto pipeline = sensen::LLMPipeline::fromGGUF(opt.model)
                        .kvCacheMaxSeqLen(opt.ctx)
                        .numThreads(opt.threads)
                        .build();
    if (!pipeline) {
        std::fprintf(stderr, "decode_golden_probe: could not load %s\n", opt.model.c_str());
        return 1;
    }

    std::printf("model      : %s\n", opt.model.c_str());
    std::printf("threads %zu, gpu_layers %zu, ctx %zu, max_new %zu, greedy+deterministic, "
                "%s\n\n",
                opt.threads, opt.gpu_layers, opt.ctx, opt.tokens,
                opt.host_sampling ? "host-sampling (logits graph)" : "on-device-sampling (greedy graph)");

    std::vector<std::vector<std::uint32_t>> runs;
    runs.reserve(std::size(kPrompts));
    for (const auto& prompt : kPrompts) runs.push_back(run_one(*pipeline, prompt, opt));

    if (opt.capture) {
        if (!write_golden(opt.golden, runs)) {
            std::fprintf(stderr, "decode_golden_probe: could not write %s\n", opt.golden.c_str());
            return 1;
        }
        for (std::size_t i = 0; i < runs.size(); ++i) {
            std::printf("  prompt %zu: %zu tokens, digest %016llx\n", i, runs[i].size(),
                        static_cast<unsigned long long>(digest(runs[i])));
        }
        std::printf("\ncaptured -> %s\n", opt.golden.c_str());
        return 0;
    }

    const auto expected = read_golden(opt.golden);
    if (expected.empty()) {
        std::fprintf(stderr, "decode_golden_probe: %s is empty or unreadable -- a missing "
                             "baseline is a FAILURE, not a pass\n", opt.golden.c_str());
        return 1;
    }
    if (expected.size() != runs.size()) {
        std::fprintf(stderr, "decode_golden_probe: baseline has %zu prompts, this build ran %zu "
                             "-- the battery changed, recapture deliberately\n",
                     expected.size(), runs.size());
        return 1;
    }

    int failures = 0;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (runs[i] == expected[i]) {
            std::printf("  prompt %zu: MATCH  (%zu tokens, digest %016llx)\n", i, runs[i].size(),
                        static_cast<unsigned long long>(digest(runs[i])));
            continue;
        }
        ++failures;
        // Report the FIRST divergent index, not just that the run differed: the
        // step at which greedy argmax first flipped is the closest thing to a
        // pointer at the layer that changed.
        std::size_t at = 0;
        const std::size_t common = std::min(runs[i].size(), expected[i].size());
        while (at < common && runs[i][at] == expected[i][at]) ++at;
        std::printf("  prompt %zu: DIVERGED at token %zu", i, at);
        if (at < common) {
            std::printf("  (expected id %u, got %u)", expected[i][at], runs[i][at]);
        } else {
            std::printf("  (length differs: expected %zu, got %zu)", expected[i].size(),
                        runs[i].size());
        }
        std::printf("\n");
    }

    if (failures > 0) {
        std::printf("\nFAIL: %d of %zu prompts diverged. This change is NOT numerically "
                    "identical.\n", failures, runs.size());
        return 1;
    }
    std::printf("\nPASS: all %zu prompts bit-identical to %s\n", runs.size(), opt.golden.c_str());
    return 0;
}
