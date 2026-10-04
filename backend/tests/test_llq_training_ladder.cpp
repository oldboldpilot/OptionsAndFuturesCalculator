/**
 * THE TRAINING LADDER: does an LLQ image pay on a frozen QLoRA base, at each of
 * the eleven QuantPrecision rungs -- and is the route EXACT?
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * NO EXTERNAL TEST FRAMEWORK (rule 39): a `check()` and two counters, and a
 * non-zero exit is the failure signal ctest reads.
 *
 * WHY THIS EXISTS, AND WHY IT IS THE 6-BIT RUNG'S REAL HOME
 *
 * The serving ladder (`test_llq_ladder`) holds the 4-, 5-, 8- and 16-bit rungs
 * and REFUSES 6-bit, because GGUF's only 6-bit weight format is Q6_K and its
 * scale granularity is sixteen elements, below the codec's group floor of
 * thirty-two. That refusal is about Q6_K, not about six bits.
 *
 * sensen has a second, entirely separate low-bit ladder -- `QuantPrecision`,
 * eleven rungs including INT6 and both FP6 variants -- which is what a frozen
 * QLoRA base is held in (`CpuFrozenLowBitWeight` on the CPU,
 * `autograd_cuda::FrozenLowBitWeight` on the GPU). Its shape is codes against
 * one scale per 64 elements plus a codebook, which is exactly
 * `LlqMatrix::fromCodes` plus `LlqGemv::runLut`. Sixty-four clears the codec's
 * floor. So the 6-bit rung IS reachable, through this path.
 *
 * AND THE CPU SIDE DOES NOT TAKE IT, WHILE THE GPU SIDE ALREADY DOES.
 * `FrozenLowBitWeight::ensureBaseLlq()` builds precisely this image from
 * `codebookRankMap` + `packedToSigned` + `LlqMatrix::fromCodes`.
 * `CpuFrozenLowBitWeight` references LLQ nowhere. That asymmetry is the gap,
 * and this file measures whether closing it would be worth anything before
 * anybody closes it.
 *
 * WHAT IS MEASURED, AND THE HYPOTHESIS IT REFUTED
 *
 * The serving ladder measured the 4- and 5-bit GGUF rungs at base == source
 * with ZERO outliers and an image slightly LARGER than dense, because an
 * absmax-fitted scale per 32 values drives at least one code per block to the
 * range edge, so the byte-minimal base can only be the source width.
 *
 * THE EXPECTATION WAS THAT A CODEBOOK RUNG WOULD BEHAVE DIFFERENTLY, AND IT
 * DOES NOT. The reasoning was that NF4's codes are value RANKS into a
 * normal-float table, so on a trained (roughly Gaussian) weight the rank
 * histogram is bell-shaped rather than flat to the edge -- the extreme ranks
 * rare, which is the condition sensen's own compression evidence names as the
 * one where a narrower base plus a sparse residual pays. Two rungs of the same
 * width should then have given opposite answers.
 *
 * MEASURED, ALL ELEVEN RUNGS GIVE base == code width, ZERO OUTLIERS, AND AN
 * IMAGE 1.000x THE PACKED SIZE -- NF4 exactly like INT4. The reasoning was
 * right about the histogram over the whole matrix and wrong about the thing
 * that decides the base: the scale is absmax-fitted PER 64-ELEMENT BLOCK, so
 * the extreme code is used at least once in every block, and a panel contains
 * many blocks. A bell-shaped GLOBAL histogram with a full-range LOCAL one still
 * forces the base to the full width.
 *
 * So the discriminator is not the width and not the code distribution: it is
 * WHETHER A PER-GROUP ABSMAX SCALE IS FITTED AT ALL. Every rung on both
 * ladders is group-scaled and none of them compresses. The one format that
 * does -- bf16, at -26.1% -- is the one that is NOT group-scaled: its exponent
 * plane carries no scale, and the redundancy LLQ finds there is low exponent
 * cardinality in a fixed-width field, which group scaling is precisely what
 * removes.
 *
 * That is why this file reports bits/weight DERIVED FROM THE BYTE COUNTS and
 * asserts an IMPLICATION rather than a figure: the numbers here are a property
 * of group scaling, and recording "NF4 saves X%" would record a measurement
 * that is 1.000x and would stay quoted if the mechanism ever changed.
 *
 * AND IT ASSERTS THE ROUTE IS EXACT. An LLQ-resident frozen base is only
 * admissible if dequantising from the image gives the same floats as
 * dequantising from the packed codes -- both are `codebook[code] * scale`, so
 * it is a lookup and a multiply either way with nothing reordered, and the
 * claim is BIT-IDENTITY rather than a bound. That is checked against
 * `CpuFrozenLowBitWeight::dequantiseInto`, the one function the QLoRA forward
 * AND backward both go through (`lora.cppm`'s
 * `CpuFrozenLowBitMatmulBackward` re-dequantises there by design), so an exact
 * route is exact for training and not only for inference.
 *
 * WHAT IS NOT CLAIMED. Nothing here is wired into training. This measures
 * whether the route is exact and whether it saves bytes; it does not make
 * `CpuFrozenLowBitWeight` hold an image, and it reports no throughput. A rung
 * that saves bytes here still owes a decode-rate measurement before anyone
 * claims a speed-up, for the reason the serving ladder's `llq` arm is not a
 * serving tier: fewer bytes on paper is not fewer bytes read.
 */
#include <cstdlib>
#include <new>

#include "quant_lowbit_codebook.h"

import std;
import sensen.lossless_quant;
import sensen.cpu_frozen_lowbit;

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

namespace llq = sensen::llq;
using QP = sensen::quant::QuantPrecision;

constexpr std::array kRungs = {
    QP::INT8, QP::FP8_E4M3, QP::FP8_E5M2, QP::INT6, QP::FP6_E2M3, QP::FP6_E3M2,
    QP::INT5, QP::FP5,      QP::NF4,      QP::FP4,  QP::INT4};

[[nodiscard]] auto rung_name(QP p) -> std::string {
    switch (p) {
        case QP::INT8: return "INT8";
        case QP::FP8_E4M3: return "FP8_E4M3";
        case QP::FP8_E5M2: return "FP8_E5M2";
        case QP::INT6: return "INT6";
        case QP::FP6_E2M3: return "FP6_E2M3";
        case QP::FP6_E3M2: return "FP6_E3M2";
        case QP::INT5: return "INT5";
        case QP::FP5: return "FP5";
        case QP::NF4: return "NF4";
        case QP::FP4: return "FP4";
        case QP::INT4: return "INT4";
    }
    return "?";
}

/// A weight shaped like a trained one: zero-mean Gaussian. This is the
/// distribution the whole question turns on -- a codebook rung's rank
/// histogram is bell-shaped on a Gaussian weight and flat on a uniform one, so
/// measuring on uniform noise would answer a question nobody asked and would
/// make NF4 look like INT4.
[[nodiscard]] auto gaussian_weight(std::size_t rows, std::size_t cols, std::uint32_t seed)
    -> std::vector<float> {
    std::vector<float> w(rows * cols);
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0F, 0.02F);  // a typical trained scale
    for (float& v : w) {
        v = nd(rng);
    }
    return w;
}

struct Row {
    std::string rung;
    std::uint32_t code_bits{0};
    std::size_t packed_bytes{0};
    std::size_t scale_bytes{0};
    std::size_t llq_bytes{0};
    std::size_t outliers{0};
    std::uint32_t base{0};
    double packed_bpw{0.0};
    double llq_bpw{0.0};
    bool exact{false};
    bool built{false};
    std::string note;
};

/// Build the LLQ image of a frozen base at one rung and report what it costs.
///
/// The conversion is the one the GPU path already uses: a rank map over the
/// codebook, `packedToSigned` to read the production packing, then
/// `fromCodes`. Nothing here is a second implementation of that -- which
/// matters, because a second one could disagree with the device and the
/// disagreement would be invisible.
[[nodiscard]] auto measure(QP p, const std::vector<float>& w, std::size_t rows, std::size_t cols)
    -> Row {
    Row out;
    out.rung = rung_name(p);
    out.code_bits = static_cast<std::uint32_t>(sensen::quant::codeBits(p));

    auto fw = sensen::quant::CpuFrozenLowBitWeight::fromHostFp32(w, rows, cols, p);
    if (!fw) {
        out.note = "fromHostFp32 refused";
        return out;
    }
    const auto n = fw->elements();
    out.packed_bytes = fw->codes().size();
    out.scale_bytes = fw->scales().size() * sizeof(float);
    out.packed_bpw =
        static_cast<double>(out.packed_bytes + out.scale_bytes) * 8.0 / static_cast<double>(n);

    const auto map = llq::codebookRankMap(fw->codebook());
    if (!map) {
        out.note = "codebookRankMap refused";
        return out;
    }
    // The codec takes 2..8 bits. Every rung is inside that, but assert rather
    // than assume: a packing whose code width disagreed with the rank map's
    // would be refused by packedToSigned, and silently skipping it would read
    // as a rung that does not compress.
    if (llq::packingCodeBits(fw->packing()) != map->code_bits) {
        out.note = std::format("packing {} is {} bits, rank map {}", fw->packing(),
                               llq::packingCodeBits(fw->packing()), map->code_bits);
        return out;
    }
    auto codes = llq::packedToSigned(fw->codes(), n, fw->packing(), *map);
    if (!codes) {
        out.note = "packedToSigned refused";
        return out;
    }

    // The frozen weight's scale index is `flat_index / block`, i.e. one scale
    // per `block` elements of the ROW-MAJOR flattening. LlqMatrix's group is
    // per row, so the two agree only when `cols % block == 0`; the caller picks
    // a width that satisfies it and this asserts it rather than trusting it.
    const std::size_t block = fw->blockSize();
    if (cols % block != 0) {
        out.note = std::format("cols {} not a multiple of block {}", cols, block);
        return out;
    }
    auto m = llq::LlqMatrix::fromCodes(*codes, fw->scales(), rows, cols,
                                       llq::LlqOptions::bits(map->code_bits).withGroup(
                                           static_cast<std::uint32_t>(block)));
    if (!m) {
        out.note = "LlqMatrix::fromCodes refused";
        return out;
    }
    out.built = true;
    out.llq_bytes = m->stats().total_bytes;
    out.outliers = m->outlierCount();
    out.base = m->baseBits();
    out.llq_bpw = static_cast<double>(out.llq_bytes) * 8.0 / static_cast<double>(n);

    // ── EXACTNESS: the image dequantises to the same floats as the packed
    //    codes. Both are codebook[code] * scale, so this is a lookup and a
    //    multiply either way with nothing reordered -- bit-identity, not a
    //    bound. Checked against dequantiseInto, which is the function the
    //    QLoRA forward and backward BOTH go through.
    const auto ranked = llq::rankedCodebook(*map, fw->codebook());
    if (!ranked) {
        out.note = "rankedCodebook refused";
        return out;
    }
    const auto via_llq = m->dequantizeLut(*ranked);
    if (!via_llq) {
        out.note = "dequantizeLut refused";
        return out;
    }
    std::vector<float> via_packed(n, 0.0F);
    if (!fw->dequantiseInto(via_packed)) {
        out.note = "dequantiseInto refused";
        return out;
    }
    out.exact = std::ranges::equal(*via_llq, via_packed, [](float a, float b) noexcept -> bool {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    });
    return out;
}

}  // namespace

auto main() -> int {
    std::println("LLQ ON THE TRAINING LADDER: the eleven QuantPrecision rungs of a frozen base");

    // 512 x 1024: cols a multiple of the codec's 64 AND of the frozen weight's
    // 64-element block, and big enough that a rank histogram is meaningful.
    constexpr std::size_t kRows = 512;
    constexpr std::size_t kCols = 1024;
    const auto w = gaussian_weight(kRows, kCols, 0xC0FFEEU);
    std::println("  weight {}x{} = {} elements, zero-mean Gaussian sigma 0.02 (a trained shape)",
                 kRows, kCols, kRows * kCols);

    section("per rung: packed vs LLQ, bits/weight DERIVED from byte counts");
    std::println("  {:<9} {:>4} {:>6} {:>12} {:>12} {:>8} {:>5} {:>9} {:>9} {:>7}", "rung",
                 "bits", "base", "packed B", "LLQ B", "outlier%", "excl", "packed bpw", "llq bpw",
                 "ratio");
    std::vector<Row> rows;
    for (const QP p : kRungs) {
        const Row r = measure(p, w, kRows, kCols);
        rows.push_back(r);
        if (!r.built) {
            std::println("  {:<9} {:>4} {:>6} {:>12} {:>12} {:>8} {:>5} {:>9} {:>9} {:>7}  <- {}",
                         r.rung, r.code_bits, "-", "-", "-", "-", "-", "-", "-", "-", r.note);
            continue;
        }
        const double frac =
            100.0 * static_cast<double>(r.outliers) / static_cast<double>(kRows * kCols);
        std::println("  {:<9} {:>4} {:>6} {:>12} {:>12} {:>7.3f}% {:>5} {:>9.4f} {:>9.4f} "
                     "{:>6.3f}x",
                     r.rung, r.code_bits, r.base, r.packed_bytes + r.scale_bytes, r.llq_bytes,
                     frac, r.exact ? "yes" : "NO", r.packed_bpw, r.llq_bpw,
                     r.llq_bpw / r.packed_bpw);
    }

    section("every rung builds, and the route is EXACT at every one");
    for (const Row& r : rows) {
        check(r.built, std::format("{}: an LLQ image builds from the frozen base's own packed "
                                   "codes, through the SAME rank-map conversion the GPU path "
                                   "uses{}",
                                   r.rung, r.built ? "" : " (" + r.note + ")"));
    }
    for (const Row& r : rows) {
        if (!r.built) {
            continue;
        }
        check(r.exact,
              std::format("{}: dequantising from the image is BIT-IDENTICAL to dequantising from "
                          "the packed codes -- so an LLQ-resident frozen base would change no "
                          "gradient, which is what licenses the route for training",
                          r.rung));
    }

    section("what it is WORTH, per rung -- the question the measurement answers");
    const auto by_name = [&rows](std::string_view n) -> const Row* {
        for (const Row& r : rows) {
            if (r.rung == n) {
                return &r;
            }
        }
        return nullptr;
    };
    const Row* nf4 = by_name("NF4");
    const Row* int4 = by_name("INT4");
    const Row* int6 = by_name("INT6");
    check(nf4 != nullptr && int4 != nullptr && int6 != nullptr,
          "NF4, INT4 and INT6 were all measured");
    if (nf4 == nullptr || int4 == nullptr || int6 == nullptr || !nf4->built || !int4->built) {
        std::println("\n{} passed / {} failed", g_checks - g_failures, g_failures);
        return g_failures == 0 ? 0 : 1;
    }

    std::println("  NF4  base {} outliers {:.3f}%  {:.4f} -> {:.4f} bpw", nf4->base,
                 100.0 * static_cast<double>(nf4->outliers) /
                     static_cast<double>(kRows * kCols),
                 nf4->packed_bpw, nf4->llq_bpw);
    std::println("  INT4 base {} outliers {:.3f}%  {:.4f} -> {:.4f} bpw", int4->base,
                 100.0 * static_cast<double>(int4->outliers) /
                     static_cast<double>(kRows * kCols),
                 int4->packed_bpw, int4->llq_bpw);
    std::println("  INT6 base {} outliers {:.3f}%  {:.4f} -> {:.4f} bpw", int6->base,
                 100.0 * static_cast<double>(int6->outliers) /
                     static_cast<double>(kRows * kCols),
                 int6->packed_bpw, int6->llq_bpw);

    // THE PROPERTY, not a recorded figure. Whether a rung's image is SMALLER
    // than its packed form is a property of that rung's code distribution on
    // this weight, and the thing worth asserting is that the two are
    // consistent: a base strictly narrower than the code width is exactly the
    // condition under which bytes can be saved, and a base equal to it is
    // exactly the condition under which they cannot.
    //
    // Pinning "NF4 saves X%" would be recording a measurement that moves with
    // the weight, the shape and the seed. Pinning the IMPLICATION cannot go
    // stale: it is a statement about the codec.
    for (const Row& r : rows) {
        if (!r.built) {
            continue;
        }
        const bool narrower = r.base < r.code_bits;
        const bool smaller = r.llq_bytes < r.packed_bytes + r.scale_bytes;
        check(!smaller || narrower,
              std::format("{}: the image is smaller ONLY IF the base is narrower than the code "
                          "width ({} vs {} bits) -- bytes cannot be saved while every code still "
                          "needs its full field",
                          r.rung, r.base, r.code_bits));
        check(!(r.base == r.code_bits) || r.outliers == 0,
              std::format("{}: base == code width implies ZERO outliers, since nothing can fall "
                          "outside a field as wide as the source",
                          r.rung));
    }

    std::println("\n{} passed / {} failed", g_checks - g_failures, g_failures);
    return g_failures == 0 ? 0 : 1;
}
