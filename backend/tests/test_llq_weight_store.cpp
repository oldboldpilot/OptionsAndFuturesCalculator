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

    // ── the 16-bit tier ───────────────────────────────────────────────────
    //
    // The claim is the same shape as the Q8 one and rests on a different fact:
    // LlqBf16Matrix splits a bf16 word into an LLQ-coded exponent plane and a raw
    // sign|mantissa byte, and rejoining them is exact for EVERY bf16 value. So the
    // words the source hands back are the words the GGUF held, and the unchanged
    // 16-bit kernel computes what it computed from the dense buffer.
    section("bf16: rows materialise byte-identically, including the values that usually break codecs");
    {
        // Half the rows are ordinary trained-weight magnitudes; the rest are the
        // awkward ones. A codec that special-cases zero, flushes subnormals or
        // canonicalises a NaN payload passes a test made only of normal numbers.
        const std::size_t rows = 192;
        const std::size_t k = 256;
        std::vector<std::uint16_t> words(rows * k);
        std::uint32_t s = 2463534242U;
        const std::array<std::uint16_t, 8> awkward{
            0x0000,  // +0
            0x8000,  // -0
            0x0001,  // smallest positive subnormal
            0x807F,  // negative subnormal
            0x7F80,  // +inf
            0xFF80,  // -inf
            0x7FC1,  // NaN with a payload
            0xFFFF,  // NaN, all bits set
        };
        for (std::size_t i = 0; i < words.size(); ++i) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            if ((i % 37) == 0) {
                words[i] = awkward[(i / 37) % awkward.size()];
            } else {
                // A narrow exponent band, as a real weight matrix has: this is what
                // gives the exponent plane a small base width.
                const std::uint32_t exp = 118U + (s % 9U);
                words[i] = static_cast<std::uint16_t>(((s & 0x8000U)) | (exp << 7U) | (s & 0x7FU));
            }
        }
        // The dense column-major buffer the 16-bit kernels read: element (col,row)
        // at (col * rows + row).
        std::vector<std::uint16_t> dense(rows * k);
        for (std::size_t r = 0; r < rows; ++r) {
            for (std::size_t c = 0; c < k; ++c) {
                dense[(c * rows) + r] = words[(r * k) + c];
            }
        }
        const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(words.data()),
                                     words.size() * 2);
        const auto src = ws::LlqBf16Source::from_bf16_row_major(bytes, rows, k, ws::Mode::Llq);
        check(src.has_value(), "a bf16 weight adopts");
        if (src.has_value()) {
            const auto [lo, hi] = (*src)->base_bits_range();
            check(hi < 8, std::format("the exponent plane codes in {}..{} bits, under bf16's 8", lo, hi));
            // Bits per weight must come from the BYTES. The base range above omits
            // the 8 raw sign|mantissa bits entirely, so quoting it as bits/weight
            // would understate the image by half.
            const double bpw = static_cast<double>((*src)->resident_bytes()) * 8.0 /
                               static_cast<double>(rows * k);
            check(bpw > static_cast<double>(hi) && bpw < 16.0,
                  std::format("measured {:.3f} bits/weight sits above the base width and under 16", bpw));

            const std::array<std::pair<std::size_t, std::size_t>, 5> slices{
                {{0, 192}, {0, 1}, {5, 77}, {63, 129}, {191, 192}}};
            for (const auto& [r0, r1] : slices) {
                const std::size_t width = r1 - r0;
                std::vector<std::uint8_t> tile(k * width * 2, 0xEE);
                const bool ok = (*src)->materialise(r0, r1, tile);
                bool same = ok;
                for (std::size_t c = 0; same && c < k; ++c) {
                    for (std::size_t t = 0; same && t < width; ++t) {
                        std::uint16_t got = 0;
                        std::memcpy(&got, tile.data() + (((c * width) + t) * 2), sizeof(got));
                        same = got == dense[(c * rows) + (r0 + t)];
                    }
                }
                check(same, std::format("bf16 rows [{}, {}) are bit-identical to the dense buffer",
                                        r0, r1));
            }
            std::vector<std::uint8_t> small(4, 0);
            check(!(*src)->materialise(0, 192, small), "a bf16 destination that is too small is refused");
            std::vector<std::uint8_t> tile2(k * 64 * 2);
            check(!(*src)->materialise(150, 250, tile2), "bf16 rows beyond the weight are refused");
        }

        // The seam, through sensen's own kernel, with the dense buffer destroyed.
        for (const std::size_t m : {std::size_t{1}, std::size_t{5}}) {
            const auto a = make_activation(m, k, static_cast<std::uint32_t>(700 + m));
            std::vector<float> ref(m * rows, -1.0F);
            GEMM::clearBf16WeightSources();
            GEMM::matvecQuantizedBatch(a.data(), dense.data(), ref.data(), m, k, rows,
                                       GEMM::QType::BF16);

            const auto s2 = ws::LlqBf16Source::from_bf16_row_major(bytes, rows, k, ws::Mode::Llq);
            check(s2.has_value(), "the bf16 source rebuilds for the seam arm");
            if (!s2.has_value()) {
                continue;
            }
            GEMM::registerBf16WeightSource(dense.data(), *s2);
            const auto saved = dense;
            std::ranges::fill(dense, 0xA5A5);

            std::vector<float> got(m * rows, -2.0F);
            GEMM::matvecQuantizedBatch(a.data(), dense.data(), got.data(), m, k, rows,
                                       GEMM::QType::BF16);
            check(bit_equal(ref, got),
                  std::format("bf16 m = {}: LLQ output is bit-identical to dense", m));

            GEMM::unregisterBf16WeightSource(dense.data());
            std::vector<float> garbage(m * rows, -3.0F);
            GEMM::matvecQuantizedBatch(a.data(), dense.data(), garbage.data(), m, k, rows,
                                       GEMM::QType::BF16);
            check(!bit_equal(ref, garbage),
                  std::format("bf16 m = {}: the poisoned dense buffer gives a DIFFERENT answer "
                              "(positive control)", m));
            dense = saved;
        }
        check(GEMM::bf16WeightSourceCount() == 0, "no bf16 registration is left behind");

        // ── FUSED bf16: the tier that is actually served, and it must be
        // bit-identical too ────────────────────────────────────────────────
        //
        // This is the opposite of the Q8_0 fused tier, which trades identity for
        // the fused kernel because per-block integer totals have thrown the dense
        // tile's 8-lane FMA structure away. Rejoining an exponent plane and a raw
        // sign|mantissa plane is a BIT JOIN, so the fused bf16 kernel sums the
        // same floats in the same canonical order and identity SURVIVES. Asserted
        // rather than argued, because if it does not hold that is a defect and not
        // a property of the tier.
        for (const std::size_t m : {std::size_t{1}, std::size_t{5}}) {
            const auto a = make_activation(m, k, static_cast<std::uint32_t>(900 + m));
            std::vector<float> ref(m * rows, -1.0F);
            GEMM::clearBf16WeightSources();
            GEMM::matvecQuantizedBatch(a.data(), dense.data(), ref.data(), m, k, rows,
                                       GEMM::QType::BF16);

            const auto sf = ws::LlqBf16Source::from_bf16_row_major(bytes, rows, k,
                                                                   ws::Mode::LlqFused);
            check(sf.has_value(), "llq-fused ADOPTS a bf16 weight");
            if (!sf.has_value()) {
                continue;
            }
            GEMM::registerBf16WeightSource(dense.data(), *sf);
            const auto saved = dense;
            std::ranges::fill(dense, 0xA5A5);

            std::vector<float> got(m * rows, -2.0F);
            GEMM::matvecQuantizedBatch(a.data(), dense.data(), got.data(), m, k, rows,
                                       GEMM::QType::BF16);
            check(bit_equal(ref, got),
                  std::format("bf16 FUSED m = {}: output is bit-identical to dense", m));

            GEMM::unregisterBf16WeightSource(dense.data());
            std::vector<float> garbage(m * rows, -3.0F);
            GEMM::matvecQuantizedBatch(a.data(), dense.data(), garbage.data(), m, k, rows,
                                       GEMM::QType::BF16);
            check(!bit_equal(ref, garbage),
                  std::format("bf16 FUSED m = {}: the poisoned dense buffer gives a DIFFERENT "
                              "answer (positive control)", m));
            dense = saved;
        }
        check(GEMM::bf16WeightSourceCount() == 0, "no fused bf16 registration is left behind");

        // A non-fused source must DECLINE gemvSlice, or `llq` would quietly serve
        // the fused kernel and the two tiers would stop being distinguishable.
        {
            const auto plain = ws::LlqBf16Source::from_bf16_row_major(bytes, rows, k, ws::Mode::Llq);
            const std::vector<float> act(k, 1.0F);
            std::vector<float> out(rows, 0.0F);
            check(plain.has_value() && !(*plain)->gemvSlice(act, out, 0, rows),
                  "a Mode::Llq bf16 source DECLINES the fused path");
        }

        // The refusals, in the direction that matters.
        std::vector<std::uint8_t> odd(rows * 100 * 2, 0);
        check(!ws::LlqBf16Source::from_bf16_row_major(odd, rows, 100, ws::Mode::Llq).has_value(),
              "a column count that is not a multiple of 64 is refused");
        check(!ws::LlqBf16Source::from_bf16_row_major(bytes, rows, k * 2, ws::Mode::Llq).has_value(),
              "a byte count that disagrees with the declared shape is refused");
    }

    // ── an F16 model must NOT be adopted as bf16 ──────────────────────────
    //
    // This is the sharpest refusal in the module. An F16 word is
    // sign|exp(5)|mantissa(10) and a bf16 word is sign|exp(8)|mantissa(7); they
    // are the same width and the same dense layout, so nothing about the buffer
    // could tell them apart. Reading F16 bits with bf16 field widths would encode
    // WITHOUT ERROR and describe a field split the format does not have. The
    // registry is therefore consulted only for BF16, and an F16 weight is left
    // dense -- which, because adopting nothing refuses, stops the process.
    section("F16 is left dense, not adopted as bf16");
    {
        const std::size_t rows = 64;
        const std::size_t k = 64;
        std::vector<std::uint16_t> f16(rows * k);
        for (std::size_t i = 0; i < f16.size(); ++i) {
            f16[i] = static_cast<std::uint16_t>(0x3C00U + (i % 512U));  // ordinary F16 values
        }
        std::vector<std::uint16_t> f16_dense(rows * k);
        auto scope = ws::LoadScope::open(ws::Mode::Llq);
        GEMM::transposeQuantizedWeights(f16.data(), f16_dense.data(), rows, k, GEMM::QType::F16);
        const auto rep = scope->report();
        check(rep.adopted == 0 && rep.left_dense == 1,
              "an F16 weight is counted left-dense and never adopted");
        check(GEMM::bf16WeightSourceCount() == 0, "nothing is registered in the bf16 map for F16");
        const auto v = scope->finish();
        check(!v.has_value(),
              "so an LLQ store REFUSES an F16 model rather than serving it from the dense path");
        scope->withdraw();
    }

    // ── the DEFAULT path, through the entry point the services actually call ──
    //
    // Every other check here drives `LoadScope::open(Mode)` directly. The services
    // call `open_scope_from_env`, and NOTHING exercised that on the Dense path --
    // which is the configuration production runs. That gap let a regression reach
    // a built engine: the mortgage assistant logged an EMPTY weight-store error and
    // went UNAVAILABLE with `MORTGAGE_WEIGHT_STORE` unset. A default that breaks is
    // worse than a feature that does, so it gets its own section.
    section("the DEFAULT path: open_scope_from_env + finish() with no selector set");
    {
        for (const char* sel : {static_cast<const char*>(nullptr), "dense"}) {
            if (sel == nullptr) {
                ::unsetenv("MORTGAGE_WEIGHT_STORE");
            } else {
                ::setenv("MORTGAGE_WEIGHT_STORE", sel, 1);
            }
            const std::string what = sel == nullptr ? "unset" : "explicitly dense";
            auto scope = ws::open_scope_from_env("MORTGAGE_WEIGHT_STORE");
            check(scope.has_value(),
                  std::format("{}: open_scope_from_env succeeds (error: '{}')", what,
                              scope.has_value() ? std::string{} : scope.error()));
            if (!scope.has_value()) {
                continue;
            }
            check((*scope)->mode() == ws::Mode::Dense, std::format("{}: the scope is Dense", what));
            const auto v = (*scope)->finish();
            check(v.has_value(), std::format("{}: finish() succeeds (error: '{}')", what,
                                             v.has_value() ? std::string{} : v.error()));
            check(GEMM::q8WeightSourceCount() == 0 && GEMM::bf16WeightSourceCount() == 0,
                  std::format("{}: a Dense scope registers nothing in either map", what));
        }
        ::unsetenv("MORTGAGE_WEIGHT_STORE");
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

    // ── the row-restricted projection: identity, and BOTH of its branches ──
    //
    // matvecQuantizedRows is what makes a grammar-constrained lm_head cheap: it
    // computes only the admitted rows. Two things have to hold, and only one of
    // them is about arithmetic.
    //
    // IDENTITY: a row it writes must equal the row the FULL projection would have
    // written, bit for bit. The reference is the same kernel on the same weights
    // in this same process -- never a figure recorded earlier.
    //
    // BOTH BRANCHES: it runs serially below 32 rows and across the thread pool
    // above, mirroring matvecQuantized's own floor. The constrained case it exists
    // for admits ~10 rows, so the PARALLEL branch is never reached by any
    // workload this engine actually serves -- which would leave it shipped and
    // unexercised. threadCount() returns a reference, so the discriminator is set
    // HERE rather than inferred from the machine: that is what makes "the parallel
    // branch ran" a fact instead of a hope.
    section("row-restricted projection: identical rows, on both branches");
    {
        const std::size_t rows = 1024;
        const std::size_t k = 256;
        const auto w = make_weight(rows, k, 127, 31);
        const auto dense = transposed(w);
        const auto a = make_activation(1, k, 7);
        GEMM::clearQ8WeightSources();

        std::vector<float> full(rows, 0.0F);
        GEMM::matvecQuantized(a.data(), dense.data(), full.data(), k, rows, GEMM::QType::Q8_0);

        // 10 rows is the measured median admitted set; 300 is a wide grammar state.
        const std::vector<std::uint32_t> narrow{0U, 3U, 17U, 64U, 65U, 200U, 511U, 512U, 900U,
                                                1023U};
        std::vector<std::uint32_t> wide;
        for (std::uint32_t r = 0; r < 300U; ++r) {
            wide.push_back(r * 3U);  // strided, so it is not one contiguous slice
        }
        check(narrow.size() < 32 && wide.size() >= 32,
              std::format("the two row sets straddle the 32-row floor ({} and {})", narrow.size(),
                          wide.size()));

        // setThreadCount, not the private counter: it also resizes the shared
        // parallel_for arena, so "8 threads" means eight real workers rather than
        // a GEMM that merely believes it has them.
        const std::size_t saved_threads = GEMM::getThreadCount();
        for (const auto& [label, forced] :
             std::vector<std::pair<std::string, std::size_t>>{{"serial (1 thread)", 1},
                                                              {"parallel (8 threads)", 8}}) {
            GEMM::setThreadCount(forced);
            for (const auto& [set_name, set] :
                 std::vector<std::pair<std::string, std::vector<std::uint32_t>>>{
                     {"narrow", narrow}, {"wide", wide}}) {
                std::vector<float> got(rows, -7.0F);
                GEMM::matvecQuantizedRows(a.data(), dense.data(), got.data(), k, rows, set,
                                          GEMM::QType::Q8_0);
                bool rows_match = true;
                for (const std::uint32_t r : set) {
                    if (std::memcmp(&got[r], &full[r], sizeof(float)) != 0) {
                        rows_match = false;
                        break;
                    }
                }
                check(rows_match, std::format("{}, {} set: every requested row is bit-identical "
                                              "to the full projection",
                                              label, set_name));
                // Rows NOT asked for must be left exactly as they were. The caller
                // gathers by row index out of a shared flat buffer, so a stray
                // write here would corrupt another sequence's logits rather than
                // this one's -- a defect that would not show on this sequence.
                std::vector<bool> asked(rows, false);
                for (const std::uint32_t r : set) {
                    asked[r] = true;
                }
                bool untouched = true;
                for (std::size_t r = 0; untouched && r < rows; ++r) {
                    untouched = asked[r] || got[r] == -7.0F;
                }
                check(untouched,
                      std::format("{}, {} set: unrequested rows are untouched", label, set_name));
            }
        }
        GEMM::setThreadCount(saved_threads);

        // An out-of-range row must be SKIPPED, not read past the matrix. The
        // caller's own gather already assigns -inf for such a row, so skipping is
        // the correct division of labour -- but reading row 5000 of a 1024-row
        // matrix would be an out-of-bounds read rather than a wrong number.
        const std::vector<std::uint32_t> ranged{5U, 5000U, 9U};
        std::vector<float> mixed(rows, -7.0F);
        GEMM::matvecQuantizedRows(a.data(), dense.data(), mixed.data(), k, rows, ranged,
                                  GEMM::QType::Q8_0);
        check(std::memcmp(&mixed[5], &full[5], sizeof(float)) == 0 &&
                  std::memcmp(&mixed[9], &full[9], sizeof(float)) == 0,
              "an out-of-range row is skipped and its in-range neighbours still compute");
    }

    std::println("\n{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
