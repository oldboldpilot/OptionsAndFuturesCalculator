module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <grpcpp/grpcpp.h>

module quota;

import fastjson;
import logger;

namespace options_calculator::quota {

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto env_or(const char* name, std::string fallback) -> std::string {
    const char* raw = std::getenv(name);
    return (raw != nullptr && *raw != '\0') ? std::string{raw} : std::move(fallback);
}

/**
 * A token bucket, refilled continuously rather than in ticks.
 *
 * Continuous refill matters for a public API: a per-minute bucket refilled once
 * a minute lets a caller spend the whole allowance in the first second and then
 * sit idle, which is the burst the limit exists to prevent. Refilling by
 * elapsed time spreads the same allowance smoothly and makes retry-after
 * computable exactly.
 */
struct Bucket {
    double tokens = 0.0;
    double capacity = 0.0;
    double refill_per_second = 0.0;
    Clock::time_point last;

    auto refill(Clock::time_point now) -> void {
        const double dt = std::chrono::duration<double>(now - last).count();
        if (dt > 0.0) {
            tokens = std::min(capacity, tokens + dt * refill_per_second);
            last = now;
        }
    }

    /** Seconds until `needed` tokens exist. Zero when they already do. */
    [[nodiscard]] auto wait_for(double needed) const -> double {
        if (tokens >= needed) return 0.0;
        if (refill_per_second <= 0.0) return 3600.0;  // never refills; back right off
        return (needed - tokens) / refill_per_second;
    }
};

struct CallerState {
    Bucket rate;      // requests
    Bucket budget;    // compute units
    Clock::time_point last_seen;
    std::string tier;
};

}  // namespace

class QuotaEnforcer::Impl {
  public:
    Impl() { load_policy(); }

    bool enabled_ = false;
    std::unordered_map<std::string, TierLimits> tiers_;
    std::unordered_map<std::string, std::string> key_to_tier_;
    std::string anonymous_tier_ = "anonymous";

    mutable std::mutex mu_;
    /** Separate from mu_ on purpose: the tier lookup happens BEFORE mu_ is taken. */
    mutable std::mutex warn_mu_;
    mutable std::unordered_set<std::string> warned_tiers_;
    static constexpr std::size_t kMaxWarnedTiers = 64;
    std::unordered_map<std::string, CallerState> callers_;
    Clock::time_point last_sweep_ = Clock::now();

    [[nodiscard]] auto tier_for(std::string_view api_key) const -> std::string {
        if (api_key.empty()) return anonymous_tier_;
        const auto it = key_to_tier_.find(std::string{api_key});
        // An unrecognised key is NOT an error and NOT a free pass: it is
        // treated as anonymous. Rejecting it would turn the quota system into
        // an authentication system it was never designed to be, and honouring
        // it would let any invented string buy a higher tier.
        return (it == key_to_tier_.end()) ? anonymous_tier_ : it->second;
    }

    [[nodiscard]] auto tier_is_defined(const std::string& tier) const -> bool {
        return tiers_.find(tier) != tiers_.end();
    }

    [[nodiscard]] auto limits_for_tier(const std::string& tier) const -> TierLimits {
        const auto it = tiers_.find(tier);
        if (it != tiers_.end()) return it->second;
        const auto anon = tiers_.find(anonymous_tier_);
        return (anon != tiers_.end()) ? anon->second : TierLimits{};
    }

    /**
     * Says ONCE that a tier arrived which QUOTA_POLICY does not define.
     *
     * `load_policy` already rejects a QUOTA_API_KEYS entry naming an unknown
     * tier, so the `admit` path cannot reach that state. `charge` can: it takes
     * the tier from a VERIFIED identity -- Supabase `app_metadata.tier` or a
     * signed licence -- and neither of those is checked against the policy,
     * because they are issued somewhere else entirely. A tier that is renamed
     * in one place and not the other lands here.
     *
     * Once per distinct name rather than per request: the condition is a
     * misconfiguration that persists, so the hundredth line says nothing the
     * first did not, and under load it would be the only thing in the log. The
     * set is capped because it is keyed on a string this process did not
     * choose; the cap is a bound on memory, not a security control, since both
     * sources of the name are authenticated.
     */
    auto note_undefined_tier(const std::string& tier) const -> void {
        {
            const std::lock_guard<std::mutex> lock(warn_mu_);
            if (warned_tiers_.find(tier) != warned_tiers_.end()) return;
            if (warned_tiers_.size() >= kMaxWarnedTiers) return;
            warned_tiers_.insert(tier);
        }
        logger::Logger::getInstance().error(
            "QUOTA_POLICY does not define tier '{}'; callers presenting it are being metered "
            "against the '{}' allowance. Refusals name it '{} (undefined in QUOTA_POLICY; "
            "anonymous limits)' so this is not mistaken for that tier's own limit.",
            tier, anonymous_tier_, tier);
    }

    /**
     * Policy comes from QUOTA_POLICY (inline JSON) and keys from
     * QUOTA_API_KEYS, both environment variables.
     *
     * Keys are credentials, so they are NOT read from a file in the repository
     * -- config/cpp_details.txt forbids secrets in source, and a committed key
     * is a key that has to be rotated the moment anyone clones. Everything
     * else (tiers, limits) is not sensitive and could equally live in a file;
     * it is in the environment only so both move together.
     *
     * Absent policy leaves quotas OFF and every call untouched, which is the
     * behaviour that existed before this module and the only safe default for
     * a change that can otherwise start refusing real traffic.
     */
    auto load_policy() -> void {
        auto& log = logger::Logger::getInstance();

        const auto policy_json = env_or("QUOTA_POLICY", "");
        if (policy_json.empty()) {
            log.info("Quotas disabled: QUOTA_POLICY is unset");
            return;
        }

        auto result = fastjson::parse(policy_json);
        if (!result) {
            // Loud, and still off. A policy that failed to parse must not read
            // as "no limits configured" three months from now.
            log.error("QUOTA_POLICY is not valid JSON; quotas stay DISABLED");
            return;
        }
        const auto& parsed = *result;
        if (!parsed.is_object()) {
            log.error("QUOTA_POLICY is not a JSON object; quotas stay DISABLED");
            return;
        }

        if (parsed.contains("anonymous_tier") && parsed["anonymous_tier"].is_string()) {
            anonymous_tier_ = std::string{parsed["anonymous_tier"].as_string()};
        }

        if (!parsed.contains("tiers") || !parsed["tiers"].is_object()) {
            log.error("QUOTA_POLICY has no `tiers` object; quotas stay DISABLED");
            return;
        }
        for (const auto& [name, t] : parsed["tiers"].as_object()) {
            if (!t.is_object()) continue;
            TierLimits lim;
            if (t.contains("requests_per_minute") && t["requests_per_minute"].is_number()) {
                lim.requests_per_minute =
                    static_cast<std::int64_t>(t["requests_per_minute"].as_number());
            }
            if (t.contains("compute_units_per_hour") && t["compute_units_per_hour"].is_number()) {
                lim.compute_units_per_hour = t["compute_units_per_hour"].as_number();
            }
            tiers_[name] = lim;
        }
        if (tiers_.empty()) {
            log.error("QUOTA_POLICY defined no usable tiers; quotas stay DISABLED");
            return;
        }
        if (tiers_.find(anonymous_tier_) == tiers_.end()) {
            log.error("QUOTA_POLICY has no tier named '{}' for unkeyed callers; quotas stay "
                      "DISABLED rather than defaulting them to someone else's limits",
                      anonymous_tier_);
            tiers_.clear();
            return;
        }

        // Keys are a separate variable so the policy can be logged and reviewed
        // without exposing them.
        const auto keys_json = env_or("QUOTA_API_KEYS", "");
        if (!keys_json.empty()) {
            auto keys_result = fastjson::parse(keys_json);
            if (keys_result && keys_result->is_object()) {
                for (const auto& [key, tier] : keys_result->as_object()) {
                    if (!tier.is_string()) continue;
                    const std::string tier_name{tier.as_string()};
                    if (tiers_.find(tier_name) == tiers_.end()) {
                        // Named a tier that does not exist. Silently falling
                        // back would give this caller the anonymous limit while
                        // whoever issued the key believes otherwise.
                        log.error("QUOTA_API_KEYS maps a key to unknown tier '{}'; that key will "
                                  "be treated as anonymous",
                                  tier_name);
                        continue;
                    }
                    key_to_tier_[key] = tier_name;
                }
            } else {
                log.error("QUOTA_API_KEYS is not a JSON object; all callers are anonymous");
            }
        }

        enabled_ = true;
        log.info("Quotas ENABLED: {} tiers, {} keys, unkeyed callers get '{}'", tiers_.size(),
                 key_to_tier_.size(), anonymous_tier_);
    }

    /**
     * Charges one call.
     *
     * Both buckets are checked BEFORE either is debited. Charging the rate
     * bucket and then discovering the budget is exhausted would consume an
     * allowance the caller never got to use, so a caller sitting at their
     * compute ceiling would also lose their request allowance -- billed twice
     * for one refusal.
     */
    [[nodiscard]] auto admit(std::string_view api_key, double compute_units) -> Decision {
        // The bucket is keyed on the key only when the key is RECOGNISED.
        //
        // Keying on whatever arrived in the header would make the limit
        // trivially defeatable: a caller sending a fresh random x-api-key on
        // every request would mint a brand-new anonymous bucket each time and
        // never run out. Unrecognised and absent keys therefore share one
        // bucket, so the anonymous allowance is a real ceiling on unidentified
        // traffic rather than a per-string one.
        const bool recognised = !api_key.empty() &&
                                key_to_tier_.find(std::string{api_key}) != key_to_tier_.end();
        const std::string id = recognised ? std::string{api_key} : std::string{"~anonymous"};
        return charge(id, tier_for(api_key), compute_units);
    }

    /**
     * Charges an already-resolved caller.
     *
     * Split out from `admit` so an authenticated call can be metered against
     * its verified identity rather than against the raw header -- the header
     * path stays for the calculator service, which has no authentication.
     *
     * An empty `caller_id` collapses to the shared anonymous bucket for the
     * same reason an unrecognised key does: an unidentified caller must not be
     * able to mint fresh allowance by varying what it sends.
     */
    [[nodiscard]] auto charge(std::string_view caller_id, std::string_view tier_name,
                              double compute_units,
                              const TierLimits* explicit_limits = nullptr) -> Decision {
        Decision d;
        // Limits written on the key are metered whether or not QUOTA_POLICY is
        // set. The enforcer being "off" means no POLICY was configured; it must
        // not also mean that a limit someone deliberately attached to an issued
        // key is discarded. That would be a limit that exists in the operator's
        // notes and nowhere in the running process.
        if (!enabled_ && explicit_limits == nullptr) {
            d.allowed = true;
            return d;
        }
        // A negative or non-finite cost would refund the bucket. Callers price
        // their own calls, so this is a guard against a bug upstream, not
        // against a hostile input.
        if (!std::isfinite(compute_units) || compute_units < 0.0) compute_units = 1.0;

        // A tier the policy does not define falls back to the anonymous limits
        // rather than to no limit -- an entitlement naming a tier that was
        // renamed must not become unlimited access.
        const std::string tier =
            tier_name.empty() ? anonymous_tier_ : std::string{tier_name};
        const auto lim = (explicit_limits != nullptr) ? *explicit_limits : limits_for_tier(tier);
        // Whether the NUMBERS above actually came from this tier. When they did
        // not, the fallback is silent by design (see limits_for_tier) and only
        // the label below can say so.
        const bool from_own_tier = (explicit_limits != nullptr) || tier_is_defined(tier);
        if (!from_own_tier) note_undefined_tier(tier);
        // Reported as the key's own tier name with a marker, so a refusal names
        // something the operator can actually go and look at. Saying "business"
        // when the number came from the key rather than from the business tier
        // would send them to the wrong file.
        //
        // The undefined-tier marker is there for the same reason and is the
        // sharper case: "quota exceeded for tier 'pro'" against the anonymous
        // allowance reads as pro's own limit being hit, and sends an operator
        // to raise a number that is not the one in force.
        d.tier = (explicit_limits != nullptr) ? (tier + " (per-key)")
                 : from_own_tier              ? tier
                                              : (tier + " (undefined in QUOTA_POLICY; anonymous limits)");

        const auto now = Clock::now();
        const std::string id = caller_id.empty() ? std::string{"~anonymous"} : std::string{caller_id};

        const std::lock_guard<std::mutex> lock(mu_);
        sweep_idle(now);

        auto [it, inserted] = callers_.try_emplace(id);
        auto& st = it->second;
        if (inserted) {
            st.tier = tier;
            st.rate.capacity = static_cast<double>(lim.requests_per_minute);
            st.rate.refill_per_second = static_cast<double>(lim.requests_per_minute) / 60.0;
            st.rate.tokens = st.rate.capacity;
            st.rate.last = now;
            st.budget.capacity = lim.compute_units_per_hour;
            st.budget.refill_per_second = lim.compute_units_per_hour / 3600.0;
            st.budget.tokens = st.budget.capacity;
            st.budget.last = now;
        }
        st.last_seen = now;

        const bool rate_limited = lim.requests_per_minute > 0;
        const bool budget_limited = lim.compute_units_per_hour > 0.0;

        if (rate_limited) st.rate.refill(now);
        if (budget_limited) st.budget.refill(now);

        double wait = 0.0;
        if (rate_limited && st.rate.tokens < 1.0) {
            wait = std::max(wait, st.rate.wait_for(1.0));
            d.reason = "request rate";
        }
        if (budget_limited && st.budget.tokens < compute_units) {
            const double w = st.budget.wait_for(compute_units);
            if (w > wait) {
                wait = w;
                d.reason = "compute budget";
            }
            // A single call costing more than a whole hour's allowance can
            // never succeed by waiting. Say so instead of handing out a
            // retry-after that will fail again identically.
            if (compute_units > st.budget.capacity && st.budget.capacity > 0.0) {
                d.reason = "compute budget (this call alone exceeds the tier's hourly allowance)";
                d.retry_after_seconds = 0;
                d.allowed = false;
                return d;
            }
        }

        if (wait > 0.0) {
            d.allowed = false;
            d.retry_after_seconds = static_cast<std::int64_t>(std::ceil(wait));
            return d;
        }

        if (rate_limited) st.rate.tokens -= 1.0;
        if (budget_limited) st.budget.tokens -= compute_units;
        d.allowed = true;
        return d;
    }

    /**
     * Forgets callers idle longer than an hour.
     *
     * Without this the map grows once per distinct key seen, for the life of
     * the process -- an unbounded allocation driven by whatever arrives on a
     * public endpoint. An hour is chosen because both buckets are fully
     * refilled by then, so a forgotten caller returns to exactly the state it
     * would have had anyway; dropping it earlier would silently hand back
     * allowance.
     */
    auto sweep_idle(Clock::time_point now) -> void {
        if (now - last_sweep_ < std::chrono::minutes{5}) return;
        last_sweep_ = now;
        std::erase_if(callers_, [now](const auto& kv) {
            return now - kv.second.last_seen > std::chrono::hours{1};
        });
    }
};

QuotaEnforcer::QuotaEnforcer() : impl_(std::make_unique<Impl>()) {}

// Out-of-line because Impl is incomplete in the module interface.
QuotaEnforcer::~QuotaEnforcer() = default;

auto QuotaEnforcer::instance() -> QuotaEnforcer& {
    static QuotaEnforcer e;
    return e;
}

auto QuotaEnforcer::enabled() const noexcept -> bool { return impl_->enabled_; }

auto QuotaEnforcer::limits_for(std::string_view api_key) const -> TierLimits {
    return impl_->limits_for_tier(impl_->tier_for(api_key));
}

auto QuotaEnforcer::reset_for_test() -> void {
    const std::lock_guard<std::mutex> lock(impl_->mu_);
    impl_->callers_.clear();
}

auto QuotaEnforcer::admit_caller(std::string_view api_key, std::string_view /*method*/,
                                 double compute_units) -> Decision {
    return impl_->admit(api_key, compute_units);
}

namespace {

/**
 * Turns a refusal into the gRPC status a client should act on.
 *
 * RESOURCE_EXHAUSTED, not UNAVAILABLE: the call was understood and refused on
 * policy, and it will keep being refused until the allowance returns.
 * UNAVAILABLE invites the caller's gRPC library to retry immediately, which is
 * precisely the wrong response.
 */
[[nodiscard]] auto to_status(const Decision& d, std::string_view method) -> grpc::Status {
    std::string msg = "quota exceeded for tier '" + d.tier + "' on " + std::string(method) + " (" +
                      d.reason + ")";
    if (d.retry_after_seconds > 0) {
        msg += "; retry in " + std::to_string(d.retry_after_seconds) + "s";
    } else {
        msg += "; this request cannot succeed at this tier regardless of waiting";
    }
    return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, msg);
}

}  // namespace

auto QuotaEnforcer::admit(const grpc::ServerContext& ctx, std::string_view method,
                          double compute_units) -> grpc::Status {
    if (!impl_->enabled_) return grpc::Status::OK;

    std::string api_key;
    const auto& md = ctx.client_metadata();
    // Header names arrive lowercased over HTTP/2, which gRPC preserves.
    if (const auto it = md.find("x-api-key"); it != md.end()) {
        api_key.assign(it->second.data(), it->second.size());
    }

    const auto d = impl_->admit(api_key, compute_units);
    if (d.allowed) return grpc::Status::OK;
    return to_status(d, method);
}

auto QuotaEnforcer::admit_identity(std::string_view caller_id, std::string_view tier,
                                   std::string_view method, double compute_units,
                                   const TierLimits* explicit_limits) -> grpc::Status {
    if (!impl_->enabled_ && explicit_limits == nullptr) return grpc::Status::OK;
    const auto d = impl_->charge(caller_id, tier, compute_units, explicit_limits);
    if (d.allowed) return grpc::Status::OK;
    return to_status(d, method);
}

// ---------------------------------------------------------------------------
// Cost model
//
// One compute unit is roughly a closed-form scalar call (a pmt, a
// Black-Scholes price). Everything else is expressed as a ratio to that, from
// the shape of the work rather than from a benchmark -- a tree is O(steps^2), a
// Monte Carlo is O(paths*steps), a covariance solve is O(n^3). The constants
// only have to make the ORDERS of magnitude right; being wrong by 2x on a call
// that is 1000x another does not change who gets throttled.
// ---------------------------------------------------------------------------

auto cost_default() noexcept -> double { return 1.0; }

auto cost_amortization(int term_months) noexcept -> double {
    // One row per period, each a handful of BigDecimal operations.
    return 1.0 + std::max(0, term_months) / 12.0;
}

auto cost_amortization_batch(int loans, int term_months) noexcept -> double {
    return 1.0 + static_cast<double>(std::max(0, loans)) * (std::max(0, term_months) / 12.0);
}

auto cost_option_tree(int steps, int averaging_states) noexcept -> double {
    // Trinomial tree: O(steps^2) nodes, multiplied by the averaging grid when
    // the option is Asian.
    const double n = std::max(0, steps);
    const double avg = (averaging_states > 0) ? averaging_states : 1;
    return 1.0 + (n * n * avg) / 1000.0;
}

auto cost_monte_carlo(int paths, int steps) noexcept -> double {
    // The dominant cost on this service by a wide margin.
    const double p = std::max(0, paths);
    const double s = std::max(0, steps);
    return 1.0 + (p * s) / 100000.0;
}

auto cost_probability_tree(int steps) noexcept -> double {
    const double n = std::max(0, steps);
    return 1.0 + (n * n) / 1000.0;
}

auto cost_portfolio_optimize(int size) noexcept -> double {
    // LU decomposition of the covariance matrix dominates: O(n^3).
    const double n = std::max(0, size);
    return 1.0 + (n * n * n) / 1000.0;
}

auto cost_portfolio_stats(int samples) noexcept -> double {
    // Linear in the return series, plus a sort for the historical quantiles.
    const double n = std::max(0, samples);
    return 1.0 + (n * std::log2(std::max(2.0, n))) / 1000.0;
}

auto cost_margin_simulation(int days) noexcept -> double {
    return 1.0 + std::max(0, days) / 100.0;
}

auto cost_cash_flow(int entries) noexcept -> double {
    // IRR/XIRR iterate a full NPV evaluation per Newton step.
    return 1.0 + std::max(0, entries) / 10.0;
}

// CalculateStrategy builds a price x date P&L matrix, and every cell reprices
// every leg: the dominant term is O(price_steps * date_steps * legs), the same
// shape as cost_option_tree's O(steps^2) or cost_portfolio_optimize's O(n^3)
// above -- work priced from the request's own arguments, normalised by 1000
// like the other grid- and tree-shaped costs in this file. The two smaller
// passes (the expiry curve, O(price_steps * legs), and the Greeks, O(legs))
// are folded into the same product rather than priced separately, because
// they are dominated by the matrix by at least a factor of date_steps and do
// not change which caller gets throttled.
//
// This is the one grid-shaped cost function in this file whose caller ALSO
// clamps the inputs before the engine ever sees them (see
// calculator_service.cpp's kMaxPriceSteps/kMaxDateSteps) -- unlike
// cost_llm_generate, which deliberately prices an absurd request rather than
// capping it. The two are not in tension: the LLM generation worker is a
// single exclusive thread with no queue behind it, so an absurd price is
// itself the throttle. The strategy matrix runs on the same shared TBB pool
// as every other CPU-priced RPC in this file, and a single caller allocating
// an arbitrarily large response message degrades every other caller's
// latency and the server's own memory long before that caller's own quota
// bucket notices -- so here the clamp does the job the price alone cannot,
// and the price exists to make repeated max-size requests cost what they
// actually cost.
auto cost_strategy_grid(int price_steps, int date_steps, int legs) noexcept -> double {
    const double p = std::max(0, price_steps);
    const double d = std::max(0, date_steps);
    const double l = std::max(1, legs);
    return 1.0 + (p * d * l) / 1000.0;
}

// ---------------------------------------------------------------------------
// Market data cost model
//
// GetMarketQuote, GetRiskFreeRate and GetMarketChain are not priced by CPU --
// they do almost none of it. What they spend is a round trip against an
// external, rate-limited data vendor, and for two of the three that vendor
// connection is the SAME one every caller of this service shares: AlpacaProvider
// (market_data.cppm) holds one keep-alive httplib::Client per (thread, host),
// and Alpaca applies its own upstream rate limit against it independent of
// anything this process's quota tiers say. An unmetered caller here can
// exhaust that shared upstream budget and degrade market data for every other
// caller of this service, including callers who are correctly staying inside
// their own compute-unit allowance -- that is the defect this section closes.
//
// The unit priced below is therefore the upstream HTTP call, not the response
// it returns. That is a deliberate departure from cost_strategy_grid just
// above, and from cost_option_tree/cost_monte_carlo further up: those price
// CPU work whose volume is a genuine function of the request (more steps,
// more paths, literally more arithmetic). A market-data request has no
// equivalent knob. GetMarketChain's eventual strike and expiration counts are
// decided server-side by Alpaca's own filtering, bounded by this codebase's
// own query parameters (limit=1000 on the option-snapshot call, limit=10000
// on the two contracts-registry calls; see market_data.cppm's
// AlpacaProvider::chain and ::expirations) -- a wider result does not cost an
// extra round trip against the shared connection, so pricing by
// strikes-returned or expirations-returned would be a fabricated
// proportionality dressed up as a measured one. What IS real and known before
// the call is dispatched is how many upstream requests the branch about to
// run will issue, so that is what is priced -- the same principle every other
// function in this file follows, just applied to network calls instead of
// arithmetic.
//
// One upstream call is priced at 5x cost_default(). That is enough for the
// charge to read as a materially different KIND of expense than a scalar
// compute op sharing the TBB pool -- which is the point, since the thing
// being protected here (a third party's rate limit) has nothing to do with
// this process's CPU -- while staying far short of what would make ordinary
// UI browsing (a handful of quote lookups per symbol search, one chain fetch
// per expiry click) visibly dent even the free tier's 3,600-unit hourly
// budget: 720 quote-equivalent calls/hour (12/min) on the free tier alone,
// before a single CalculateStrategy call is added to the ledger. The request-
// rate axis (QUOTA_POLICY's requests_per_minute, unaffected by any of this)
// is what caps raw call frequency; the compute-unit axis charged here exists,
// as quota.cppm's own module comment says, to catch a caller staying inside
// that rate but doing disproportionately expensive work -- which for these
// three RPCs means hammering the shared connection repeatedly within the
// hour rather than in one instantaneous burst.
//
// Cache-blind, deliberately. quote_cache and chain_cache in market_data.cppm
// are each a 15-second TTL, so a caller re-polling the exact same symbol (and,
// for a chain, the exact same expiration) pays for upstream work it mostly
// does not do. That is accepted rather than corrected, for two reasons:
//
//   1. The attack this section defends against -- draining the shared Alpaca
//      connection's own rate limit -- is executed by varying the symbol or
//      expiration on every call, specifically because that guarantees a cache
//      miss. Pricing for the miss prices the actual threat shape; a caller
//      who cannot get a cache hit gains nothing by trying.
//   2. Reporting real cache-hit status here would mean threading it out of
//      market_data.cppm's fetch_quote/fetch_chain/fetch_risk_free_rate and
//      charging AFTER the fetch runs. Every admission check in this file runs
//      BEFORE the work it prices -- see CalculateStrategy in
//      calculator_service.cpp pricing from the clamped grid before the SGEE
//      pipeline executes -- specifically so a refusal costs a hash lookup,
//      not the work being refused. Charging after the fetch would mean the
//      one call that drains the last of a caller's budget still always
//      succeeds, and the upstream request it made -- cache hit or not -- has
//      already happened by the time RESOURCE_EXHAUSTED could be returned.
//      That ordering guarantee is worth more than precise pricing of a
//      well-behaved caller's cache hits, and the well-behaved caller pays for
//      it in a small number of over-priced calls against a multi-thousand-
//      unit hourly budget, not in being throttled.
//
// GetMarketQuote and GetRiskFreeRate are one round trip each and are priced
// identically, even though only the former touches the specifically-shared
// Alpaca connection: fetch_risk_free_rate hits home.treasury.gov, a distinct,
// keyless, generously-cached (6h TTL) host with no connection contention from
// this service's other RPCs. It is still an unauthenticated caller's ability
// to make this process issue network requests on demand, and pricing it at a
// different, invented discount from GetMarketQuote would be exactly the kind
// of unmeasured proportionality this section otherwise avoids -- one upstream
// call costs one upstream call.
namespace {
constexpr double kUpstreamCallCost = 5.0;
}  // namespace

auto cost_market_quote() noexcept -> double { return 1.0 + kUpstreamCallCost; }

auto cost_risk_free_rate() noexcept -> double { return 1.0 + kUpstreamCallCost; }

auto cost_market_chain(int upstream_calls) noexcept -> double {
    return 1.0 + static_cast<double>(std::max(0, upstream_calls)) * kUpstreamCallCost;
}

// Every function above prices CPU work that shares the TBB pool with
// everything else running on the engine -- it slows the pool down, but the
// pool keeps serving other callers around it. LLM generation does not share
// anything. The fine-tuned Qwen3-0.6B extraction model runs on a single
// dedicated inference worker thread, decodes at roughly 34 tokens/sec on CPU,
// and measured out at about 1.1 seconds of wall clock and 2.63 GB resident
// (model weights plus KV cache) for one generation. For that entire 1.1
// seconds, that ONE thread is unavailable to every other caller of this RPC,
// full stop -- there is no queueing around it the way there is with a busy
// but shared thread pool. A price that did not reflect that exclusivity would
// let a single caller queue enough LLM calls to freeze the endpoint for
// everyone else behind a single-threaded model, while their own
// compute_units_per_hour ledger looked perfectly reasonable next to a caller
// hammering ComputePayment.
//
// We price the two measured costs separately and add them, because they are
// two independent ways this call is expensive:
//
//   - Wall-clock exclusivity: `max_tokens` bounds how many tokens the decode
//     loop is allowed to produce, and at ~34 tokens/sec that converts directly
//     to the number of seconds the worker thread is held hostage. A second of
//     that exclusive hold is priced at 1,000 units -- three orders of
//     magnitude above cost_default()'s single scalar op -- because unlike
//     every function above, this one denies the pool outright rather than
//     merely adding load to it.
//   - Resident memory: 2.63 GB is charged flatly per generation, not
//     per-token, because it is overwhelmingly the loaded model weights and KV
//     cache rather than anything that grows meaningfully with a short
//     extraction's output. No other cost function in this file carries a
//     memory term, because none of them holds anything close to a rounding
//     error of a gigabyte; this one does, for the whole call.
//
// `samples` multiplies both terms because each requested sample is another
// full generation run sequentially on that same single worker thread --
// asking for ten samples is not ten times the risk of one call, it is
// literally ten times the exclusive hold and ten times the resident-memory
// window. Guarded the same way as the functions above: non-positive inputs
// clamp to zero rather than going negative or dividing by zero, and there is
// deliberately no upper clamp -- a caller asking for an absurd sample count or
// token ceiling should see an absurd price, not a silently capped one, so the
// quota layer above can throttle it honestly.
auto cost_llm_generate(int samples, int max_tokens) noexcept -> double {
    const double n = std::max(0, samples);
    const double tokens = std::max(0, max_tokens);
    const double decode_seconds = tokens / 34.0;
    const double time_units = decode_seconds * 1000.0;
    const double memory_units = 2.63 * 500.0;
    return 1.0 + n * (time_units + memory_units);
}

}  // namespace options_calculator::quota
