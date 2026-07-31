module;
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <grpcpp/grpcpp.h>

export module quota;

export namespace options_calculator::quota {

/**
 * Per-caller quotas for the public gRPC surface.
 *
 * Two limits run together because they answer different questions.
 *
 *   RATE    requests per minute -- catches bursts and runaway retry loops.
 *   BUDGET  compute units per hour -- catches a caller doing expensive work at
 *           a perfectly reasonable request rate.
 *
 * The second exists because a request count is the wrong unit for this service.
 * ComputePayment is a handful of __int128 operations; PriceOptionMonteCarlo at
 * a million paths and a thousand steps is ~10^9 RNG draws across the TBB pool.
 * They differ by six orders of magnitude, so a caller staying comfortably
 * inside a requests-per-minute limit can still saturate the engine. Charging by
 * measured work is what makes the limit mean something.
 *
 * Enforcement is a call-site guard rather than a gRPC interceptor, deliberately.
 * A synchronous C++ server interceptor cannot return a Status to reject an RPC
 * (it would have to hijack the call, which the sync API does not support
 * cleanly), and it cannot see the DESERIALIZED request -- so it could not read
 * `paths`, `steps` or `term_months` to price the call. An explicit line at the
 * top of each RPC is visible, debuggable, and can charge what the work actually
 * costs.
 */

/** What a caller is allowed, per tier. Zero on either axis means unlimited. */
struct TierLimits {
    std::int64_t requests_per_minute = 0;
    double compute_units_per_hour = 0.0;
};

/**
 * The outcome of an admission check.
 *
 * `retry_after_seconds` is a real figure derived from the bucket's own refill
 * rate, not a fixed backoff -- a client that honours it will arrive exactly
 * when it can be served, instead of hammering a closed door.
 */
struct Decision {
    bool allowed = false;
    std::string tier;
    std::int64_t retry_after_seconds = 0;
    std::string reason;
};

/**
 * Loads policy, resolves callers, and meters them.
 *
 * A singleton because the buckets ARE the state: two instances would each let a
 * caller through their own copy of the limit, which is not a limit.
 */
class QuotaEnforcer {
  public:
    [[nodiscard]] static auto instance() -> QuotaEnforcer&;

    /**
     * Charges a call against its caller's quota.
     *
     * `compute_units` is what this specific call costs, priced from its own
     * arguments by the caller (see cost_of_* helpers in the services). Returns a
     * gRPC Status: OK to proceed, RESOURCE_EXHAUSTED with a retry-after when
     * the caller is over.
     */
    [[nodiscard]] auto admit(const grpc::ServerContext& ctx, std::string_view method,
                             double compute_units) -> grpc::Status;

    /** The same check without the gRPC types, for tests and diagnostics. */
    [[nodiscard]] auto admit_caller(std::string_view api_key, std::string_view method,
                                    double compute_units) -> Decision;

    /** Whether quotas are switched on at all. Off leaves every call untouched. */
    [[nodiscard]] auto enabled() const noexcept -> bool;

    /** Resolved limits for a key, for the diagnostics endpoint. */
    [[nodiscard]] auto limits_for(std::string_view api_key) const -> TierLimits;

    /** Drops all metering state. Tests only -- never called in serving. */
    auto reset_for_test() -> void;

    // Owns state that must not be copied into a second enforcer -- two copies
    // would each grant the full allowance.
    QuotaEnforcer(const QuotaEnforcer&) = delete;
    auto operator=(const QuotaEnforcer&) -> QuotaEnforcer& = delete;
    QuotaEnforcer(QuotaEnforcer&&) = delete;
    auto operator=(QuotaEnforcer&&) -> QuotaEnforcer& = delete;
    ~QuotaEnforcer();

  private:
    QuotaEnforcer();
    class Impl;
    // Declared here, defined in the implementation unit. The destructor is
    // out-of-line for the same reason: Impl is incomplete at this point, and
    // unique_ptr needs a complete type to destroy it.
    std::unique_ptr<Impl> impl_;
};

/**
 * Prices a call from its own arguments.
 *
 * Kept here rather than in the services so every RPC's cost is stated in one
 * place -- a new expensive RPC that forgets to price itself is then a visible
 * omission in this file, not an invisible free ride.
 *
 * The weights are ratios of real work, normalised so a trivial call is 1.
 */
[[nodiscard]] auto cost_default() noexcept -> double;
[[nodiscard]] auto cost_amortization(int term_months) noexcept -> double;
[[nodiscard]] auto cost_amortization_batch(int loans, int term_months) noexcept -> double;
[[nodiscard]] auto cost_option_tree(int steps, int averaging_states) noexcept -> double;
[[nodiscard]] auto cost_monte_carlo(int paths, int steps) noexcept -> double;
[[nodiscard]] auto cost_probability_tree(int steps) noexcept -> double;
[[nodiscard]] auto cost_portfolio_optimize(int size) noexcept -> double;
[[nodiscard]] auto cost_portfolio_stats(int samples) noexcept -> double;
[[nodiscard]] auto cost_margin_simulation(int days) noexcept -> double;
[[nodiscard]] auto cost_cash_flow(int entries) noexcept -> double;

}  // namespace options_calculator::quota
