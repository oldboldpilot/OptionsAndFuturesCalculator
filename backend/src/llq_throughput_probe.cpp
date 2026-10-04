/**
 * Generation throughput of the REAL serving path, per weight store.
 *
 * Answers "how many tokens per second does this model generate when its
 * weights are held dense, and when they are held as LLQ?" through the same
 * `LLMPipeline::fromGGUF(...).numThreads(N).kvCacheMaxSeqLen(...)` construction
 * the assistant services use, greedy and deterministic, on real prompts -- not
 * a kernel microbenchmark. One configuration per process, so nothing is shared
 * between arms.
 *
 *   llq_throughput_probe <model.gguf>
 *       [--store dense|llq|llq-fused]   which numeric path holds the weights
 *       [--threads N]                   inference threads (default 16)
 *       [--prompts N]                   real mortgage utterances, from --val (default 12)
 *       [--timed N]                     how many of them are timed (default 4)
 *       [--tokens N]                    max new tokens per generation (default 96)
 *       [--reps N]                      timed repetitions (default 3)
 *       [--ctx N]                       KV cache length (default 2048)
 *       [--val FILE]                    the holdout to draw utterances from
 *       [--count-coverage]              also count which slices LLQ answered
 *       [--release-dense]               madvise the dense copies away, so any
 *                                       path that still reads them computes garbage
 *       [--dump-tokens FILE]            per-prompt token ids, for diffing two arms
 *       [--label TEXT]
 *
 * It prints ONE machine-readable line beginning `RESULT ` recording the model,
 * its sha256, the quantisation and measured bits per weight, whether LLQ was
 * used, the thread count, prompt and generated token counts, prefill and decode
 * rates separately, and an aggregate SHA-256 over every generated token id. Two
 * arms whose `token_sha` agree produced the same tokens on every prompt.
 *
 * PREFILL AND DECODE ARE SEPARATED BY MEASUREMENT, not by the pipeline's own
 * `tokens_per_second`: the streaming callback stamps the first token, so
 * prefill = prompt tokens / time-to-first-token and decode = (generated - 1) /
 * (total - time-to-first-token). The first token is produced by the prefill
 * pass, which is why it is excluded from the decode rate.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <openssl/evp.h>
#include <new>

import std;
import sensen.llm_pipeline;
import sensen.gguf_parser;
import sensen.gemm;
import llq_weight_store;

namespace {

struct Args {
    std::string model;
    std::string store = "dense";
    std::size_t threads = 16;
    std::size_t prompts = 12;
    std::size_t timed = 4;
    std::size_t tokens = 96;
    int reps = 3;
    std::size_t ctx = 2048;
    std::string val = "agent/dataset/data_mortgage/val.jsonl";
    bool count_coverage = false;
    bool release_dense = false;
    std::string dump_tokens;
    std::string label;
};

[[nodiscard]] auto parse_args(int argc, char** argv) -> std::optional<Args> {
    if (argc < 2) {
        return std::nullopt;
    }
    Args a;
    a.model = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view k = argv[i];
        const auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string{argv[++i]} : std::string{};
        };
        const auto num = [&]() -> std::size_t {
            return static_cast<std::size_t>(std::strtoull(next().c_str(), nullptr, 10));
        };
        if (k == "--store") a.store = next();
        else if (k == "--threads") a.threads = num();
        else if (k == "--prompts") a.prompts = num();
        else if (k == "--timed") a.timed = num();
        else if (k == "--tokens") a.tokens = num();
        else if (k == "--reps") a.reps = static_cast<int>(num());
        else if (k == "--ctx") a.ctx = num();
        else if (k == "--val") a.val = next();
        else if (k == "--count-coverage") a.count_coverage = true;
        else if (k == "--release-dense") a.release_dense = true;
        else if (k == "--dump-tokens") a.dump_tokens = next();
        else if (k == "--label") a.label = next();
        else {
            std::println(stderr, "unknown argument: {}", k);
            return std::nullopt;
        }
    }
    return a;
}

[[nodiscard]] auto hex(std::span<const unsigned char> b) -> std::string {
    std::string s;
    for (const auto c : b) {
        s += std::format("{:02x}", c);
    }
    return s;
}

/// sha256 of a file, streamed. OpenSSL is already this binary's crypto.
[[nodiscard]] auto sha256_file(const std::string& path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "unreadable";
    }
    EVP_MD_CTX* raw = EVP_MD_CTX_new();
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(raw, &EVP_MD_CTX_free);
    EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
    std::vector<char> buf(1U << 20U);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (in.gcount() > 0) {
            EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<std::size_t>(in.gcount()));
        }
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx.get(), md.data(), &len);
    return hex(std::span(md).first(len));
}

class Sha256 {
  public:
    Sha256() : ctx_(EVP_MD_CTX_new(), &EVP_MD_CTX_free) {
        EVP_DigestInit_ex(ctx_.get(), EVP_sha256(), nullptr);
    }
    auto add(std::span<const std::byte> b) -> void {
        EVP_DigestUpdate(ctx_.get(), b.data(), b.size());
    }
    auto add_u32(std::uint32_t v) -> void { add(std::as_bytes(std::span(&v, 1))); }
    [[nodiscard]] auto finish() -> std::string {
        std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
        unsigned int len = 0;
        EVP_DigestFinal_ex(ctx_.get(), md.data(), &len);
        return hex(std::span(md).first(len));
    }

  private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx_;
};

[[nodiscard]] auto rss_kib() -> std::size_t {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.starts_with("VmRSS:")) {
            return static_cast<std::size_t>(std::strtoull(line.c_str() + 6, nullptr, 10));
        }
    }
    return 0;
}

/// Pull `"role": "<r>", "content": "<c>"` pairs out of a val.jsonl row. The rows
/// are one JSON object per line with a `conversations` array; a full parser is
/// not needed to lift the first system and first user turn, but escapes are.
[[nodiscard]] auto unescape(std::string_view s) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        const char n = s[++i];
        switch (n) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'u': {
                if (i + 4 < s.size()) {
                    const auto cp = static_cast<char32_t>(
                        std::strtoul(std::string{s.substr(i + 1, 4)}.c_str(), nullptr, 16));
                    i += 4;
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0U | (cp >> 6U));
                        out += static_cast<char>(0x80U | (cp & 0x3FU));
                    } else {
                        out += static_cast<char>(0xE0U | (cp >> 12U));
                        out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
                        out += static_cast<char>(0x80U | (cp & 0x3FU));
                    }
                }
                break;
            }
            default: out += n; break;
        }
    }
    return out;
}

[[nodiscard]] auto content_of(std::string_view row, std::string_view role) -> std::optional<std::string> {
    const std::string key = std::format("\"role\": \"{}\", \"content\": \"", role);
    const auto at = row.find(key);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    const auto start = at + key.size();
    std::size_t end = start;
    while (end < row.size() && !(row[end] == '"' && row[end - 1] != '\\')) {
        ++end;
    }
    return unescape(row.substr(start, end - start));
}

/// The exact prompt shape mortgage_assistant_service.cpp's build_prompt emits for
/// a first turn: system, user, then the assistant header.
[[nodiscard]] auto load_prompts(const std::string& path, std::size_t n) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string row;
    while (out.size() < n && std::getline(f, row)) {
        const auto sys = content_of(row, "system");
        const auto usr = content_of(row, "user");
        if (!sys || !usr) {
            continue;
        }
        // Single-turn rows only: a row with a second user turn is a
        // clarification exchange, and build_prompt's two-turn shape differs.
        if (row.find("\"role\": \"user\"", row.find("\"role\": \"user\"") + 1) != std::string::npos) {
            continue;
        }
        out.push_back("<|im_start|>system\n" + *sys + "<|im_end|>\n<|im_start|>user\n" + *usr +
                      "<|im_end|>\n<|im_start|>assistant\n");
    }
    return out;
}

struct Timing {
    double ttft_ms{0.0};
    double total_ms{0.0};
    std::size_t prompt_tokens{0};
    std::size_t generated{0};
    std::vector<std::uint32_t> ids;
};

[[nodiscard]] auto run_once(sensen::LLMPipeline& pipe, const std::string& prompt,
                            const sensen::GenerationConfig& cfg) -> Timing {
    Timing t;
    const auto t0 = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> first;
    const auto result = pipe.generateStream(
        prompt, cfg, [&](std::string_view, std::uint32_t) -> bool {
            if (!first) {
                first = std::chrono::steady_clock::now();
            }
            return true;
        });
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) -> double {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count()) /
               1e6;
    };
    t.total_ms = ms(t0, t1);
    t.ttft_ms = first ? ms(t0, *first) : t.total_ms;
    t.prompt_tokens = result.num_prompt_tokens;
    t.generated = result.num_tokens_generated;
    t.ids = result.token_ids;
    return t;
}

[[nodiscard]] auto median(std::vector<double> v) -> double {
    if (v.empty()) {
        return 0.0;
    }
    std::ranges::sort(v);
    return v[v.size() / 2];
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const auto parsed = parse_args(argc, argv);
    if (!parsed) {
        std::println(stderr,
                     "usage: llq_throughput_probe <model.gguf> [--store dense|llq|llq-fused] "
                     "[--threads N] [--prompts N] [--timed N] [--tokens N] [--reps N] [--ctx N] "
                     "[--val FILE] [--count-coverage] [--release-dense] [--dump-tokens FILE] "
                     "[--label TEXT]");
        return 2;
    }
    const Args a = *parsed;

    const auto mode = llq_weight_store::parse_mode(a.store);
    if (!mode) {
        std::println(stderr, "FATAL: --store {} is not dense, llq or llq-fused", a.store);
        return 2;
    }
    if (const auto ready = llq_weight_store::prepare_process_environment(*mode); !ready) {
        std::println(stderr, "FATAL: {}", ready.error());
        return 2;
    }
    // QKV fusion is left at its default for the dense arm ONLY when the operator
    // did not set it; print what is in force so two arms are comparable.
    const char* fusion_env = std::getenv("SENSEN_QKV_FUSION");
    const bool qkv_fusion_on = fusion_env == nullptr || fusion_env[0] != '0';

    const auto prompts = load_prompts(a.val, a.prompts);
    if (prompts.size() < a.prompts) {
        std::println(stderr, "FATAL: only {} usable single-turn prompts in {}", prompts.size(), a.val);
        return 2;
    }

    // ── what the model IS ───────────────────────────────────────────────────
    const std::string sha = sha256_file(a.model);
    std::error_code ec;
    const auto file_bytes = std::filesystem::file_size(a.model, ec);
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> by_type;  // type -> (elements, bytes)
    std::size_t tensors_2d = 0;
    {
        auto parser = sensen::GGUFParser::open(a.model).loadMetadata().loadTensorIndex().build();
        for (const auto& t : parser->getAllTensors()) {
            auto& e = by_type[static_cast<std::uint32_t>(t.type)];
            e.first += t.num_elements;
            e.second += t.data_size;
            tensors_2d += t.dimensions.size() == 2 ? 1 : 0;
        }
    }
    std::uint32_t dom_type = 0;
    std::uint64_t dom_elems = 0;
    for (const auto& [ty, e] : by_type) {
        if (e.first > dom_elems) {
            dom_elems = e.first;
            dom_type = ty;
        }
    }
    const auto& dom = by_type[dom_type];
    const double bpw = dom.first != 0 ? static_cast<double>(dom.second) * 8.0 / static_cast<double>(dom.first) : 0.0;
    const std::string_view qname = dom_type == 8 ? "Q8_0" : dom_type == 2 ? "Q4_0" : dom_type == 1 ? "F16"
                                 : dom_type == 0 ? "F32" : dom_type == 30 ? "BF16" : "other";

    // ── load, under the requested weight store ─────────────────────────────
    auto scope = llq_weight_store::LoadScope::open(*mode);
    const std::size_t rss_before = rss_kib();
    auto pipe = sensen::LLMPipeline::fromGGUF(a.model)
                    .kvCacheMaxSeqLen(a.ctx)
                    .numThreads(a.threads)
                    .build();
    if (!pipe) {
        std::println(stderr, "FATAL: build() returned null");
        return 1;
    }
    const auto verdict = scope->finish();
    if (!verdict) {
        std::println(stderr, "FATAL: {}", verdict.error());
        return 1;
    }
    const auto& rep = *verdict;
    if (*mode != llq_weight_store::Mode::Dense) {
        std::println(stderr, "{}", rep.summary());
    }
    const std::size_t rss_loaded = rss_kib();
    std::size_t released = 0;
    if (a.release_dense) {
        if (*mode == llq_weight_store::Mode::Dense) {
            std::println(stderr, "FATAL: --release-dense with --store dense would zero the only copy");
            return 2;
        }
        released = scope->release_dense_pages();
    }
    const std::size_t rss_after_release = rss_kib();

    sensen::GenerationConfig cfg;
    cfg.strategy = sensen::SamplingStrategy::GREEDY;
    cfg.deterministic = true;
    cfg.max_new_tokens = a.tokens;
    cfg.n_gpu_layers = 0;
    cfg.repetition_penalty = 1.0F;  // production pins this; the default of 1.1 changes every logit

    // ── identity pass: every prompt, every token ───────────────────────────
    Sha256 agg;
    std::size_t total_gen = 0;
    std::size_t total_prompt = 0;
    std::ofstream dump;
    if (!a.dump_tokens.empty()) {
        dump.open(a.dump_tokens);
    }
    for (std::size_t p = 0; p < prompts.size(); ++p) {
        const auto t = run_once(*pipe, prompts[p], cfg);
        agg.add_u32(static_cast<std::uint32_t>(p));
        agg.add_u32(static_cast<std::uint32_t>(t.ids.size()));
        for (const auto id : t.ids) {
            agg.add_u32(id);
        }
        total_gen += t.generated;
        total_prompt += t.prompt_tokens;
        if (dump) {
            dump << p << ':';
            for (const auto id : t.ids) {
                dump << ' ' << id;
            }
            dump << '\n';
        }
    }
    const std::string token_sha = agg.finish();

    // ── throughput pass ────────────────────────────────────────────────────
    for (int w = 0; w < 1; ++w) {  // one untimed pass: page faults and lazy touch are a load cost
        for (std::size_t p = 0; p < std::min(a.timed, prompts.size()); ++p) {
            (void)run_once(*pipe, prompts[p], cfg);
        }
    }
    std::vector<double> decode_rates;
    std::vector<double> prefill_rates;
    std::size_t timed_gen = 0;
    std::size_t timed_prompt = 0;
    for (int r = 0; r < a.reps; ++r) {
        double ttft = 0.0;
        double decode_ms = 0.0;
        std::size_t g = 0;
        std::size_t pt = 0;
        for (std::size_t p = 0; p < std::min(a.timed, prompts.size()); ++p) {
            const auto t = run_once(*pipe, prompts[p], cfg);
            ttft += t.ttft_ms;
            decode_ms += t.total_ms - t.ttft_ms;
            g += t.generated > 0 ? t.generated - 1 : 0;  // the first token belongs to prefill
            pt += t.prompt_tokens;
        }
        timed_gen = g;
        timed_prompt = pt;
        decode_rates.push_back(decode_ms > 0.0 ? static_cast<double>(g) * 1000.0 / decode_ms : 0.0);
        prefill_rates.push_back(ttft > 0.0 ? static_cast<double>(pt) * 1000.0 / ttft : 0.0);
    }

    // ── coverage pass (kept out of the timing: counting is a shared atomic) ─
    sensen::GEMM::Q8SourceCounters cov{};
    std::uint64_t low_bit_type_mismatch = 0;
    // Always measured for an LLQ store: "LLQ was requested" and "LLQ served the
    // slices" are different facts, and the scope's own report only proves the
    // first (it says the image was built and registered, not that a kernel read it).
    if (a.count_coverage || *mode != llq_weight_store::Mode::Dense) {
        sensen::GEMM::resetQ8SourceCounters();
        sensen::GEMM::resetBf16SourceCounters();
        sensen::GEMM::resetLowBitSourceCounters();
        sensen::GEMM::setQ8SourceCounting(true);
        sensen::GEMM::setBf16SourceCounting(true);
        sensen::GEMM::setLowBitSourceCounting(true);
        for (std::size_t p = 0; p < std::min<std::size_t>(a.timed, prompts.size()); ++p) {
            (void)run_once(*pipe, prompts[p], cfg);
        }
        sensen::GEMM::setQ8SourceCounting(false);
        sensen::GEMM::setBf16SourceCounting(false);
        sensen::GEMM::setLowBitSourceCounting(false);
        // ALL THREE registries, summed. Each rung has its own front door, so
        // reading fewer than all of them would report a run on an unread rung
        // as having measured nothing -- and `llq_used` would then be false for a
        // run that did serve every slice from an image. Summing is safe because
        // a given weight has exactly one source type, so no slice is counted
        // twice. This was TWO registries until the 4- and 5-bit rungs landed;
        // the count is part of the contract, not an implementation detail.
        const auto q8 = sensen::GEMM::q8SourceCounters();
        const auto bf16 = sensen::GEMM::bf16SourceCounters();
        const auto low = sensen::GEMM::lowBitSourceCounters();
        cov.source_calls = q8.source_calls + bf16.source_calls + low.source_calls;
        cov.dense_calls = q8.dense_calls + bf16.dense_calls + low.dense_calls;
        // A type mismatch means an image of the wrong type was registered for a
        // buffer. It is counted into dense_calls HERE -- not in the front door,
        // which keeps it separate -- so that `llq_used` goes FALSE and the run
        // refuses rather than reporting a rate for a mixed path.
        cov.dense_calls += low.type_mismatch;
        low_bit_type_mismatch = low.type_mismatch;
    }

    // Measured, not requested: LLQ served this run only if at least one slice was
    // answered from an image and none ran the dense kernel.
    const bool llq_measured =
        *mode != llq_weight_store::Mode::Dense && cov.source_calls > 0 && cov.dense_calls == 0;
    const double dec = median(decode_rates);
    const double pre = median(prefill_rates);
    const auto [dmin, dmax] = std::ranges::minmax(decode_rates);
    std::println(
        "RESULT {{\"label\":\"{}\",\"model\":\"{}\",\"model_sha256\":\"{}\",\"model_bytes\":{},"
        "\"quantisation\":\"{}\",\"bits_per_weight\":{:.3f},\"tensors_2d\":{},"
        "\"store\":\"{}\",\"llq_used\":{},\"llq_weights_adopted\":{},"
        "\"llq_adopted_kind\":\"{}\",\"llq_adopted_q8\":{},\"llq_adopted_bf16\":{},"
        "\"llq_bits_per_weight\":{:.4f},\"llq_source_bits_per_weight\":{:.4f},\"llq_bytes\":{},"
        "\"dense_bytes_of_adopted\":{},\"llq_outliers\":{},\"llq_base_bits\":\"{}..{}\","
        "\"threads\":{},\"qkv_fusion\":{},\"prompts\":{},\"max_new_tokens\":{},"
        "\"identity_prompt_tokens\":{},\"identity_generated_tokens\":{},"
        "\"timed_prompts\":{},\"timed_prompt_tokens\":{},\"timed_decode_tokens\":{},\"reps\":{},"
        "\"decode_tok_s\":{:.2f},\"decode_tok_s_min\":{:.2f},\"decode_tok_s_max\":{:.2f},"
        "\"prefill_tok_s\":{:.1f},\"token_sha\":\"{}\","
        "\"rss_kib_loaded\":{},\"rss_kib_before_load\":{},\"dense_released_bytes\":{},"
        "\"rss_kib_after_release\":{},\"coverage_source_calls\":{},\"coverage_dense_calls\":{},"
        "\"llq_adopted_q5_0\":{},\"llq_adopted_q4_0\":{},\"llq_left_dense\":{},"
        "\"llq_refused\":{},\"low_bit_type_mismatch\":{}}}",
        a.label, a.model, sha, file_bytes, qname, bpw, tensors_2d, mode_name(*mode),
        llq_measured ? "true" : "false", rep.adopted, rep.adopted_kind(), rep.adopted_q8,
        rep.adopted_bf16, rep.llq_bits_per_weight(), rep.source_bits_per_weight(), rep.llq_bytes,
        rep.dense_bytes, rep.outliers, rep.base_bits_min, rep.base_bits_max, a.threads,
        qkv_fusion_on ? "true" : "false", prompts.size(), a.tokens, total_prompt, total_gen,
        std::min(a.timed, prompts.size()), timed_prompt, timed_gen, a.reps, dec, dmin, dmax, pre,
        token_sha, rss_loaded, rss_before, released, rss_after_release, cov.source_calls,
        cov.dense_calls, rep.adopted_q5_0, rep.adopted_q4_0, rep.left_dense, rep.refused,
        low_bit_type_mismatch);
    if (*mode != llq_weight_store::Mode::Dense && !llq_measured) {
        std::println(stderr,
                     "FATAL: --store {} was requested but the dense kernel answered {} of {} "
                     "quantized slice calls: this run did NOT measure LLQ, and its rate is the "
                     "dense rate",
                     a.store, cov.dense_calls, cov.dense_calls + cov.source_calls);
        if (low_bit_type_mismatch != 0) {
            std::println(stderr,
                         "       {} of those were a low-bit TYPE MISMATCH: an image of the wrong "
                         "qtype was registered for a buffer. That is a defect, not a "
                         "configuration -- do not re-run until it is fixed",
                         low_bit_type_mismatch);
        }
        return 3;
    }
    return 0;
}
