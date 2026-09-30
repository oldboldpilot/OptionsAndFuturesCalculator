/**
 * Gates for the LLQ weight store: a Q8_0 weight held as an LLQ image and served
 * from it by sensen's own kernels.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * NO EXTERNAL TEST FRAMEWORK (rule 39): a `check()` and two counters, and a
 * non-zero exit is the failure signal ctest reads.
 *
 * WHAT IS GATED, AND AGAINST WHAT
 *
 * The claim is that an LLQ image of Q8_0 codes reproduces the dense Q8_0
 * kernel's output BYTE FOR BYTE, because the kernel is handed a tile with the
 * same bytes it always read. The reference here is therefore not a figure the
 * engine produced earlier: it is the SAME kernel on the SAME weights through the
 * dense buffer, computed first in this process, and the LLQ result is required
 * to equal it bit for bit. The dense buffer is then overwritten with 0xA5
 * before the LLQ arm runs, so a result that matches can only have come from the
 * image -- a seam that quietly kept reading dense would match a copy of itself
 * and prove nothing.
 *
 * The refusals are gated in the direction that matters: a group size that is
 * not 32 must be REFUSED, not reinterpreted.
 */
#include <cstdlib>
#include <new>

import std;
import sensen.gemm;
import sensen.lossless_quant;
import llq_weight_store;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::println("  PASS: {}", what);
    } else {
        std::println("  FAIL: {}", what);
        ++g_failures;
    }
}

auto section(std::string_view title) -> void { std::println("\n=== {} ===", title); }

namespace ws = llq_weight_store;
namespace llq = sensen::llq;
using GEMM = sensen::GEMM;

constexpr std::size_t kBB = 34;

struct Weight {
    std::size_t rows{0};
    std::size_t k{0};
    std::vector<std::uint8_t> row_major;   ///< GGUF layout
    std::vector<std::int8_t> codes;        ///< rows x k
    std::vector<float> scales;             ///< rows x k/32, fp16-exact
};

/// A synthetic Q8_0 weight. `spread` bounds the code magnitude, so a small
/// spread gives LLQ a narrow base (B < 8) and a full spread gives B = 8.
[[nodiscard]] auto make_weight(std::size_t rows, std::size_t k, int spread, std::uint32_t seed,
                               double outlier_rate = 0.0) -> Weight {
    Weight w;
    w.rows = rows;
    w.k = k;
    const std::size_t nb = k / 32;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> code(-spread, spread);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::uniform_real_distribution<float> sc(0.0005F, 0.05F);
    w.codes.resize(rows * k);
    w.scales.resize(rows * nb);
    w.row_major.resize(rows * nb * kBB);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t b = 0; b < nb; ++b) {
            const float s = ws::half_to_float(ws::half_bits(sc(rng)));
            w.scales[(r * nb) + b] = s;
            auto* blk = w.row_major.data() + (((r * nb) + b) * kBB);
            const std::uint16_t dh = ws::half_bits(s);
            std::memcpy(blk, &dh, 2);
            for (std::size_t i = 0; i < 32; ++i) {
                int v = code(rng);
                if (outlier_rate > 0.0 && u(rng) < outlier_rate) {
                    v = (u(rng) < 0.5) ? -127 : 127;
                }
                w.codes[(r * k) + (b * 32) + i] = static_cast<std::int8_t>(v);
                blk[2 + i] = static_cast<std::uint8_t>(static_cast<std::int8_t>(v));
            }
        }
    }
    return w;
}

/// The dense column-major buffer sensen's kernels read, built the way the model
/// loader builds it.
[[nodiscard]] auto transposed(const Weight& w) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> t(GEMM::quantMatrixBytes(w.rows, w.k, GEMM::QType::Q8_0));
    GEMM::transposeQuantizedWeights(w.row_major.data(), t.data(), w.rows, w.k, GEMM::QType::Q8_0);
    return t;
}

[[nodiscard]] auto bit_equal(std::span<const float> a, std::span<const float> b) -> bool {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

[[nodiscard]] auto make_activation(std::size_t m, std::size_t k, std::uint32_t seed)
    -> std::vector<float> {
    std::mt19937 rng(seed);
    std::normal_distribution<float> n(0.0F, 1.0F);
    std::vector<float> a(m * k);
    for (auto& v : a) {
        v = n(rng);
    }
    return a;
}

}  // namespace

auto main() -> int {
    std::println("LLQ weight store gates");

    // ── selection ──────────────────────────────────────────────────────────
    section("selection: default OFF, and a typo is refused rather than defaulted");
    check(ws::parse_mode("") == ws::Mode::Dense, "unset selects dense");
    check(ws::parse_mode("dense") == ws::Mode::Dense, "'dense' selects dense");
    check(ws::parse_mode("llq") == ws::Mode::Llq, "'llq' selects the materialising path");
    check(ws::parse_mode("llq-fused") == ws::Mode::LlqFused, "'llq-fused' selects the fused path");
    check(!ws::parse_mode("LLQ").has_value(), "'LLQ' is refused, not read as dense");
    check(!ws::parse_mode("llq ").has_value(), "'llq ' (trailing space) is refused");
    check(!ws::parse_mode("lql").has_value(), "a misspelling is refused");
    static_assert(ws::parse_mode("llq") == ws::Mode::Llq);

    // ── adoption: what LLQ can and cannot be presented as ─────────────────
    section("adoption: group size other than 32 is REFUSED, not reinterpreted");
    {
        const auto w = make_weight(64, 128, 127, 1);
        const auto fused_ok = ws::LlqQ8Source::from_q8_0_row_major(w.row_major, 64, 128, ws::Mode::Llq);
        check(fused_ok.has_value(), "a 64x128 Q8_0 weight is adopted");

        // Group 64 and group 0 images of the SAME codes: one scale per 64 codes,
        // and one per row. Neither can be rebuilt as Q8_0 blocks without
        // inventing scales.
        for (const std::uint32_t g : {64U, 0U}) {
            const std::size_t groups = g == 0 ? 1 : 128 / g;
            std::vector<float> sc(64 * groups, 0.5F);
            auto m = llq::LlqMatrix::fromCodes(w.codes, sc, 64, 128,
                                               llq::LlqOptions::bits(8).withGroup(g));
            check(m.has_value(), std::format("an LLQ image with group {} builds", g));
            std::vector<llq::LlqMatrix> panels;
            panels.push_back(std::move(*m));
            const auto r = ws::LlqQ8Source::adopt(std::move(panels), 64, 128, ws::Mode::Llq);
            check(!r.has_value() && r.error() == ws::AdoptError::GroupNotThirtyTwo,
                  std::format("group {} is refused as GroupNotThirtyTwo", g));
        }
        // 4-bit source codes.
        {
            std::vector<std::int8_t> c4(64 * 128);
            for (std::size_t i = 0; i < c4.size(); ++i) {
                c4[i] = static_cast<std::int8_t>(static_cast<int>(i % 15) - 7);
            }
            std::vector<float> sc(64 * 4, 0.5F);
            auto m = llq::LlqMatrix::fromCodes(c4, sc, 64, 128, llq::LlqOptions::bits(4).withGroup(32));
            check(m.has_value(), "a 4-bit LLQ image builds");
            std::vector<llq::LlqMatrix> panels;
            panels.push_back(std::move(*m));
            const auto r = ws::LlqQ8Source::adopt(std::move(panels), 64, 128, ws::Mode::Llq);
            check(!r.has_value() && r.error() == ws::AdoptError::NotEightBit,
                  "a 4-bit image is refused as NotEightBit");
        }
        // A scale that fp16 cannot hold.
        {
            std::vector<float> sc(64 * 4, 0.1F);  // 0.1f is not an fp16 value
            auto m = llq::LlqMatrix::fromCodes(w.codes, sc, 64, 128,
                                               llq::LlqOptions::bits(8).withGroup(32));
            std::vector<llq::LlqMatrix> panels;
            panels.push_back(std::move(*m));
            const auto r = ws::LlqQ8Source::adopt(std::move(panels), 64, 128, ws::Mode::Llq);
            check(!r.has_value() && r.error() == ws::AdoptError::ScaleNotHalfExact,
                  "a scale fp16 cannot hold is refused as ScaleNotHalfExact");
        }
        // Shape.
        {
            const auto r = ws::LlqQ8Source::from_q8_0_row_major(w.row_major, 64, 96, ws::Mode::Llq);
            check(!r.has_value(), "k = 96 (not a multiple of 64) is refused");
        }
    }

    // ── materialise: the tile is what the dense transpose would hold ──────
    section("materialise reproduces the dense column-major bytes exactly");
    for (const int spread : {127, 6}) {
        const bool narrow = spread < 100;
        const auto w = make_weight(192, 256, spread, 7, narrow ? 0.01 : 0.0);
        const auto dense = transposed(w);
        const auto src = ws::LlqQ8Source::from_q8_0_row_major(w.row_major, w.rows, w.k, ws::Mode::Llq);
        check(src.has_value(), std::format("spread {} adopts", spread));
        if (!src) {
            continue;
        }
        const auto [lo, hi] = (*src)->base_bits_range();
        check(narrow ? (lo < 8) : (lo == 8 && hi == 8),
              std::format("spread {}: base width {}..{} is the {} path", spread, lo, hi,
                          narrow ? "narrow (decodeRow)" : "direct (memcpy)"));
        const std::size_t nb = w.k / 32;
        const std::array<std::pair<std::size_t, std::size_t>, 5> slices{
            {{0, 192}, {0, 1}, {5, 77}, {63, 129}, {191, 192}}};
        for (const auto& [r0, r1] : slices) {
            const std::size_t width = r1 - r0;
            std::vector<std::uint8_t> tile(nb * width * kBB, 0xEE);
            const bool ok = (*src)->materialise(r0, r1, tile);
            bool same = ok;
            for (std::size_t bc = 0; same && bc < nb; ++bc) {
                for (std::size_t t = 0; same && t < width; ++t) {
                    same = std::memcmp(tile.data() + ((bc * width) + t) * kBB,
                                       dense.data() + ((bc * w.rows) + (r0 + t)) * kBB, kBB) == 0;
                }
            }
            check(same, std::format("spread {}: rows [{}, {}) byte-identical to the dense buffer",
                                    spread, r0, r1));
        }
        std::vector<std::uint8_t> small(4, 0);
        check(!(*src)->materialise(0, 192, small), "a destination that is too small is refused");
        std::vector<std::uint8_t> tile2(nb * 64 * kBB);
        check(!(*src)->materialise(150, 250, tile2), "rows beyond the weight are refused");
    }

    // ── the seam: sensen's own kernels served from the image ───────────────
    section("seam: byte-identical to the dense kernel, with the dense buffer destroyed");
    {
        const std::size_t rows = 192;
        const std::size_t k = 256;
        for (const int spread : {127, 6}) {
            const auto w = make_weight(rows, k, spread, 11, spread < 100 ? 0.01 : 0.0);
            auto dense = transposed(w);
            for (const std::size_t m : {std::size_t{1}, std::size_t{5}, std::size_t{9}}) {
                const auto a = make_activation(m, k, static_cast<std::uint32_t>(100 + m));
                std::vector<float> ref(m * rows, -1.0F);
                GEMM::clearQ8WeightSources();
                GEMM::matvecQuantizedBatch(a.data(), dense.data(), ref.data(), m, k, rows,
                                           GEMM::QType::Q8_0);

                const auto src =
                    ws::LlqQ8Source::from_q8_0_row_major(w.row_major, rows, k, ws::Mode::Llq);
                GEMM::registerQ8WeightSource(dense.data(), *src);
                // Destroy the dense copy: only the image can produce the answer now.
                // The kernel receives the ADDRESS that was registered, so poison
                // that very buffer.
                const auto saved = dense;
                std::ranges::fill(dense, 0xA5);

                std::vector<float> got(m * rows, -2.0F);
                GEMM::matvecQuantizedBatch(a.data(), dense.data(), got.data(), m, k, rows,
                                           GEMM::QType::Q8_0);
                check(bit_equal(ref, got),
                      std::format("spread {}, m = {}: LLQ output is bit-identical to dense", spread, m));

                // Unregister, and the poisoned buffer is what the kernel reads:
                // the result must DIFFER. This is the positive control that
                // proves the buffer really was destroyed.
                GEMM::unregisterQ8WeightSource(dense.data());
                std::vector<float> garbage(m * rows, -3.0F);
                GEMM::matvecQuantizedBatch(a.data(), dense.data(), garbage.data(), m, k, rows,
                                           GEMM::QType::Q8_0);
                check(!bit_equal(ref, garbage),
                      std::format("spread {}, m = {}: the poisoned dense buffer gives a DIFFERENT "
                                  "answer (positive control)",
                                  spread, m));
                dense = saved;
            }
        }
        check(GEMM::q8WeightSourceCount() == 0, "nothing is left registered");
    }

    // ── fused: exact integers, float order not promised ───────────────────
    section("fused: close to dense, and the difference is measured, not assumed");
    {
        const std::size_t rows = 192;
        const std::size_t k = 512;
        const auto w = make_weight(rows, k, 127, 21);
        auto dense = transposed(w);
        const auto a = make_activation(1, k, 5);
        std::vector<float> ref(rows, 0.0F);
        GEMM::clearQ8WeightSources();
        GEMM::matvecQuantizedBatch(a.data(), dense.data(), ref.data(), 1, k, rows,
                                   GEMM::QType::Q8_0);
        const auto src = ws::LlqQ8Source::from_q8_0_row_major(w.row_major, rows, k, ws::Mode::LlqFused);
        GEMM::registerQ8WeightSource(dense.data(), *src);
        std::ranges::fill(dense, 0xA5);
        std::vector<float> got(rows, 0.0F);
        GEMM::matvecQuantizedBatch(a.data(), dense.data(), got.data(), 1, k, rows,
                                   GEMM::QType::Q8_0);
        double worst = 0.0;
        double scale = 0.0;
        std::size_t equal = 0;
        for (std::size_t r = 0; r < rows; ++r) {
            worst = std::max(worst, static_cast<double>(std::fabs(ref[r] - got[r])));
            scale = std::max(scale, static_cast<double>(std::fabs(ref[r])));
            equal += (std::bit_cast<std::uint32_t>(ref[r]) == std::bit_cast<std::uint32_t>(got[r]));
        }
        std::println("  fused vs dense: max |diff| = {:.3e} on outputs up to {:.3e}; {} of {} rows "
                     "bit-equal",
                     worst, scale, equal, rows);
        check(scale > 0.0 && worst <= 1e-5 * scale,
              "fused output is within 1e-5 of the largest output (integer part exact, float "
              "order differs)");
        check(std::ranges::all_of(got, [](float v) { return std::isfinite(v); }),
              "fused output is finite");
        GEMM::clearQ8WeightSources();
    }

    // ── the scope: the observer path the services use ─────────────────────
    section("scope: adopts at transpose time, Dense installs nothing, withdraw() undoes it");
    {
        const auto w = make_weight(128, 256, 127, 31);
        std::vector<std::uint8_t> dst(GEMM::quantMatrixBytes(w.rows, w.k, GEMM::QType::Q8_0));

        GEMM::clearQ8WeightSources();
        {
            auto scope = ws::LoadScope::open(ws::Mode::Dense);
            GEMM::transposeQuantizedWeights(w.row_major.data(), dst.data(), w.rows, w.k,
                                            GEMM::QType::Q8_0);
            check(GEMM::q8WeightSourceCount() == 0, "a Dense scope registers nothing");
            const auto v = scope->finish();
            check(v.has_value() && v->adopted == 0, "a Dense scope finishes clean with nothing adopted");
        }
        {
            auto scope = ws::LoadScope::open(ws::Mode::Llq);
            GEMM::transposeQuantizedWeights(w.row_major.data(), dst.data(), w.rows, w.k,
                                            GEMM::QType::Q8_0);
            check(GEMM::q8WeightSourceCount() == 1, "an Llq scope registers the transposed weight");
            const auto v = scope->finish();
            check(v.has_value() && v->fully_adopted(), "the scope reports the weight adopted");
            if (v) {
                check(v->adopted_weights == w.rows * w.k, "the report counts the weights served");
                check(v->llq_bytes > 0 && v->dense_bytes == w.rows * (w.k / 32) * kBB,
                      "the report carries both footprints");
                std::println("  {}", v->summary());
            }
            scope->withdraw();
            check(GEMM::q8WeightSourceCount() == 0, "withdraw() removes what the scope registered");
        }
        // Nothing to adopt: asked for LLQ, got none of it -> finish() says so.
        {
            auto scope = ws::LoadScope::open(ws::Mode::Llq);
            const auto v = scope->finish();
            check(!v.has_value(), "asking for LLQ and adopting nothing is an error, not dense");
        }
        // A weight LLQ cannot hold is a refusal that finish() reports.
        {
            auto scope = ws::LoadScope::open(ws::Mode::Llq);
            const auto bad = make_weight(64, 96, 127, 41);  // k = 96 is not a multiple of 64
            std::vector<std::uint8_t> d2(GEMM::quantMatrixBytes(64, 96, GEMM::QType::Q8_0));
            GEMM::transposeQuantizedWeights(bad.row_major.data(), d2.data(), 64, 96,
                                            GEMM::QType::Q8_0);
            const auto v = scope->finish();
            check(!v.has_value(), "a weight LLQ cannot hold makes finish() an error");
            scope->withdraw();
        }
        check(GEMM::q8WeightSourceCount() == 0, "no registration survives the scopes");
    }

    // ── process environment ───────────────────────────────────────────────
    section("process environment: QKV fusion must be off for an LLQ store");
    {
        ::setenv("SENSEN_QKV_FUSION", "1", 1);
        check(!ws::prepare_process_environment(ws::Mode::Llq).has_value(),
              "SENSEN_QKV_FUSION=1 with an LLQ store is refused");
        check(ws::prepare_process_environment(ws::Mode::Dense).has_value(),
              "SENSEN_QKV_FUSION=1 with the dense store is fine");
        ::setenv("SENSEN_QKV_FUSION", "0", 1);
        check(ws::prepare_process_environment(ws::Mode::LlqFused).has_value(),
              "SENSEN_QKV_FUSION=0 is accepted");
        ::unsetenv("SENSEN_QKV_FUSION");
        check(ws::prepare_process_environment(ws::Mode::Llq).has_value(), "unset is set to 0");
        const char* now = std::getenv("SENSEN_QKV_FUSION");
        check(now != nullptr && std::string_view{now} == "0", "and is now 0");
        ::unsetenv("MORTGAGE_WEIGHT_STORE");
        const auto unset = ws::mode_from_env("MORTGAGE_WEIGHT_STORE");
        check(unset.has_value() && *unset == ws::Mode::Dense, "an unset selector is dense");
        ::setenv("MORTGAGE_WEIGHT_STORE", "lql", 1);
        check(!ws::mode_from_env("MORTGAGE_WEIGHT_STORE").has_value(), "a misspelt selector is refused");
        ::unsetenv("MORTGAGE_WEIGHT_STORE");
    }

    std::println("\n{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
