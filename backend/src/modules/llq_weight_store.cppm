/**
 * Serving a Q8_0 model from an LLQ image instead of the dense Q8_0 buffer.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS IS
 *
 * `sensen.lossless_quant` (LLQ) holds an already-quantised weight matrix as a
 * dense B-bit base plus a CSR of the few residuals, and decodes it back to the
 * exact codes and scales. Until this module existed the codec was linked into
 * no binary, so no request had ever been served from an LLQ-held weight and
 * every LLQ speed figure was a kernel microbenchmark.
 *
 * This module is the SEAM. It is selected explicitly and defaults OFF:
 *
 *     MORTGAGE_WEIGHT_STORE = dense (default) | llq | llq-fused
 *     STRATEGY_WEIGHT_STORE = dense (default) | llq | llq-fused
 *
 * Two variables and no fallback between them, for the reason
 * `MORTGAGE_MODEL_PATH` does not fall back to `MODEL_PATH`: an operator who
 * opted ONE model into a different numeric path must not have the other
 * quietly follow. An unrecognised value is REFUSED, not read as `dense` -- a
 * typo must not silently serve the path the operator meant to leave.
 *
 * ---------------------------------------------------------------------------
 * HOW IT PLUGS IN, AND WHY THE SENSEN PATCH IS SO SMALL
 *
 * sensen's Q8_0 kernels receive one thing that identifies a weight: the address
 * of its dense column-major buffer (`b_T`). `GEMM::registerQ8WeightSource`
 * associates a `Q8WeightSource` with that address, and the two Int8 slice front
 * doors consult it. `GEMM::setQuantTransposeObserver` is told about every
 * weight as it is transposed at model load -- with the GGUF row-major bytes
 * still alive -- which is where an LLQ image is built.
 *
 * Nothing above the kernel knows. Attention, the feed-forward network and the
 * model class are untouched.
 *
 * TWO MODES, WITH DIFFERENT PROMISES:
 *
 *   `llq`        materialise the slice's rows from the LLQ image into a Q8_0 tile
 *                and run sensen's EXISTING kernel on it. The kernel sees the same
 *                bytes it always did, so the output is byte-identical to the
 *                dense path BY CONSTRUCTION. This measures the cost of decoding
 *                on the decode path, and is the accuracy-neutral tier.
 *
 *   `llq-fused`  compute the slice with `LlqGemv::runInt8Exact`, one integer per
 *                32-weight block, and combine in float. The integer part is
 *                exact; the float accumulation ORDER differs from sensen's
 *                8-lane FMA tile, so this is NOT promised to be bit-identical and
 *                the harness reports token agreement rather than assuming it.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS AND IS NOT COVERED
 *
 * Every weight that passes through `transposeQuantizedWeights` at load: the
 * per-layer Q/K/V/O and gate/up/down projections. NOT covered: the token
 * embedding and the tied `lm_head`, which sensen holds row-major and reads
 * through a different kernel. On Qwen3-0.6B that is 1 of 197 Q8_0 tensors but
 * 155,582,464 of 595,984,384 weights (26%), so a throughput figure from this
 * path describes 74% of the weight stream and says so in `Report`.
 *
 * QKV fusion must be off: `build_qkv_decode_plan` copies Q, K and V into a
 * second interleaved buffer whose address is registered nowhere, so with fusion
 * on, three of the seven per-layer projections would silently keep serving from
 * dense while the run reported LLQ. `prepare_process_environment` turns it off
 * (it recovers 239 MB and measured no decode cost) and refuses an explicit
 * `SENSEN_QKV_FUSION=1`.
 *
 * ---------------------------------------------------------------------------
 * WHY A REFUSAL, AND NOT A REINTERPRETATION, FOR A GROUP SIZE THAT IS NOT 32
 *
 * A Q8_0 block is 32 int8 codes and ONE fp16 scale. An LLQ image with group
 * size 64 holds one scale per 64 codes; rewriting it as Q8_0 blocks would have
 * to invent a second scale for each pair of blocks, and the arithmetic would
 * then be a different (wrong) model with no error anywhere. `adopt` therefore
 * accepts exactly {8 source bits, group 32, scales exactly representable as
 * fp16} and returns a named error for everything else.
 */
module;

#include <cstdlib>
#include <new>

#include <sys/mman.h>

export module llq_weight_store;

import std;
import sensen.gemm;
import sensen.lossless_quant;

export namespace llq_weight_store {

namespace llq = sensen::llq;

// ── Selection ───────────────────────────────────────────────────────────────

enum class Mode : std::uint8_t {
    Dense,     ///< the existing path, untouched (default)
    Llq,       ///< LLQ image, materialised into a Q8_0 tile, existing kernel
    LlqFused,  ///< LLQ image, fused per-block integer GEMV, float combine
};

[[nodiscard]] constexpr auto mode_name(Mode m) noexcept -> std::string_view {
    switch (m) {
        case Mode::Dense:
            return "dense";
        case Mode::Llq:
            return "llq";
        case Mode::LlqFused:
            return "llq-fused";
    }
    return "unknown";
}

/// Empty or `dense` is Dense. An unrecognised value is nullopt: refused, never
/// defaulted, so a misspelt `llq` cannot quietly serve the dense path.
[[nodiscard]] constexpr auto parse_mode(std::string_view v) noexcept -> std::optional<Mode> {
    if (v.empty() || v == "dense") {
        return Mode::Dense;
    }
    if (v == "llq") {
        return Mode::Llq;
    }
    if (v == "llq-fused") {
        return Mode::LlqFused;
    }
    return std::nullopt;
}

// ── Adoption ────────────────────────────────────────────────────────────────

enum class AdoptError : std::uint8_t {
    NotEightBit,        ///< source bits != 8: not a Q8_0 code range
    GroupNotThirtyTwo,  ///< group size != 32: one scale per 32 codes is the Q8_0 block
    ScaleNotHalfExact,  ///< a scale does not survive fp16 -> the Q8_0 header cannot hold it
    ShapeMismatch,      ///< rows/cols disagree with the caller's declaration
    ColumnsNotMultiple, ///< cols not a multiple of 64: LLQ cannot hold it
    Encode,             ///< the codec refused the codes
};

[[nodiscard]] constexpr auto describe(AdoptError e) noexcept -> std::string_view {
    switch (e) {
        case AdoptError::NotEightBit:
            return "LLQ image is not 8-bit: it cannot be presented as Q8_0 blocks";
        case AdoptError::GroupNotThirtyTwo:
            return "LLQ image does not carry one scale per 32 codes: a Q8_0 block cannot be "
                   "rebuilt from it without inventing scales";
        case AdoptError::ScaleNotHalfExact:
            return "an LLQ scale is not exactly representable as fp16: a Q8_0 block header "
                   "cannot hold it";
        case AdoptError::ShapeMismatch:
            return "LLQ image shape disagrees with the declared weight shape";
        case AdoptError::ColumnsNotMultiple:
            return "columns are not a multiple of 64: LLQ cannot hold this matrix";
        case AdoptError::Encode:
            return "the LLQ codec refused the codes";
    }
    return "unknown";
}

inline constexpr std::size_t kQ8BlockValues = 32;
inline constexpr std::size_t kQ8BlockBytes = 34;  ///< fp16 scale + 32 int8
/// Rows per LLQ panel. A weight is held as ceil(rows / 64) independent images so
/// a slice of output rows (which is what sensen's workers are handed) touches
/// only the panels it covers, and the fused GEMV can run serially per panel
/// without re-reading the rest of the matrix.
inline constexpr std::size_t kPanelRows = 64;

[[nodiscard]] inline auto half_bits(float f) noexcept -> std::uint16_t {
    return std::bit_cast<std::uint16_t>(static_cast<_Float16>(f));
}

[[nodiscard]] inline auto half_to_float(std::uint16_t h) noexcept -> float {
    return static_cast<float>(std::bit_cast<_Float16>(h));
}

/// One Q8_0 weight held as LLQ panels. Immutable after construction, so every
/// worker thread may call it concurrently.
class LlqQ8Source final : public sensen::GEMM::Q8WeightSource {
  public:
    /// Adopt an already-built LLQ panel set. `panels` must tile `rows` rows of
    /// `k` columns in order, every panel 8-bit, group 32, fp16-exact scales.
    [[nodiscard]] static auto adopt(std::vector<llq::LlqMatrix> panels, std::size_t rows,
                                    std::size_t k, Mode mode)
        -> std::expected<std::shared_ptr<const LlqQ8Source>, AdoptError> {
        std::size_t covered = 0;
        for (const auto& p : panels) {
            if (p.sourceBits() != 8) {
                return std::unexpected(AdoptError::NotEightBit);
            }
            if (p.groupSize() != kQ8BlockValues) {
                return std::unexpected(AdoptError::GroupNotThirtyTwo);
            }
            if (p.cols() != k) {
                return std::unexpected(AdoptError::ShapeMismatch);
            }
            for (const float s : p.scales()) {
                if (half_to_float(half_bits(s)) != s) {
                    return std::unexpected(AdoptError::ScaleNotHalfExact);
                }
            }
            covered += p.rows();
        }
        if (covered != rows || panels.empty()) {
            return std::unexpected(AdoptError::ShapeMismatch);
        }
        return std::shared_ptr<const LlqQ8Source>(new LlqQ8Source(std::move(panels), rows, k, mode));
    }

    /// Build from GGUF row-major Q8_0 bytes: `rows` rows of k/32 blocks of
    /// {fp16 scale, 32 int8}.
    [[nodiscard]] static auto from_q8_0_row_major(std::span<const std::uint8_t> blocks,
                                                  std::size_t rows, std::size_t k, Mode mode)
        -> std::expected<std::shared_ptr<const LlqQ8Source>, AdoptError> {
        if (k == 0 || k % kQ8BlockValues != 0 || rows == 0) {
            return std::unexpected(AdoptError::ShapeMismatch);
        }
        if (k % 64 != 0) {
            return std::unexpected(AdoptError::ColumnsNotMultiple);
        }
        const std::size_t nb = k / kQ8BlockValues;
        if (blocks.size() != rows * nb * kQ8BlockBytes) {
            return std::unexpected(AdoptError::ShapeMismatch);
        }
        std::vector<llq::LlqMatrix> panels;
        panels.reserve((rows + kPanelRows - 1) / kPanelRows);
        std::vector<std::int8_t> codes;
        std::vector<float> scales;
        for (std::size_t r0 = 0; r0 < rows; r0 += kPanelRows) {
            const std::size_t pr = std::min(kPanelRows, rows - r0);
            codes.resize(pr * k);
            scales.resize(pr * nb);
            for (std::size_t t = 0; t < pr; ++t) {
                for (std::size_t b = 0; b < nb; ++b) {
                    const auto* blk = blocks.data() + (((r0 + t) * nb) + b) * kQ8BlockBytes;
                    std::uint16_t dh = 0;
                    std::memcpy(&dh, blk, sizeof(dh));
                    scales[(t * nb) + b] = half_to_float(dh);
                    std::memcpy(codes.data() + (t * k) + (b * kQ8BlockValues), blk + 2,
                                kQ8BlockValues);
                }
            }
            auto m = llq::LlqMatrix::fromCodes(
                codes, scales, pr, k, llq::LlqOptions::bits(8).withGroup(kQ8BlockValues));
            if (!m) {
                return std::unexpected(AdoptError::Encode);
            }
            panels.push_back(std::move(*m));
        }
        return adopt(std::move(panels), rows, k, mode);
    }

    [[nodiscard]] auto rows() const noexcept -> std::size_t { return rows_; }
    [[nodiscard]] auto cols() const noexcept -> std::size_t { return k_; }
    [[nodiscard]] auto mode() const noexcept -> Mode { return mode_; }

    /// Bytes this weight occupies as LLQ (payload, scales and headers).
    [[nodiscard]] auto resident_bytes() const noexcept -> std::size_t {
        std::size_t total = 0;
        for (const auto& p : panels_) {
            total += p.stats().total_bytes;
        }
        return total;
    }
    [[nodiscard]] auto outlier_count() const noexcept -> std::size_t {
        std::size_t total = 0;
        for (const auto& p : panels_) {
            total += p.outlierCount();
        }
        return total;
    }
    /// Smallest and largest base width across panels (they are chosen per panel).
    [[nodiscard]] auto base_bits_range() const noexcept -> std::pair<std::uint32_t, std::uint32_t> {
        std::uint32_t lo = 8;
        std::uint32_t hi = 0;
        for (const auto& p : panels_) {
            lo = std::min(lo, p.baseBits());
            hi = std::max(hi, p.baseBits());
        }
        return {lo, hi};
    }

    [[nodiscard]] auto materialise(std::size_t r_start, std::size_t r_end,
                                   std::span<std::uint8_t> dst) const noexcept -> bool override {
        const std::size_t w = r_end - r_start;
        const std::size_t nb = k_ / kQ8BlockValues;
        if (r_start >= r_end || r_end > rows_ || dst.size() < nb * w * kQ8BlockBytes) {
            return false;
        }
        thread_local std::vector<std::int8_t> row_codes;
        for (std::size_t t = 0; t < w; ++t) {
            const std::size_t r = r_start + t;
            const auto& panel = panels_[r / kPanelRows];
            const std::size_t pr = r % kPanelRows;
            const auto scales = panel.rowScales(pr);
            // B = 8 with no residuals: the packed stream IS the two's-complement
            // codes, so a block is one memcpy. Any other shape goes through the
            // codec's own row decode (correct, and scalar).
            const bool direct = panel.baseBits() == 8 && panel.outlierCount() == 0;
            std::span<const std::uint8_t> raw;
            if (direct) {
                raw = panel.packedRow(pr);
            } else {
                row_codes.resize(k_);
                panel.decodeRow(pr, row_codes);
            }
            for (std::size_t b = 0; b < nb; ++b) {
                auto* out = dst.data() + (((b * w) + t) * kQ8BlockBytes);
                const std::uint16_t dh = half_bits(scales[b]);
                std::memcpy(out, &dh, sizeof(dh));
                if (direct) {
                    std::memcpy(out + 2, raw.data() + (b * kQ8BlockValues), kQ8BlockValues);
                } else {
                    std::memcpy(out + 2, row_codes.data() + (b * kQ8BlockValues), kQ8BlockValues);
                }
            }
        }
        return true;
    }

    [[nodiscard]] auto gemvSlice(std::span<const std::int8_t> aq, std::span<const float> ad,
                                 std::span<float> c, std::size_t r_start,
                                 std::size_t r_end) const noexcept -> bool override {
        if (mode_ != Mode::LlqFused) {
            return false;
        }
        const std::size_t nb = k_ / kQ8BlockValues;
        if (aq.size() != k_ || ad.size() != nb || c.size() != r_end - r_start || r_end > rows_) {
            return false;
        }
        thread_local llq::LlqActivation xa;
        thread_local std::vector<std::uint8_t> biased;
        thread_local std::vector<std::int32_t> acc;
        biased.resize(k_);
        for (std::size_t i = 0; i < k_; ++i) {
            biased[i] = static_cast<std::uint8_t>(static_cast<int>(aq[i]) + 128);
        }
        xa.assignBiased(biased, 1.0F);
        llq::LlqGemv gemv;
        (void)gemv.withSerialExecution(true);  // already inside a worker's slice
        for (std::size_t p = r_start / kPanelRows; p * kPanelRows < r_end; ++p) {
            const auto& panel = panels_[p];
            const std::size_t base = p * kPanelRows;
            acc.resize(panel.rows() * nb);
            if (!gemv.runInt8Exact(panel, xa, acc)) {
                return false;  // declined: the caller materialises instead
            }
            const std::size_t lo = std::max(r_start, base);
            const std::size_t hi = std::min(r_end, base + panel.rows());
            for (std::size_t r = lo; r < hi; ++r) {
                const std::size_t pr = r - base;
                const auto scales = panel.rowScales(pr);
                float sum = 0.0F;
                for (std::size_t g = 0; g < nb; ++g) {
                    sum += static_cast<float>(acc[(pr * nb) + g]) * (scales[g] * ad[g]);
                }
                c[r - r_start] = sum;
            }
        }
        return true;
    }

  private:
    LlqQ8Source(std::vector<llq::LlqMatrix> panels, std::size_t rows, std::size_t k, Mode mode)
        : panels_(std::move(panels)), rows_(rows), k_(k), mode_(mode) {}

    std::vector<llq::LlqMatrix> panels_;
    std::size_t rows_;
    std::size_t k_;
    Mode mode_;
};

// ── Load scope ──────────────────────────────────────────────────────────────

/// What a scope did. Reading this is the difference between "LLQ was used" and
/// "LLQ was asked for".
struct Report {
    Mode mode{Mode::Dense};
    std::size_t adopted{0};          ///< Q8_0 weights now served from an LLQ image
    std::size_t left_dense{0};       ///< quantized weights of another type, untouched
    std::size_t refused{0};          ///< Q8_0 weights LLQ could not hold (still dense)
    std::uint64_t adopted_weights{0};
    std::uint64_t dense_bytes{0};    ///< what the adopted weights cost as Q8_0
    std::uint64_t llq_bytes{0};      ///< what they cost as LLQ
    std::uint64_t outliers{0};
    std::uint32_t base_bits_min{8};
    std::uint32_t base_bits_max{0};
    std::vector<std::string> refusals;  ///< first few, named

    [[nodiscard]] auto fully_adopted() const noexcept -> bool {
        return refused == 0 && adopted > 0;
    }
    [[nodiscard]] auto summary() const -> std::string {
        std::string s = std::format(
            "weight store {}: {} Q8_0 weights served from LLQ ({} weights, {} B as Q8_0 -> {} B as "
            "LLQ, {} outliers, base {}..{} bits), {} refused, {} left dense (other qtype); "
            "token embedding and tied lm_head are NOT covered",
            mode_name(mode), adopted, adopted_weights, dense_bytes, llq_bytes, outliers,
            base_bits_min, base_bits_max, refused, left_dense);
        for (const auto& r : refusals) {
            s += "\n  refused: " + r;
        }
        return s;
    }
};

namespace detail {

class Observer final : public sensen::GEMM::QuantTransposeObserver {
  public:
    explicit Observer(Mode mode) noexcept : mode_(mode) { report_.mode = mode; }

    auto onTransposed(const void* src_row_major, const void* dense_key, std::size_t n_rows,
                      std::size_t k, sensen::GEMM::QType qtype) noexcept -> void override {
        std::lock_guard lock(mu_);
        if (qtype != sensen::GEMM::QType::Q8_0) {
            ++report_.left_dense;
            return;
        }
        try {
            const std::size_t bytes = n_rows * (k / kQ8BlockValues) * kQ8BlockBytes;
            const auto blocks = std::span(static_cast<const std::uint8_t*>(src_row_major), bytes);
            auto src = LlqQ8Source::from_q8_0_row_major(blocks, n_rows, k, mode_);
            if (!src) {
                ++report_.refused;
                if (report_.refusals.size() < 8) {
                    report_.refusals.push_back(std::format("{}x{}: {}", n_rows, k,
                                                           describe(src.error())));
                }
                return;
            }
            const auto& s = **src;
            ++report_.adopted;
            report_.adopted_weights += static_cast<std::uint64_t>(n_rows) * k;
            report_.dense_bytes += bytes;
            report_.llq_bytes += s.resident_bytes();
            report_.outliers += s.outlier_count();
            const auto [lo, hi] = s.base_bits_range();
            report_.base_bits_min = std::min(report_.base_bits_min, lo);
            report_.base_bits_max = std::max(report_.base_bits_max, hi);
            sensen::GEMM::registerQ8WeightSource(dense_key, *src);
            keys_.emplace_back(dense_key, bytes);
        } catch (...) {
            ++report_.refused;
            if (report_.refusals.size() < 8) {
                report_.refusals.push_back(std::format("{}x{}: exception while encoding", n_rows, k));
            }
        }
    }

    [[nodiscard]] auto snapshot() const -> Report {
        std::lock_guard lock(mu_);
        return report_;
    }

    /// Withdraw every registration this observer made.
    auto withdraw() -> void {
        std::lock_guard lock(mu_);
        for (const auto& [key, bytes] : keys_) {
            (void)bytes;
            sensen::GEMM::unregisterQ8WeightSource(key);
        }
        keys_.clear();
    }

    /// MEASUREMENT ONLY. Return the dense copy of every adopted weight to the OS
    /// (MADV_DONTNEED on the whole pages inside each buffer). Anonymous private
    /// pages read back as zeros afterwards, so any path that still reads a dense
    /// buffer computes from zeros: if generation is unchanged, the tokens came
    /// from the LLQ image and from nothing else. Not wired into the services --
    /// a dense read that this reveals must be fixed, not survived.
    [[nodiscard]] auto release_dense_pages() -> std::size_t {
        std::lock_guard lock(mu_);
        constexpr std::uintptr_t kPage = 4096;
        std::size_t released = 0;
        for (const auto& [key, bytes] : keys_) {
            const auto base = reinterpret_cast<std::uintptr_t>(key);
            const auto lo = (base + kPage - 1) & ~(kPage - 1);
            const auto hi = (base + bytes) & ~(kPage - 1);
            if (hi > lo && madvise(reinterpret_cast<void*>(lo), hi - lo, MADV_DONTNEED) == 0) {
                released += hi - lo;
            }
        }
        return released;
    }

  private:
    Mode mode_;
    mutable std::mutex mu_;
    Report report_;
    std::vector<std::pair<const void*, std::size_t>> keys_;  ///< dense key, dense bytes
};

}  // namespace detail

/// Installs the observer for its lifetime, so every weight transposed while a
/// model loads is offered to LLQ. Destroying the scope stops NEW registrations;
/// weights already adopted stay adopted (the registry owns their sources).
class LoadScope {
  public:
    LoadScope(const LoadScope&) = delete;
    auto operator=(const LoadScope&) -> LoadScope& = delete;
    LoadScope(LoadScope&&) = delete;
    auto operator=(LoadScope&&) -> LoadScope& = delete;

    ~LoadScope() {
        // Only a scope that installed an observer may remove one: a Dense scope
        // destroyed while the other assistant's LLQ scope is mid-load must not
        // uninstall it.
        if (observer_) {
            sensen::GEMM::setQuantTransposeObserver(nullptr);
        }
    }

    /// `Dense` yields an inert scope that installs nothing.
    [[nodiscard]] static auto open(Mode mode) -> std::unique_ptr<LoadScope> {
        return std::unique_ptr<LoadScope>(new LoadScope(mode));
    }

    [[nodiscard]] auto mode() const noexcept -> Mode { return mode_; }

    /// Withdraw everything this scope adopted. Call it when the model it loaded
    /// is being torn down or abandoned: a registered key outliving its buffer can
    /// be reused by an unrelated allocation and then answer for it.
    auto withdraw() -> void {
        if (observer_) {
            observer_->withdraw();
        }
    }

    /// See Observer::release_dense_pages. Measurement only.
    [[nodiscard]] auto release_dense_pages() -> std::size_t {
        return observer_ ? observer_->release_dense_pages() : 0;
    }
    [[nodiscard]] auto report() const -> Report {
        return observer_ ? observer_->snapshot() : Report{};
    }

    /// The verdict after the model has loaded. An operator who asked for LLQ
    /// and got nothing -- or got only some of it -- is told, not served dense.
    [[nodiscard]] auto finish() const -> std::expected<Report, std::string> {
        const Report r = report();
        if (mode_ == Mode::Dense) {
            return r;
        }
        if (r.adopted == 0) {
            return std::unexpected("LLQ was requested but no Q8_0 weight was adopted: this model "
                                   "would be served entirely from the dense path. " + r.summary());
        }
        if (r.refused != 0) {
            return std::unexpected("LLQ was requested but some Q8_0 weights could not be held: "
                                   "serving a mixture would misreport what was measured. " +
                                   r.summary());
        }
        return r;
    }

  private:
    explicit LoadScope(Mode mode) : mode_(mode) {
        if (mode != Mode::Dense) {
            observer_ = std::make_shared<detail::Observer>(mode);
            sensen::GEMM::setQuantTransposeObserver(observer_);
        }
    }

    Mode mode_;
    std::shared_ptr<detail::Observer> observer_;
};

/// Read `env_var` and return its Mode, or a message naming the bad value.
[[nodiscard]] inline auto mode_from_env(std::string_view env_var)
    -> std::expected<Mode, std::string> {
    const std::string name{env_var};
    const char* raw = std::getenv(name.c_str());
    const auto parsed = parse_mode(raw == nullptr ? std::string_view{} : std::string_view{raw});
    if (!parsed) {
        return std::unexpected(std::format(
            "{}=\"{}\" is not a weight store this build knows (expected dense, llq or llq-fused)",
            env_var, raw == nullptr ? "" : raw));
    }
    return *parsed;
}

/// The service-side entry: read the selector variable and open a scope for it.
/// Dense yields an inert scope, so the caller's code path is one path.
[[nodiscard]] inline auto open_scope_from_env(std::string_view env_var)
    -> std::expected<std::unique_ptr<LoadScope>, std::string> {
    const auto mode = mode_from_env(env_var);
    if (!mode) {
        return std::unexpected(mode.error());
    }
    if (*mode != Mode::Dense) {
        const char* fusion = std::getenv("SENSEN_QKV_FUSION");
        if (fusion == nullptr || fusion[0] != '0') {
            return std::unexpected(std::format(
                "{}={} needs SENSEN_QKV_FUSION=0 in the environment before the process starts "
                "(prepare_process_environment does that in main); it is not, so part of every "
                "attention block would keep serving from the dense buffer",
                env_var, mode_name(*mode)));
        }
    }
    return LoadScope::open(*mode);
}

/// Must run before ANY model loads. QKV fusion has to be off when LLQ is
/// selected -- see the header. `SENSEN_QKV_FUSION` is read once into a
/// function-local static, so this is a process-start decision.
[[nodiscard]] inline auto prepare_process_environment(Mode requested)
    -> std::expected<std::monostate, std::string> {
    if (requested == Mode::Dense) {
        return std::monostate{};
    }
    const char* cur = std::getenv("SENSEN_QKV_FUSION");
    if (cur != nullptr && cur[0] != '0') {
        return std::unexpected(
            "an LLQ weight store needs SENSEN_QKV_FUSION=0: with fusion on, Q, K and V are copied "
            "to a buffer no source is registered for and would keep serving from dense");
    }
    if (cur == nullptr && setenv("SENSEN_QKV_FUSION", "0", 1) != 0) {
        return std::unexpected("could not set SENSEN_QKV_FUSION=0");
    }
    return std::monostate{};
}

}  // namespace llq_weight_store
