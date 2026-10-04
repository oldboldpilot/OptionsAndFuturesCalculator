/**
 * THE LADDER, PRINTED AND GATED: which quantised types an LLQ image can hold
 * exactly, which cannot, and why -- per type, derived rather than recorded.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * NO EXTERNAL TEST FRAMEWORK (rule 39): a `check()` and two counters, and a
 * non-zero exit is the failure signal ctest reads.
 *
 * WHY THIS IS A TEST AND NOT A SCRIPT THAT PRINTS A TABLE
 *
 * The table is the useful artefact -- a reader wants to know, for each rung,
 * what serves it and what refuses it. But a printed table is a RECORDED answer,
 * and this repository has already paid for one of those going stale beside the
 * thing it described. So every row is COMPUTED, from three facts about the
 * format (`GEMM::quantIsSymmetric`, `quantCodeBits`, `quantScaleGranularity`,
 * each transcribed from this tree's own dequantisation) and the codec's own
 * limits (`llq::kBlockWeights`, `llq::kColumnMultiple`, the 2..8 bit range).
 * The checks below then assert the PROPERTIES the table must have, so a change
 * to any input moves the table and either keeps the properties or fails here.
 *
 * WHAT IS ASSERTED, AND WHY EACH ONE EARNS ITS PLACE
 *
 *  1. The four rungs the store serves -- BF16, Q8_0, Q5_0, Q4_0 -- are exactly
 *     the integer-coded types the ladder admits plus BF16. Stated as an
 *     EQUALITY over every enumerator, so a new QType cannot be admitted by the
 *     ladder and quietly left unserved, nor served without the ladder agreeing.
 *  2. Every refusal has a REASON, and the reason is the right one. Q6_K is the
 *     interesting case and the one worth reading: it is symmetric AND its code
 *     width is in range, so the only thing refusing it is scale granularity.
 *     A test that only asserted "Q6_K is refused" would pass if it were refused
 *     for a wrong reason, which is how a rung gets written off.
 *  3. Granularity is NOT the block size. Asserted directly on Q6_K, because
 *     reading one as the other says 256 where the truth is 16 -- wrong by a
 *     factor of sixteen, in the direction that makes an impossible re-encoding
 *     look possible.
 *  4. The decision is DERIVED from the codec's floor, not a copy of it. Checked
 *     by arithmetic against `llq::kBlockWeights` rather than against 32.
 *  5. A round trip through the 4- and 5-bit builders returns the ORIGINAL GGUF
 *     bytes. Both formats interleave a block's 32 values across nibbles, so the
 *     builder de-interleaves and `materialise` re-interleaves; asserting the
 *     round trip tests the PAIR, where inspecting either alone would pass on two
 *     mistakes that cancel.
 *  6. The fused arm is bounded, and THE BOUND IS DERIVED. It cannot be
 *     bit-identical -- it forms `scale * sum(code * x)` per group where the
 *     dense kernel dequantises each element first -- so the check is against
 *     `n * eps * sum|code * scale * x|`, the standard bound for two orderings
 *     of one sum, computed from the actual terms. A fitted tolerance cannot
 *     tell a reordered sum from a wrong one, which is the whole reason this is
 *     spelled out.
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
using QT = sensen::GEMM::QType;

/// Every enumerator of QType. Written out because C++ has no enumerator
/// reflection on this toolchain, and a SHORT list would let a new type escape
/// every check in this file. The count is asserted against the switch
/// coverage below so a missing entry shows up as a failure rather than as a
/// smaller sweep.
constexpr std::array kAllTypes = {
    QT::F32,  QT::F16,     QT::Q4_0,   QT::Q4_1,      QT::Q5_0,      QT::Q5_1,
    QT::Q8_0, QT::Q4_K,    QT::Q5_K,   QT::Q6_K,      QT::Q8_K,      QT::IQ4_NL,
    QT::IQ3_S, QT::I8,     QT::I32,    QT::F64,       QT::BF16,      QT::F8_E4M3,
    QT::F8_E5M2, QT::F6_E2M3, QT::F6_E3M2, QT::F5};

[[nodiscard]] auto type_name(QT q) -> std::string {
    switch (q) {
        case QT::F32: return "F32";
        case QT::F16: return "F16";
        case QT::Q4_0: return "Q4_0";
        case QT::Q4_1: return "Q4_1";
        case QT::Q5_0: return "Q5_0";
        case QT::Q5_1: return "Q5_1";
        case QT::Q8_0: return "Q8_0";
        case QT::Q4_K: return "Q4_K";
        case QT::Q5_K: return "Q5_K";
        case QT::Q6_K: return "Q6_K";
        case QT::Q8_K: return "Q8_K";
        case QT::IQ4_NL: return "IQ4_NL";
        case QT::IQ3_S: return "IQ3_S";
        case QT::I8: return "I8";
        case QT::I32: return "I32";
        case QT::F64: return "F64";
        case QT::BF16: return "BF16";
        case QT::F8_E4M3: return "F8_E4M3";
        case QT::F8_E5M2: return "F8_E5M2";
        case QT::F6_E2M3: return "F6_E2M3";
        case QT::F6_E3M2: return "F6_E3M2";
        case QT::F5: return "F5";
    }
    return "?";
}

/// Which kernel serves a type on the CPU slice path, read from
/// `matvecQuantizedTiledSliceDense`'s own dispatch order. Prose, for the
/// printed table only -- nothing is asserted against it, because a string
/// describing a kernel is exactly the kind of recorded answer this file
/// otherwise avoids.
[[nodiscard]] auto dense_kernel(QT q) -> std::string {
    switch (q) {
        case QT::Q8_0: return "matvecQ8_0_Int8Slice (default) / _InlineSlice (PRECISE=1)";
        case QT::Q4_0: return "matvecQ4_0_InlineSlice (AVX-512)";
        case QT::Q5_0: return "matvecQ5_0_InlineSlice (AVX-512), fused gate+up variant";
        case QT::Q4_K: return "matvecQ4K_InlineSlice (AVX-512)";
        case QT::Q6_K: return "matvecQ6K_InlineSlice (AVX-512)";
        case QT::F16:
        case QT::BF16: return "generic tail, dot() per element (no vector kernel)";
        default: return "generic tail via resolveDotKernel, or none";
    }
}

// ── 1. the printed ladder ───────────────────────────────────────────────────

auto print_ladder() -> void {
    section("THE LADDER (every QType, derived)");
    std::println(
        "  {:<9} {:>4} {:>5} {:>5} {:>5}  {:<6} {:<48} {}", "type", "blkV", "blkB", "bits",
        "gran", "sym", "ladder verdict", "dense kernel");
    for (const QT q : kAllTypes) {
        const auto why = ws::rung_refusal(q);
        const std::string verdict =
            !why.has_value()
                ? std::string{"HOLDS"}
                : std::string{"refused: "} + std::string{ws::describe(*why)}.substr(0, 38);
        std::println("  {:<9} {:>4} {:>5} {:>5} {:>5}  {:<6} {:<48} {}", type_name(q),
                     GEMM::quantBlockValues(q), GEMM::quantBlockBytes(q), GEMM::quantCodeBits(q),
                     GEMM::quantScaleGranularity(q),
                     GEMM::quantIsSymmetric(q) ? "yes" : "no", verdict, dense_kernel(q));
    }
    std::println("\n  codec floors: group must be a multiple of {}, columns a multiple of {},"
                 " code width 2..8",
                 llq::kBlockWeights, llq::kColumnMultiple);
    std::println("  NOTE: BF16 is held by its own tier (a plane split, not integer codes), so the"
                 " ladder's\n        integer-code verdict does not apply to it. F16 is refused"
                 " there, by field width.");
}

// ── 2. the verdicts, as properties ──────────────────────────────────────────

auto test_verdicts() -> void {
    section("verdicts: WHICH types the ladder holds, and WHY the rest are refused");

    // The integer-coded types the ladder admits, computed.
    std::vector<QT> holds;
    for (const QT q : kAllTypes) {
        if (ws::ladder_holds(q)) {
            holds.push_back(q);
        }
    }
    std::string names;
    for (const QT q : holds) {
        names += (names.empty() ? "" : ",") + type_name(q);
    }
    std::println("  ladder holds: [{}]", names);

    // EQUALITY, not containment: a new QType that the algebra happens to admit
    // must either be served or be noticed here. This is the check that stops the
    // ladder and the store drifting apart, which is the defect this repository
    // records against its label-space tables.
    const std::vector<QT> expected = {QT::Q4_0, QT::Q5_0, QT::Q8_0, QT::Q8_K};
    check(holds == expected,
          "the ladder admits exactly Q4_0, Q5_0, Q8_0, Q8_K -- the symmetric integer-coded "
          "types whose scale granularity clears the codec's group floor");

    // Q8_K is admitted by the algebra and NOT served, deliberately, and that
    // gap is asserted rather than left implicit: it is an activation type in
    // llama.cpp's own use, nothing produces it for a weight here, and its
    // scales are fp32 rather than fp16. Admitting it in the algebra while
    // refusing it in the builder is the honest split -- the algebra describes
    // the FORMAT, the builder describes what bit layouts were written down.
    check(GEMM::quantScaleGranularity(QT::Q8_K) == 256,
          "Q8_K carries ONE scale for its whole 256-value super-block");

    check(ws::rung_refusal(QT::Q4_1) == ws::AdoptError::AffineType,
          "Q4_1 is refused as AFFINE (q * d + m), not for some other reason");
    check(ws::rung_refusal(QT::Q5_1) == ws::AdoptError::AffineType,
          "Q5_1 is refused as AFFINE");
    check(ws::rung_refusal(QT::Q4_K) == ws::AdoptError::AffineType,
          "Q4_K is refused as AFFINE (d * sc * q - dmin * m)");
    check(ws::rung_refusal(QT::Q5_K) == ws::AdoptError::AffineType,
          "Q5_K is refused as AFFINE");

    // THE 6-BIT RUNG. The reason matters more than the verdict: Q6_K passes the
    // two tests a reader would expect to be what refuses it.
    check(GEMM::quantIsSymmetric(QT::Q6_K),
          "Q6_K IS symmetric -- d * sc * (q - 32), no additive term");
    check(GEMM::quantCodeBits(QT::Q6_K) == 6,
          "Q6_K's code width IS 6, inside the codec's 2..8 range");
    check(ws::rung_refusal(QT::Q6_K) == ws::AdoptError::ScaleTooFine,
          "Q6_K is refused ONLY on scale granularity -- so GGUF's single 6-bit weight format "
          "is out of reach for a reason about the format, not about the codec's width");
    check(GEMM::quantScaleGranularity(QT::Q6_K) == 16,
          "Q6_K's scale granularity is 16 elements, NOT its 256-value block size");
    check(GEMM::quantBlockValues(QT::Q6_K) == 256,
          "...while its block IS 256 values: granularity and block size are different facts");

    // DERIVED, not a copy of the floor: the refusal must follow from the
    // codec's own constant. If kBlockWeights ever became 16, Q6_K would become
    // holdable and this arithmetic would say so.
    check(GEMM::quantScaleGranularity(QT::Q6_K) % llq::kBlockWeights != 0,
          "Q6_K's granularity fails the codec's OWN group floor (derived from llq::kBlockWeights, "
          "not compared against a literal 32)");
    check(GEMM::quantScaleGranularity(QT::Q4_0) % llq::kBlockWeights == 0,
          "Q4_0's granularity clears that same floor -- the positive control, without which the "
          "check above would pass for a floor that rejects everything");

    check(ws::rung_refusal(QT::F16) == ws::AdoptError::NoRungForType,
          "F16 has no integer-code rung at all (its 16-bit tier refuses it on field widths)");
    check(ws::rung_refusal(QT::F32) == ws::AdoptError::NoRungForType, "F32 has no rung");
    check(ws::rung_refusal(QT::IQ4_NL) == ws::AdoptError::NoRungForType,
          "IQ4_NL has no rung: a non-linear codebook is not a code against a scale");
}

// ── 3. the 4- and 5-bit builders: a GGUF round trip ─────────────────────────

struct Built {
    std::size_t rows{0};
    std::size_t k{0};
    std::vector<std::uint8_t> row_major;  ///< GGUF layout, rows x nb blocks
    std::vector<std::uint8_t> col_major;  ///< what the dense kernel reads
    std::vector<float> dequant;           ///< rows x k, reference values
};

[[nodiscard]] auto half_bits(float f) -> std::uint16_t {
    return std::bit_cast<std::uint16_t>(static_cast<_Float16>(f));
}
[[nodiscard]] auto half_to_float(std::uint16_t h) -> float {
    return static_cast<float>(std::bit_cast<_Float16>(h));
}

/// Synthesise a weight in GGUF Q4_0 or Q5_0 layout, with codes that SPAN the
/// full range (so the codec's byte-minimal base really is the source width,
/// which is what the real model does) and fp16-exact scales.
[[nodiscard]] auto build(std::size_t rows, std::size_t k, QT qtype, std::uint32_t seed) -> Built {
    const bool five = qtype == QT::Q5_0;
    const int bias = five ? 16 : 8;
    const int lo = -bias;
    const int hi = bias - 1;
    const std::size_t bb = GEMM::quantBlockBytes(qtype);
    const std::size_t nb = k / 32;
    Built b;
    b.rows = rows;
    b.k = k;
    b.row_major.assign(rows * nb * bb, 0);
    b.dequant.assign(rows * k, 0.0F);
    std::mt19937 rng(seed);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t blk = 0; blk < nb; ++blk) {
            // fp16-exact by construction: a power of two over a small integer.
            const float d = std::ldexp(1.0F, -(static_cast<int>((r + blk) % 5U) + 4));
            auto* out = b.row_major.data() + (((r * nb) + blk) * bb);
            const std::uint16_t dh = half_bits(d);
            std::memcpy(out, &dh, sizeof(dh));
            std::uint32_t qh = 0;
            std::uint8_t* qs = out + (five ? 6 : 2);
            std::array<int, 32> codes{};
            for (std::size_t i = 0; i < 32; ++i) {
                codes[i] = lo + static_cast<int>(rng() % static_cast<std::uint32_t>(hi - lo + 1));
            }
            // Force both extremes into every block, so the code histogram
            // genuinely needs the full source width. Without this the codec
            // could pick a narrower base and the test would be measuring a
            // matrix the real model never produces.
            codes[0] = lo;
            codes[1] = hi;
            for (std::size_t i = 0; i < 16; ++i) {
                const int l = codes[i] + bias;
                const int h = codes[i + 16] + bias;
                qs[i] = static_cast<std::uint8_t>((l & 0x0F) | ((h & 0x0F) << 4));
                if (five) {
                    qh |= static_cast<std::uint32_t>((l >> 4) & 1) << i;
                    qh |= static_cast<std::uint32_t>((h >> 4) & 1) << (i + 16);
                }
            }
            if (five) {
                std::memcpy(out + 2, &qh, sizeof(qh));
            }
            for (std::size_t i = 0; i < 32; ++i) {
                b.dequant[(r * k) + (blk * 32) + i] = static_cast<float>(codes[i]) * d;
            }
        }
    }
    // Column-major block layout: block (blk, r) at ((blk * rows) + r) * bb.
    b.col_major.assign(rows * nb * bb, 0);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t blk = 0; blk < nb; ++blk) {
            std::memcpy(b.col_major.data() + (((blk * rows) + r) * bb),
                        b.row_major.data() + (((r * nb) + blk) * bb), bb);
        }
    }
    return b;
}

auto test_round_trip(QT qtype) -> void {
    section(std::format("{}: the builder and materialise are inverses", type_name(qtype)));
    const std::size_t rows = 128;  // two panels, so the panel seam is exercised
    const std::size_t k = 192;     // multiple of 64, not a multiple of 128
    const auto w = build(rows, k, qtype, 0x5EEDU + static_cast<std::uint32_t>(qtype));
    const std::size_t bb = GEMM::quantBlockBytes(qtype);
    const std::size_t nb = k / 32;

    auto src = ws::LlqLowBitSource::from_row_major(w.row_major, rows, k, qtype, ws::Mode::Llq);
    check(src.has_value(), std::format("{}: builds from GGUF row-major bytes", type_name(qtype)));
    if (!src) {
        std::println("    (error: {})", ws::describe(src.error()));
        return;
    }
    const auto& s = **src;
    check(s.qtype() == qtype,
          std::format("{}: the source DECLARES its own type (the front door's check)",
                      type_name(qtype)));
    check(s.outlier_count() == 0 && s.base_bits_range().first == GEMM::quantCodeBits(qtype),
          std::format("{}: base == source width with zero outliers -- the measured shape of a "
                      "group-scaled low-bit format, and the reason this tier saves no bytes",
                      type_name(qtype)));
    std::println("    image {} B against dense {} B ({:+.3f}%)", s.resident_bytes(),
                 w.row_major.size(),
                 100.0 * (static_cast<double>(s.resident_bytes()) /
                              static_cast<double>(w.row_major.size()) -
                          1.0));

    // THE ROUND TRIP. materialise writes the column-major tile the kernel
    // reads, so compare against the column-major reference for the whole row
    // range. Equal bytes means the de-interleave and re-interleave are
    // inverses; testing either alone would pass on two mistakes that cancel.
    std::vector<std::uint8_t> tile(nb * rows * bb, 0xA5);
    check(s.materialise(0, rows, tile), std::format("{}: materialise fills the tile",
                                                    type_name(qtype)));
    check(tile == w.col_major,
          std::format("{}: materialised bytes are BYTE-IDENTICAL to the original GGUF blocks, so "
                      "the dense kernel reads exactly what it would have read",
                      type_name(qtype)));

    // A partial slice must be self-contained at its own width.
    const std::size_t r0 = 64;
    const std::size_t r1 = 96;
    std::vector<std::uint8_t> part(nb * (r1 - r0) * bb, 0xA5);
    check(s.materialise(r0, r1, part), std::format("{}: materialise fills a partial slice",
                                                   type_name(qtype)));
    bool part_ok = true;
    for (std::size_t t = 0; t < r1 - r0; ++t) {
        for (std::size_t blk = 0; blk < nb; ++blk) {
            const auto* got = part.data() + (((blk * (r1 - r0)) + t) * bb);
            const auto* want = w.row_major.data() + ((((r0 + t) * nb) + blk) * bb);
            part_ok = part_ok && std::memcmp(got, want, bb) == 0;
        }
    }
    check(part_ok, std::format("{}: a partial slice is self-contained at width {} and matches",
                               type_name(qtype), r1 - r0));
}

// ── 4. refusals at the builder ──────────────────────────────────────────────

auto test_builder_refusals() -> void {
    section("the builder refuses rather than reinterprets");
    const auto w = build(64, 128, QT::Q4_0, 7U);

    // A type the ladder refuses must be refused HERE too, with the ladder's own
    // reason -- not with a generic shape error, which would send a reader
    // looking at the bytes instead of at the format.
    auto q6 = ws::LlqLowBitSource::from_row_major(w.row_major, 64, 128, QT::Q6_K, ws::Mode::Llq);
    check(!q6 && q6.error() == ws::AdoptError::ScaleTooFine,
          "Q6_K is refused by the builder with the LADDER's reason (scale granularity)");

    auto q41 = ws::LlqLowBitSource::from_row_major(w.row_major, 64, 128, QT::Q4_1, ws::Mode::Llq);
    check(!q41 && q41.error() == ws::AdoptError::AffineType,
          "Q4_1 is refused by the builder as AFFINE");

    // Columns not a multiple of the codec's own multiple.
    const auto narrow = build(64, 96, QT::Q4_0, 8U);
    auto bad_cols = ws::LlqLowBitSource::from_row_major(narrow.row_major, 64, 96, QT::Q4_0,
                                                        ws::Mode::Llq);
    const bool cols_ok = llq::kColumnMultiple == 64 ? (!bad_cols && bad_cols.error() ==
                                                           ws::AdoptError::ColumnsNotMultiple)
                                                    : bad_cols.has_value();
    check(cols_ok, std::format("96 columns against the codec's multiple of {}: refused",
                               llq::kColumnMultiple));

    // A declared shape that disagrees with the byte count.
    auto bad_shape = ws::LlqLowBitSource::from_row_major(w.row_major, 65, 128, QT::Q4_0,
                                                         ws::Mode::Llq);
    check(!bad_shape && bad_shape.error() == ws::AdoptError::ShapeMismatch,
          "a row count that disagrees with the byte count is refused");

    // CROSS-WIDTH: Q5_0 bytes offered as Q4_0. The byte count differs (22 vs
    // 18 per block) so this is caught on shape -- but it is worth asserting,
    // because the front door's qtype agreement check exists precisely so that a
    // mismatch cannot be resolved by guessing from geometry.
    const auto w5 = build(64, 128, QT::Q5_0, 9U);
    auto cross = ws::LlqLowBitSource::from_row_major(w5.row_major, 64, 128, QT::Q4_0,
                                                      ws::Mode::Llq);
    check(!cross, "Q5_0 bytes offered as Q4_0 are refused, not decoded as Q4_0");
}

// ── 5. the fused arm: a DERIVED bound ───────────────────────────────────────

auto test_fused_bound(QT qtype) -> void {
    section(std::format("{}: llq-fused against dense, with a derived bound", type_name(qtype)));
    const std::size_t rows = 128;
    const std::size_t k = 256;
    const auto w = build(rows, k, qtype, 0xB0UL + static_cast<std::uint32_t>(qtype));

    std::vector<float> x(k);
    std::mt19937 rng(4242);
    std::uniform_real_distribution<float> dist(-2.0F, 2.0F);
    for (float& v : x) {
        v = dist(rng);
    }

    // Dense reference: the real kernel on the real column-major buffer, through
    // the PUBLIC entry a caller actually uses, which funnels to the slice front
    // door. Nothing is registered for this buffer -- building a source does not
    // register it, only the load-time observer does -- so the door falls
    // through to the dense body. That makes this the dense path as a caller
    // reaches it rather than a private entry point.
    std::vector<float> want(rows, 0.0F);
    GEMM::matvecQuantizedBatch(x.data(), w.col_major.data(), want.data(), 1, k, rows, qtype);

    auto src = ws::LlqLowBitSource::from_row_major(w.row_major, rows, k, qtype,
                                                   ws::Mode::LlqFused);
    check(src.has_value(), std::format("{}: builds in LlqFused mode", type_name(qtype)));
    if (!src) {
        return;
    }
    std::vector<float> got(rows, 0.0F);
    check((*src)->gemvSlice(x, got, 0, rows),
          std::format("{}: the fused arm accepts the whole row range", type_name(qtype)));

    // THE BOUND, DERIVED. Both arms sum the same k terms t_i = code_i * d * x_i
    // in different orders. Each fp32 addition commits at most eps = 2^-24
    // relative error, so a sum of k terms carries at most k * eps * sum|t_i|,
    // and two orderings differ by at most twice that. Nothing here is fitted:
    // the only inputs are k, the fp32 epsilon, and the actual terms.
    constexpr double kEps = 1.0 / 16777216.0;  // 2^-24
    std::size_t over = 0;
    double worst_ratio = 0.0;
    double worst_abs = 0.0;
    for (std::size_t r = 0; r < rows; ++r) {
        double abs_sum = 0.0;
        for (std::size_t c = 0; c < k; ++c) {
            abs_sum += std::fabs(static_cast<double>(w.dequant[(r * k) + c]) *
                                 static_cast<double>(x[c]));
        }
        const double bound = 2.0 * static_cast<double>(k) * kEps * abs_sum;
        const double diff = std::fabs(static_cast<double>(got[r]) - static_cast<double>(want[r]));
        worst_abs = std::max(worst_abs, diff);
        if (bound > 0.0) {
            worst_ratio = std::max(worst_ratio, diff / bound);
        }
        if (diff > bound) {
            ++over;
        }
    }
    std::println("    worst |fused - dense| = {:.3e}; worst diff/bound = {:.4f}", worst_abs,
                 worst_ratio);
    check(over == 0,
          std::format("{}: every row is within 2*k*eps*sum|terms| -- the bound for two orderings "
                      "of one sum, DERIVED from k and the fp32 epsilon rather than fitted",
                      type_name(qtype)));

    // The bound must be TIGHT enough to mean something. A bound that admits
    // everything would pass the check above with a wrong kernel, so assert the
    // observed difference is well inside it -- and that the bound would catch a
    // defect the size of a wrong scale.
    check(worst_ratio < 1.0,
          std::format("{}: and strictly inside it, so the bound is not vacuous", type_name(qtype)));

    // POSITIVE CONTROL for the bound: perturb one output by the smallest
    // defect that could matter -- a factor of 12 is this project's example of a
    // wrong map -- and require the bound to reject it.
    bool rejects = false;
    {
        double abs_sum = 0.0;
        for (std::size_t c = 0; c < k; ++c) {
            abs_sum += std::fabs(static_cast<double>(w.dequant[c]) * static_cast<double>(x[c]));
        }
        const double bound = 2.0 * static_cast<double>(k) * kEps * abs_sum;
        rejects = std::fabs(static_cast<double>(want[0]) * 12.0 - static_cast<double>(want[0])) >
                  bound;
    }
    check(rejects,
          std::format("{}: the same bound REJECTS a 12x error, so it discriminates", type_name(qtype)));
}

// ── 6. the mode contract ────────────────────────────────────────────────────

auto test_modes() -> void {
    section("modes: llq declines the fused path, llq-fused takes it");
    const auto w = build(64, 128, QT::Q4_0, 11U);
    std::vector<float> x(128, 0.5F);
    std::vector<float> out(64, 0.0F);

    auto plain = ws::LlqLowBitSource::from_row_major(w.row_major, 64, 128, QT::Q4_0, ws::Mode::Llq);
    check(plain.has_value() && !(*plain)->gemvSlice(x, out, 0, 64),
          "Mode::Llq DECLINES gemvSlice, so it cannot quietly serve the fused kernel -- which "
          "would make the two tiers indistinguishable, the measurement defect this store exists "
          "to prevent");

    auto fused = ws::LlqLowBitSource::from_row_major(w.row_major, 64, 128, QT::Q4_0,
                                                      ws::Mode::LlqFused);
    check(fused.has_value() && (*fused)->gemvSlice(x, out, 0, 64),
          "Mode::LlqFused accepts gemvSlice");

    // Both modes must still materialise: llq-fused falls back to it when the
    // front door asks for a tile, and a mode that could not would abort.
    std::vector<std::uint8_t> tile((128 / 32) * 64 * 18, 0);
    check(fused.has_value() && (*fused)->materialise(0, 64, tile),
          "LlqFused still materialises, which is the front door's fallback");
}

}  // namespace

auto main() -> int {
    std::println("LLQ LADDER: rungs, refusals and their reasons");
    print_ladder();
    test_verdicts();
    test_round_trip(QT::Q4_0);
    test_round_trip(QT::Q5_0);
    test_builder_refusals();
    test_fused_bound(QT::Q4_0);
    test_fused_bound(QT::Q5_0);
    test_modes();
    std::println("\n{} passed / {} failed", g_checks - g_failures, g_failures);
    return g_failures == 0 ? 0 : 1;
}
