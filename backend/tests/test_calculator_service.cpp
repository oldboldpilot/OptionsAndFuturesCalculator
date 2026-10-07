// @author Olumuyiwa Oluwasanmi
//
// Real, in-process gRPC gate for CalculateStrategy -- the RPC EVERY strategy
// calculation on optionsandfuturescalculator.com goes through, via SGEE's
// graph interpreter. Before this file, that path had ZERO automated test
// coverage: the file it replaces (see git history) targeted an API that no
// longer exists (ComputeStrategyPnL, CalculationRequest), asserted a
// fabricated probability-of-profit of exactly 0.65, and was never even wired
// into the CMake build -- its presence implied coverage that did not exist.
//
// Same shape as tests/test_option_pricing_service.cpp and
// tests/test_finance_service_validation.cpp: a plain hand-rolled
// check()/section() harness (sensen coding policy, config/cpp_details.txt
// rule 39, forbids external test frameworks), a real grpc::Server on an
// OS-assigned loopback port hosting exactly what
// options_calculator::service::RegisterCalculatorService registers in
// production, and a real OptionsCalculator::Stub -- not a direct call into
// calculator_service.cpp's internals.
//
// WHAT THIS FILE PROVES, SECTION BY SECTION:
//
//   1. CLOSED-FORM IDENTITY: a 580/600 bull call spread at a 7.25 debit
//      prices to maxProfit 1275, maxLoss -725, breakEven 587.25 -- the exact
//      figures CLAUDE.md already cites as the production Pro-gate identity
//      ("closed-form answer for a 7.25 debit"), so this is independently
//      derivable, not a number the engine handed itself.
//   2. IRON CONDOR: a short iron condor prices to exactly TWO breakevens
//      (one per short strike) and non-zero net gamma/vega -- the properties
//      that distinguish a real multi-leg risk computation from a stub.
//   3. DID-COMPUTE, end-to-end over the wire: pnl_matrix and matrix have
//      EXACTLY the requested dimensions, not zero and not some other count.
//   4. THE DISCRIMINATING PROOF (the highest-value section): a server built
//      with only 4 of the graph's 5 actions registered -- reproducing,
//      through the REAL RPC, the exact silent-halt failure mode documented
//      in calculator_service.cpp's postcondition comment (an action id with
//      nothing bound to it routes into interpreter.cppm's silent-halt
//      branch, exactly like a null registry or an action that fails without
//      setting ctx->status). Before the postcondition existed, THIS SECTION
//      FAILED: the RPC returned Status::OK with net_greeks all zero. See the
//      task's own before/after log for the recorded run of both states; this
//      file only asserts the fixed (passing) behaviour, which is what a
//      permanent regression gate should do.
//
//   8. THE POSITION'S EXTREMES, NOT THE GRID'S: max_profit and max_loss are
//      the supremum and infimum over every price in (0, infinity), and a
//      direction with no finite extreme says so in max_profit_unbounded /
//      max_loss_unbounded instead of reporting the edge of the +/-25% grid as a
//      maximum. Every expected figure is the closed form of a payoff written
//      out by hand beside it, not a number the engine produced.
//   9. FUTURES NEED NO VOLATILITY: a position of linear legs prices, flags and
//      draws its curve with no implied volatility anywhere, and the probability
//      fields stay at zero without an error or a NaN.
//  10. ONE TODAY: the dates the chain serves and the dates the matrix is drawn
//      on come from the same calendar, so a cache that outlives a midnight
//      cannot put the last matrix column a day past the chosen expiry.
//
// Deliberately scoped to calculator_service.cpp's request handling and
// response marshalling around SGEE, exactly as the two finance-service test
// files are scoped to finance_service.cpp around sensen's pricers/solvers.
// backend/sensen/** and backend/external/SGEE/** are untouched and read-only
// for this file.
#include <cstdio>



#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

import std;
import calculator_service;
import market_data;
import strategy_store;

namespace {

// ---------------------------------------------------------------------------
// Harness (mirrors tests/test_option_pricing_service.cpp /
// tests/test_mortgage_verification.cpp).
// ---------------------------------------------------------------------------

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

// ---------------------------------------------------------------------------
// In-process server + stub, over the REAL RegisterCalculatorService --
// exactly what main.cpp registers in production, on an ephemeral port
// instead of the configured one.
//
// No API key and no PRO_GATE_MODE are set in this process's environment, so
// check_strategy_entitlement's gate is Off (api_key.cpp: pro_gate_mode()
// falls back to Off when PRO_GATE_MODE is unset) -- every multi-leg request
// below runs BELOW the Pro gate, not through it. A test failing with gRPC
// code 7 (PERMISSION_DENIED) would mean the ambient environment set that
// variable, not a defect in this file.
// ---------------------------------------------------------------------------

using calculator::Leg;
using calculator::OptionsCalculator;
using calculator::StrategyRequest;
using calculator::StrategyResponse;

struct ServiceFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<OptionsCalculator::Stub> stub;

    ServiceFixture() {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::service::RegisterCalculatorService(builder);
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr,
                         "FATAL: could not start the in-process calculator service on an "
                         "OS-assigned loopback port\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = OptionsCalculator::NewStub(channel);
    }

    ~ServiceFixture() {
        if (server) server->Shutdown();
    }
};

/**
 * A fixture built on RegisterCalculatorServiceForTest instead, binding only
 * the action names listed at construction. Exists solely for section 4 --
 * see that hook's own doc comment in calculator_service.cppm for why
 * production has no path that can construct one of these with a partial set.
 */
struct PartialActionsFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<OptionsCalculator::Stub> stub;

    explicit PartialActionsFixture(std::span<const std::string_view> bound_action_names) {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::service::RegisterCalculatorServiceForTest(builder, bound_action_names);
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr,
                         "FATAL: could not start the in-process calculator service (partial "
                         "action set) on an OS-assigned loopback port\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = OptionsCalculator::NewStub(channel);
    }

    ~PartialActionsFixture() {
        if (server) server->Shutdown();
    }
};

auto make_context(std::chrono::seconds deadline = std::chrono::seconds{30})
    -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + deadline);
    return ctx;
}

auto add_leg(StrategyRequest& req, Leg::Action action, Leg::Type type, double strike,
             double expiration_days, double premium, double iv, int qty = 1,
             double multiplier = 100.0) -> void {
    auto* leg = req.add_legs();
    leg->set_action(action);
    leg->set_type(type);
    leg->set_strike(strike);
    leg->set_expiration_days(expiration_days);
    leg->set_quantity(qty);
    leg->set_premium(premium);
    leg->set_implied_volatility(iv);
    leg->set_contract_multiplier(multiplier);
}

auto call_strategy(OptionsCalculator::Stub& stub, const StrategyRequest& req)
    -> std::pair<grpc::Status, StrategyResponse> {
    StrategyResponse resp;
    const auto ctx = make_context();
    const auto status = stub.CalculateStrategy(ctx.get(), req, &resp);
    return {status, resp};
}

/** A request with nothing in it but the underlying and the grid, for sections 8-10. */
auto make_request(double spot, double price_range_percent = 0.25, std::uint32_t date_steps = 12)
    -> StrategyRequest {
    StrategyRequest req;
    req.set_underlying_symbol("SPY");
    req.set_current_price(spot);
    req.set_risk_free_rate(0.04);
    req.set_price_range_percent(price_range_percent);
    req.set_price_steps(81);
    req.set_date_steps(date_steps);
    return req;
}

/** A future or stock leg: no strike, no volatility, the entry carried in `premium`. */
auto add_linear_leg(StrategyRequest& req, Leg::Action action, Leg::Type type, double entry,
                    double expiration_days, double multiplier, int qty = 1) -> void {
    auto* leg = req.add_legs();
    leg->set_action(action);
    leg->set_type(type);
    leg->set_expiration_days(expiration_days);
    leg->set_quantity(qty);
    leg->set_premium(entry);
    leg->set_contract_multiplier(multiplier);
}

/** Every number a response carries, finite: a NaN rendered as a price is the failure to rule out. */
auto all_finite(const StrategyResponse& r) -> bool {
    bool ok = std::isfinite(r.max_profit()) && std::isfinite(r.max_loss()) &&
              std::isfinite(r.break_even()) && std::isfinite(r.expected_value()) &&
              std::isfinite(r.pop()) && std::isfinite(r.risk_reward_ratio()) &&
              std::isfinite(r.probability_of_touch()) &&
              std::isfinite(r.probability_of_target_profit());
    for (const auto& p : r.pnl_matrix()) ok = ok && std::isfinite(p.pnl());
    for (const auto& c : r.matrix()) ok = ok && std::isfinite(c.pnl_dollars());
    const auto& g = r.net_greeks();
    ok = ok && std::isfinite(g.delta()) && std::isfinite(g.gamma()) && std::isfinite(g.theta()) &&
         std::isfinite(g.vega()) && std::isfinite(g.rho());
    const auto& m = r.risk_metrics();
    return ok && std::isfinite(m.var_parametric_95()) && std::isfinite(m.var_parametric_99()) &&
           std::isfinite(m.cvar_parametric_95()) && std::isfinite(m.cvar_parametric_99());
}

/** The extremes of the CURVE a response carries: what the grid alone would have reported. */
auto curve_extremes(const StrategyResponse& r) -> std::pair<double, double> {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const auto& p : r.pnl_matrix()) {
        lo = std::min(lo, p.pnl());
        hi = std::max(hi, p.pnl());
    }
    return {lo, hi};
}

auto near(double a, double b, double eps = 1e-6) -> bool { return std::abs(a - b) < eps; }

// ---------------------------------------------------------------------------
// Section 10: a market-data provider and a calendar the test controls.
// ---------------------------------------------------------------------------

namespace md = options_calculator::market_data;
using calculator::ChainRequest;
using calculator::ChainResponse;

/** One symbol at 779, two listed expirations, and a count of how often it was asked. */
class FakeChainProvider final : public md::IMarketDataProvider {
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "fake_calendar"; }

    [[nodiscard]] auto quote(const std::string& symbol) const
        -> std::expected<md::Quote, std::error_code> override {
        return md::Quote{.symbol = symbol,
                         .price = 779.0,
                         .previous_close = 778.0,
                         .timestamp = "2035-10-05T14:00:00Z"};
    }

    [[nodiscard]] auto expirations(const std::string&, double) const
        -> std::expected<std::vector<md::Expiration>, std::error_code> override {
        ++expirations_calls_;
        return std::vector<md::Expiration>{md::Expiration{.date = "2035-10-12"},
                                           md::Expiration{.date = "2035-10-19"}};
    }

    [[nodiscard]] auto chain(const std::string& symbol, const std::string& expiration,
                             double spot) const -> std::expected<md::Chain, std::error_code> override {
        ++chain_calls_;
        md::Chain c;
        c.symbol = symbol;
        c.spot = spot;
        c.selected_expiration = expiration;
        c.strikes.push_back(md::StrikeRow{.strike = 779.0});
        return c;
    }

    [[nodiscard]] auto chain_calls() const noexcept -> int { return chain_calls_.load(); }
    [[nodiscard]] auto expirations_calls() const noexcept -> int { return expirations_calls_.load(); }

private:
    mutable std::atomic<int> chain_calls_{0};
    mutable std::atomic<int> expirations_calls_{0};
};

/** A flat 4% curve, so the futures term structure has the one measured input it refuses to guess. */
class FakeRateProvider final : public md::IRateProvider {
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "fake_rate"; }
    [[nodiscard]] auto risk_free_rate() const
        -> std::expected<md::RiskFreeRate, std::error_code> override {
        return md::RiskFreeRate{.rate = 0.04,
                                .rate_published = 0.0392,
                                .tenor = "3M",
                                .as_of_date = "2026-10-05",
                                .source = "fake",
                                .fetched_at = "2026-10-06T00:00:00Z"};
    }
};

std::chrono::sys_days g_fake_today{};
auto fake_today() -> std::chrono::sys_days { return g_fake_today; }

auto get_chain(OptionsCalculator::Stub& stub, const std::string& symbol,
               const std::string& asset_class, const std::string& expiration_date)
    -> std::pair<grpc::Status, ChainResponse> {
    ChainRequest req;
    req.set_symbol(symbol);
    req.set_asset_class(asset_class);
    req.set_expiration_date(expiration_date);
    ChainResponse resp;
    const auto ctx = make_context();
    const auto status = stub.GetMarketChain(ctx.get(), req, &resp);
    return {status, resp};
}


// ---------------------------------------------------------------------------
// Saved-scenario support: an in-memory store and a real signed JWT
// ---------------------------------------------------------------------------

namespace store = options_calculator::store;

/**
 * An in-memory IStrategyStore that reproduces the Postgres implementation's
 * OBSERVABLE contract -- per-user scoping, upsert-by-name, the per-user cap,
 * newest-first ordering -- and nothing else.
 *
 * The point is to test the RPC layer's own behaviour (auth, entitlement,
 * validation, JSON round-trip, error mapping) without a database. It is NOT a
 * substitute for exercising the real SQL: that is what
 * tests/test_strategy_store_pg.cpp does, against a live Postgres.
 */
class FakeStore final : public store::IStrategyStore {
  public:
    [[nodiscard]] auto save(std::string_view subject, std::string_view name,
                            std::string_view symbol, std::string_view payload_json)
        -> std::expected<store::SaveOutcome, store::StoreError> override {
        if (subject.empty() || name.empty()) return std::unexpected(store::StoreError::Invalid);
        auto& rows = by_user_[std::string{subject}];

        const auto it = std::find_if(rows.begin(), rows.end(),
                                     [&](const store::SavedRow& r) { return r.name == name; });
        if (it != rows.end()) {
            it->symbol = std::string{symbol};
            it->payload_json = std::string{payload_json};
            it->updated_at = stamp(++clock_);
            // Newest-first: move the touched row to the front, mirroring the
            // real store's ORDER BY updated_at DESC.
            store::SavedRow moved = *it;
            rows.erase(it);
            rows.insert(rows.begin(), moved);
            return store::SaveOutcome{.row = moved, .replaced_existing = true};
        }

        if (rows.size() >= store::kMaxPerUser) {
            return std::unexpected(store::StoreError::AtCapacity);
        }
        store::SavedRow row{
            .id = std::format("00000000-0000-4000-8000-{:012}", ++id_seq_),
            .name = std::string{name},
            .symbol = std::string{symbol},
            .payload_json = std::string{payload_json},
            .created_at = stamp(++clock_),
            .updated_at = stamp(clock_),
        };
        rows.insert(rows.begin(), row);
        return store::SaveOutcome{.row = row, .replaced_existing = false};
    }

    [[nodiscard]] auto list(std::string_view subject)
        -> std::expected<std::vector<store::SavedRow>, store::StoreError> override {
        if (subject.empty()) return std::unexpected(store::StoreError::Invalid);
        const auto it = by_user_.find(std::string{subject});
        if (it == by_user_.end()) return std::vector<store::SavedRow>{};
        return it->second;
    }

    [[nodiscard]] auto remove(std::string_view subject, std::string_view id)
        -> std::expected<bool, store::StoreError> override {
        if (subject.empty()) return std::unexpected(store::StoreError::Invalid);
        const auto it = by_user_.find(std::string{subject});
        if (it == by_user_.end()) return false;
        auto& rows = it->second;
        const auto row = std::find_if(rows.begin(), rows.end(),
                                      [&](const store::SavedRow& r) { return r.id == id; });
        if (row == rows.end()) return false;
        rows.erase(row);
        return true;
    }

    /** Direct read, for assertions that must not go through the RPC. */
    [[nodiscard]] auto count_for(std::string_view subject) const -> std::size_t {
        const auto it = by_user_.find(std::string{subject});
        return it == by_user_.end() ? 0U : it->second.size();
    }

  private:
    [[nodiscard]] static auto stamp(int tick) -> std::string {
        return std::format("2026-01-01T00:00:{:02}Z", tick % 60);
    }

    std::map<std::string, std::vector<store::SavedRow>> by_user_;
    int id_seq_ = 0;
    int clock_ = 0;
};

/** Unpadded base64url -- must match api_key.cpp's own b64url_encode exactly. */
auto b64url(std::string_view raw) -> std::string {
    static constexpr std::string_view kA =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < raw.size(); i += 3) {
        const std::uint32_t c = (static_cast<unsigned char>(raw[i]) << 16) |
                                (static_cast<unsigned char>(raw[i + 1]) << 8) |
                                static_cast<unsigned char>(raw[i + 2]);
        out.push_back(kA[(c >> 18) & 0x3F]);
        out.push_back(kA[(c >> 12) & 0x3F]);
        out.push_back(kA[(c >> 6) & 0x3F]);
        out.push_back(kA[c & 0x3F]);
    }
    if (i + 1 == raw.size()) {
        const std::uint32_t c = static_cast<unsigned char>(raw[i]) << 16;
        out.push_back(kA[(c >> 18) & 0x3F]);
        out.push_back(kA[(c >> 12) & 0x3F]);
    } else if (i + 2 == raw.size()) {
        const std::uint32_t c = (static_cast<unsigned char>(raw[i]) << 16) |
                                (static_cast<unsigned char>(raw[i + 1]) << 8);
        out.push_back(kA[(c >> 18) & 0x3F]);
        out.push_back(kA[(c >> 12) & 0x3F]);
        out.push_back(kA[(c >> 6) & 0x3F]);
    }
    return out;
}

constexpr std::string_view kJwtSecret = "test-only-jwt-secret-not-a-real-one";

/**
 * Mints a REAL HS256 token that api_key.cpp's verify_supabase_jwt actually
 * verifies -- not a stub identity injected past the auth layer.
 *
 * That distinction is the whole value of these sections: the property under
 * test is "a verified subject scopes storage", and asserting it against a
 * hand-built Identity would prove nothing about the code path a browser takes.
 */
auto mint_jwt(std::string_view sub, std::string_view tier) -> std::string {
    const auto exp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count() +
                     3600;
    const std::string header = R"({"alg":"HS256","typ":"JWT"})";
    const std::string payload =
        std::format(R"({{"sub":"{}","exp":{},"app_metadata":{{"tier":"{}"}}}})", sub, exp, tier);
    const std::string signing_input = b64url(header) + "." + b64url(payload);

    std::array<unsigned char, EVP_MAX_MD_SIZE> mac{};
    unsigned int mac_len = 0;
    HMAC(EVP_sha256(), kJwtSecret.data(), static_cast<int>(kJwtSecret.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size(),
         mac.data(), &mac_len);
    return signing_input + "." +
           b64url(std::string_view{reinterpret_cast<const char*>(mac.data()), mac_len});
}

/** A fixture whose service is backed by a caller-supplied store. */
struct SavedFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<OptionsCalculator::Stub> stub;

    explicit SavedFixture(std::shared_ptr<store::IStrategyStore> injected) {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::service::RegisterCalculatorServiceForTest(
            builder, kAllActions, std::move(injected));
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr, "FATAL: could not start the saved-scenario fixture\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = OptionsCalculator::NewStub(channel);
    }

    ~SavedFixture() {
        if (server) server->Shutdown();
    }

    static constexpr std::array<std::string_view, 5> kAllActions{
        "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeGreeks",
        "ComputeProbabilities"};
};

/** A context carrying a bearer token, the way the browser sends one. */
auto auth_context(std::string_view jwt) -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = make_context();
    if (!jwt.empty()) ctx->AddMetadata("authorization", std::string{"Bearer "} + std::string{jwt});
    return ctx;
}

/** A minimal but valid two-leg scenario to save. */
auto sample_scenario() -> StrategyRequest {
    StrategyRequest req;
    req.set_underlying_symbol("SPY");
    req.set_current_price(585.0);
    req.set_implied_volatility(0.18);
    req.set_risk_free_rate(0.043);
    add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.50, 0.19);
    add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 5.25, 0.17);
    return req;
}

}  // namespace

auto main() -> int {
    ServiceFixture fixture;
    OptionsCalculator::Stub& stub = *fixture.stub;

    // =======================================================================
    section("1. CLOSED-FORM IDENTITY: 580/600 bull call spread @ 7.25 debit");
    // =======================================================================
    // CLAUDE.md's own production-verification identity for the Pro gate:
    // "maxProfit 1275, maxLoss -725, breakEven 587.25 on a 580/600 bull call
    // spread ... the closed-form answer for a 7.25 debit." Both legs share
    // the same expiry, so the payoff curve (drawn at that shared expiry,
    // curve_days_to_expiration) is the plain piecewise-linear at-expiration
    // diagram: flat at -725 below 580, flat at 1275 above 600, linear
    // between -- exact identities, not approximations, regardless of the
    // price grid's resolution (the code's own linear-interpolation breakeven
    // formula is exact for a linear function between any two bracketing grid
    // points).
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);

        auto [status, resp] = call_strategy(stub, req);
        check(status.ok(), "580/600 bull call spread: RPC succeeds (got " +
                                (status.ok() ? std::string{"OK"}
                                            : std::to_string(static_cast<int>(status.error_code())) +
                                                  ": " + status.error_message()) +
                                ")");

        constexpr double kEps = 1e-6;
        check(std::abs(resp.max_profit() - 1275.0) < kEps,
              "maxProfit == 1275 (got " + std::to_string(resp.max_profit()) + ")");
        check(std::abs(resp.max_loss() - (-725.0)) < kEps,
              "maxLoss == -725 (got " + std::to_string(resp.max_loss()) + ")");
        check(std::abs(resp.break_even() - 587.25) < kEps,
              "breakEven == 587.25 (got " + std::to_string(resp.break_even()) + ")");
        check(resp.breakeven_prices_size() == 1,
              "exactly one breakeven for a two-leg vertical spread (got " +
                  std::to_string(resp.breakeven_prices_size()) + ")");
        check(std::abs(resp.risk_reward_ratio() - (1275.0 / 725.0)) < 1e-4,
              "riskRewardRatio == maxProfit/|maxLoss| (got " +
                  std::to_string(resp.risk_reward_ratio()) + ")");
    }

    // =======================================================================
    section("2. IRON CONDOR: two breakevens, non-zero net Greeks");
    // =======================================================================
    // Short iron condor: buy 560 put / sell 570 put (put spread, net credit
    // 1.50), sell 610 call / buy 620 call (call spread, net credit 1.50).
    // Total credit 3.00 -> put-side breakeven 570 - 3.00 = 567, call-side
    // breakeven 610 + 3.00 = 613: two DISTINCT breakevens, one per short
    // strike, which is exactly the property a single scalar `break_even`
    // field cannot represent (see calculator_service.cpp's own comment on
    // breakeven_prices). Short gamma and short vega are structural for any
    // condor with time value left, so both are asserted non-zero rather than
    // delta, which can land near zero for strikes symmetric around spot.
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::PUT, 560.0, 30.0, 2.00, 0.22);
        add_leg(req, Leg::SELL, Leg::PUT, 570.0, 30.0, 3.50, 0.22);
        add_leg(req, Leg::SELL, Leg::CALL, 610.0, 30.0, 3.25, 0.20);
        add_leg(req, Leg::BUY, Leg::CALL, 620.0, 30.0, 1.75, 0.20);

        auto [status, resp] = call_strategy(stub, req);
        check(status.ok(), "iron condor: RPC succeeds (got " +
                                (status.ok() ? std::string{"OK"}
                                            : std::to_string(static_cast<int>(status.error_code())) +
                                                  ": " + status.error_message()) +
                                ")");

        check(resp.breakeven_prices_size() == 2,
              "iron condor has exactly TWO breakevens (got " +
                  std::to_string(resp.breakeven_prices_size()) + ")");
        if (resp.breakeven_prices_size() == 2) {
            const double lo = std::min(resp.breakeven_prices(0), resp.breakeven_prices(1));
            const double hi = std::max(resp.breakeven_prices(0), resp.breakeven_prices(1));
            check(std::abs(lo - 567.0) < 1e-6,
                  "lower breakeven == 567 (put strike 570 - 3.00 credit; got " +
                      std::to_string(lo) + ")");
            check(std::abs(hi - 613.0) < 1e-6,
                  "upper breakeven == 613 (call strike 610 + 3.00 credit; got " +
                      std::to_string(hi) + ")");
        }

        check(resp.has_net_greeks(), "response carries net_greeks");
        check(std::abs(resp.net_greeks().gamma()) > 1e-6,
              "net gamma is non-zero (got " + std::to_string(resp.net_greeks().gamma()) + ")");
        check(std::abs(resp.net_greeks().vega()) > 1e-6,
              "net vega is non-zero (got " + std::to_string(resp.net_greeks().vega()) + ")");
        check(resp.net_greeks().gamma() < 0.0,
              "a SHORT iron condor is short gamma (negative; got " +
                  std::to_string(resp.net_greeks().gamma()) + ")");
        check(resp.net_greeks().vega() < 0.0,
              "a SHORT iron condor is short vega (negative; got " +
                  std::to_string(resp.net_greeks().vega()) + ")");
        check(resp.leg_risk_size() == 4,
              "leg_risk has one entry per leg (got " + std::to_string(resp.leg_risk_size()) + ")");
    }

    // =======================================================================
    section("3. DID-COMPUTE, end-to-end: pnl_matrix and matrix have the "
            "REQUESTED dimensions");
    // =======================================================================
    // The wire-level twin of calculator_service.cpp's own postcondition:
    // asking for a specific price_steps/date_steps grid must produce a
    // response with exactly that many points, not zero and not some other
    // count. A single-leg call needs no Pro credential even with the gate
    // enforced (check_strategy_entitlement admits leg_count <= 1
    // unconditionally), so this section is a control that holds regardless
    // of the ambient PRO_GATE_MODE.
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(500.0);
        req.set_risk_free_rate(0.04);
        req.set_price_range_percent(0.20);
        req.set_price_steps(50);
        req.set_date_steps(5);
        add_leg(req, Leg::BUY, Leg::CALL, 500.0, 45.0, 15.0, 0.25);

        auto [status, resp] = call_strategy(stub, req);
        check(status.ok(), "single-leg call: RPC succeeds");
        check(resp.pnl_matrix_size() == 50,
              "pnl_matrix has exactly the requested 50 points (got " +
                  std::to_string(resp.pnl_matrix_size()) + ")");
        check(resp.matrix_size() == 50 * 5,
              "matrix has exactly the requested 50 x 5 = 250 cells (got " +
                  std::to_string(resp.matrix_size()) + ")");
    }

    // =======================================================================
    section("4. THE DISCRIMINATING PROOF: a server missing one of the "
            "graph's five actions must NOT return OK");
    // =======================================================================
    // Reproduces, through the REAL RPC, the exact silent-halt failure mode
    // calculator_service.cpp's postcondition comment documents: "if an
    // action fails WITHOUT setting ctx->status, the entity halts silently
    // ... e.g. a P&L curve with no Greeks". ComputeGreeks is left unbound
    // (only Initialize, ComputeExpiryCurve, ComputeMatrix, and
    // ComputeProbabilities are registered) -- the graph builds and runs
    // Initialize/ComputeExpiryCurve/ComputeMatrix normally, then reaches the
    // ComputeGreeks node, finds no action bound to its id, and
    // ActionRegistry::Execute returns ExecutionError::ActionFailed with
    // ctx->status untouched. interpreter.cppm's node loop then halts the
    // entity at ComputeGreeks -- it never reaches ComputeProbabilities or
    // Done.
    //
    // BEFORE calculator_service.cpp's postcondition existed, this section
    // FAILED: the RPC returned Status::OK, with pnl_matrix and matrix fully
    // populated (those actions did run) but net_greeks all zero and no
    // probability/risk fields (those actions never ran) -- a partially
    // filled response with no error of any kind, indistinguishable from a
    // real answer to a client. Recorded from the actual pre-fix binary: this
    // exact request returned status.ok()=true. The postcondition's
    // TERMINAL-STATE check (the entity is parked at ComputeGreeks, not Done)
    // now catches it and turns it into INTERNAL.
    {
        constexpr std::array<std::string_view, 4> kMissingGreeks{
            "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeProbabilities"};
        PartialActionsFixture broken_fixture(kMissingGreeks);
        OptionsCalculator::Stub& broken_stub = *broken_fixture.stub;

        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);

        auto [status, resp] = call_strategy(broken_stub, req);
        check(!status.ok(),
              "a graph missing the ComputeGreeks action is REFUSED, not answered with a "
              "partial response (got " +
                  (status.ok() ? std::string{"OK -- THE BUG: pnl_matrix had "} +
                                     std::to_string(resp.pnl_matrix_size()) +
                                     " points but net_greeks.gamma()=" +
                                     std::to_string(resp.net_greeks().gamma()) + " (should be "
                                     "non-zero, per section 2)"
                               : std::to_string(static_cast<int>(status.error_code())) + ": " +
                                     status.error_message()) +
                  ")");
        check(status.error_code() == grpc::StatusCode::INTERNAL,
              "...specifically INTERNAL (a server-side defect), not some other code (got " +
                  std::to_string(static_cast<int>(status.error_code())) + ")");
        check(status.error_message().find("Done") != std::string::npos ||
                  status.error_message().find("ComputeGreeks") != std::string::npos,
              "...and the message names where the graph actually stalled, not a generic "
              "failure: \"" +
                  status.error_message() + "\"");
    }

    // A control alongside section 4: the FULL action set, run through the
    // SAME PartialActionsFixture machinery (not ServiceFixture), still
    // succeeds -- so section 4's failure is specifically about the missing
    // action, not an artifact of using RegisterCalculatorServiceForTest at
    // all.
    {
        constexpr std::array<std::string_view, 5> kAllFive{
            "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeGreeks",
            "ComputeProbabilities"};
        PartialActionsFixture control_fixture(kAllFive);
        OptionsCalculator::Stub& control_stub = *control_fixture.stub;

        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);

        auto [status, resp] = call_strategy(control_stub, req);
        check(status.ok(),
              "CONTROL: the same fixture machinery with all FIVE actions bound still "
              "succeeds (proves section 4's failure is the missing action, not the test "
              "harness)");
        check(std::abs(resp.max_profit() - 1275.0) < 1e-6,
              "...and prices to the same maxProfit == 1275 as section 1");
    }

    // =======================================================================
    section("6. ASIAN LEGS: refused by CODE, and only when actually Asian");
    // =======================================================================
    // Every figure this service returns is a function of TERMINAL SPOT. An
    // average-price Asian pays on the realized AVERAGE, which is a different
    // random variable -- Var(A) < Var(S_T) for the same sigma. Walking an
    // Asian leg across the spot grid does not approximate it; it prices a
    // vanilla and calls it Asian.
    //
    // The refusal is whole-response and carries its own status code because
    // proto3 has no absent: an unset max_profit is indistinguishable from a
    // real 0.0, so a partial answer would render "max profit $0".
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        req.mutable_legs(0)->set_asian_type(Leg::AVERAGE_PRICE);

        auto [status, resp] = call_strategy(stub, req);
        check(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
              "an AVERAGE_PRICE leg is refused with FAILED_PRECONDITION -- a modelling "
              "limit, not malformed input, and discriminated by CODE not text");
        // Deliberately NOT asserting that `resp` is empty here. gRPC does not
        // deliver a message body alongside a non-OK status, so `resp` is a
        // default-constructed StrategyResponse whatever the engine did --
        // `pnl_matrix_size() == 0 && max_profit() == 0.0` is a property of the
        // transport, not of this service, and cannot fail. A line that reads
        // like a check but has no failing input is worse than no line: it makes
        // the refusal look better covered than it is.
        //
        // The content that assertion was reaching for is real and is asserted
        // below instead, on the direction that CAN fail: the vanilla request
        // must come back carrying a payoff curve. That is what makes "no curve
        // on refusal" mean anything.

        // BREAK DIRECTION. Without this, the check above is satisfied by an
        // engine that refuses every request, including vanilla ones -- which
        // is precisely how a refuse-only gate passes its own test while
        // taking the product down.
        StrategyRequest vanilla = req;
        vanilla.mutable_legs(0)->set_asian_type(Leg::NOT_ASIAN);
        auto [v_status, v_resp] = call_strategy(stub, vanilla);
        check(v_status.ok(),
              "...while the SAME request with asian_type cleared succeeds: the refusal "
              "is caused by the averaging style, not by the request shape");
        check(v_resp.pnl_matrix_size() > 0,
              "...and that vanilla answer really does carry a payoff curve");

        // A malformed Asian must be named as malformed, not as unsupported --
        // averaging_states < 2 is a CRASH in price_option_double (it
        // subscripts a vector whose grid has no width), so the bound is
        // enforced before the unsupported-instrument refusal is reached.
        StrategyRequest bad_states = req;
        bad_states.mutable_legs(0)->set_averaging_states(1);
        auto [b_status, b_resp] = call_strategy(stub, bad_states);
        check(b_status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "averaging_states=1 is INVALID_ARGUMENT, distinct from the Asian refusal: "
              "a malformed Asian is named as malformed");

        StrategyRequest ok_states = req;
        ok_states.mutable_legs(0)->set_averaging_states(50);
        auto [o_status, o_resp] = call_strategy(stub, ok_states);
        check(o_status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
              "...while a VALID averaging_states still reaches the Asian refusal, so the "
              "bounds check is not standing in for it");
    }


    // =======================================================================
    // 5. SAVED SCENARIOS
    //
    // The property that matters most here is not "save works" -- it is that a
    // scenario is reachable ONLY by the verified user who saved it. Section 5c
    // is the one that proves it; everything before it establishes that the
    // happy path is real enough for 5c to mean something.
    // =======================================================================
    {
        section("5. Saved scenarios");

        // Set BEFORE any RPC: both are read per-call, so this is the
        // configuration every request below sees.
        ::setenv("SUPABASE_JWT_SECRET", std::string{kJwtSecret}.c_str(), 1);
        ::setenv("PRO_GATE_MODE", "enforce", 1);

        constexpr std::string_view kAlice = "11111111-1111-4111-8111-111111111111";
        constexpr std::string_view kBob = "22222222-2222-4222-8222-222222222222";
        const auto alice_pro = mint_jwt(kAlice, "pro");
        const auto bob_pro = mint_jwt(kBob, "pro");
        const auto alice_free = mint_jwt(kAlice, "free");

        auto fake = std::make_shared<FakeStore>();
        SavedFixture fx{fake};
        auto& s = *fx.stub;

        // ------------------------------------------------------------------
        // 5a. Refusals
        // ------------------------------------------------------------------
        {
            calculator::SaveStrategyRequest req;
            req.set_name("Anonymous attempt");
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context("");
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::UNAUTHENTICATED,
                  "anonymous SaveStrategy is UNAUTHENTICATED (got " +
                      std::to_string(static_cast<int>(st.error_code())) + ")");
            check(fake->count_for(kAlice) == 0 && fake->count_for(kBob) == 0,
                  "anonymous SaveStrategy wrote nothing");
        }
        {
            // The security-critical variant: with the Pro gate OFF, an
            // anonymous caller must STILL be refused. If this ever returns OK,
            // every anonymous visitor shares one storage bucket.
            ::setenv("PRO_GATE_MODE", "off", 1);
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context("");
            const auto st = s.ListStrategies(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::UNAUTHENTICATED,
                  "anonymous ListStrategies is UNAUTHENTICATED even with PRO_GATE_MODE=off");
            ::setenv("PRO_GATE_MODE", "enforce", 1);
        }
        {
            calculator::SaveStrategyRequest req;
            req.set_name("Free tier attempt");
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_free);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::PERMISSION_DENIED,
                  "signed-in free tier SaveStrategy is PERMISSION_DENIED (got " +
                      std::to_string(static_cast<int>(st.error_code())) + ")");
            check(fake->count_for(kAlice) == 0, "free-tier SaveStrategy wrote nothing");
        }

        // ------------------------------------------------------------------
        // 5b. Round trip
        // ------------------------------------------------------------------
        std::string alice_id;
        {
            calculator::SaveStrategyRequest req;
            req.set_name("  Earnings play  ");  // whitespace is trimmed, not stored
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.ok(), "Pro SaveStrategy succeeds (got " + st.error_message() + ")");
            check(resp.strategy().name() == "Earnings play",
                  "the stored name is trimmed (got \"" + resp.strategy().name() + "\")");
            check(!resp.strategy().id().empty(), "the response carries a server-assigned id");
            check(!resp.strategy().created_at().empty(), "the response carries created_at");
            check(!resp.replaced_existing(), "a first save is not a replacement");
            alice_id = resp.strategy().id();
        }
        {
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.ListStrategies(ctx.get(), req, &resp);
            check(st.ok(), "Pro ListStrategies succeeds");
            check(resp.strategies_size() == 1, "Alice sees exactly her one scenario");
            if (resp.strategies_size() == 1) {
                const auto& got = resp.strategies(0).request();
                // The JSON round trip is the part that could silently lose
                // data, so it is asserted field by field rather than by count.
                check(got.underlying_symbol() == "SPY", "round trip preserves the symbol");
                check(got.legs_size() == 2, "round trip preserves both legs");
                check(std::abs(got.current_price() - 585.0) < 1e-9,
                      "round trip preserves the spot price");
                if (got.legs_size() == 2) {
                    check(got.legs(0).action() == Leg::BUY && got.legs(0).type() == Leg::CALL,
                          "round trip preserves leg 0's action and type");
                    check(std::abs(got.legs(1).strike() - 600.0) < 1e-9,
                          "round trip preserves leg 1's strike");
                    check(std::abs(got.legs(1).premium() - 5.25) < 1e-9,
                          "round trip preserves leg 1's premium");
                }
            }
        }
        {
            // Same name again: an update, not a second row.
            calculator::SaveStrategyRequest req;
            req.set_name("Earnings play");
            auto scenario = sample_scenario();
            scenario.set_current_price(590.0);
            *req.mutable_request() = scenario;
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.ok(), "re-saving under the same name succeeds");
            check(resp.replaced_existing(), "re-saving under the same name reports a replacement");
            check(fake->count_for(kAlice) == 1,
                  "re-saving under the same name leaves ONE row, not two");
        }

        // ------------------------------------------------------------------
        // 5c. Cross-user isolation -- the section this feature exists to get right
        // ------------------------------------------------------------------
        {
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context(bob_pro);
            const auto st = s.ListStrategies(ctx.get(), req, &resp);
            check(st.ok(), "Bob's ListStrategies succeeds");
            check(resp.strategies_size() == 0, "Bob does NOT see Alice's scenario");
        }
        {
            // Bob holds Alice's real id and is Pro. He must still not be able
            // to delete it -- authorisation is by subject, not by knowing an id.
            calculator::DeleteStrategyRequest req;
            req.set_id(alice_id);
            calculator::DeleteStrategyResponse resp;
            const auto ctx = auth_context(bob_pro);
            const auto st = s.DeleteStrategy(ctx.get(), req, &resp);
            check(st.ok(), "Bob's DeleteStrategy of Alice's id returns OK, not an error");
            check(!resp.deleted(), "Bob does NOT delete Alice's scenario");
            check(fake->count_for(kAlice) == 1, "Alice's scenario survives Bob's delete");
        }

        // ------------------------------------------------------------------
        // 5d. Validation and delete
        // ------------------------------------------------------------------
        {
            calculator::SaveStrategyRequest req;
            req.set_name("   ");
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a whitespace-only name is INVALID_ARGUMENT");
        }
        {
            calculator::SaveStrategyRequest req;
            req.set_name("No legs");
            req.mutable_request()->set_underlying_symbol("SPY");
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a scenario with no legs is INVALID_ARGUMENT");
        }
        {
            calculator::DeleteStrategyRequest req;
            req.set_id("not-a-uuid");
            calculator::DeleteStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.DeleteStrategy(ctx.get(), req, &resp);
            check(st.ok() && !resp.deleted(),
                  "deleting a malformed id is OK with deleted=false, not an error");
        }
        {
            calculator::DeleteStrategyRequest req;
            req.set_id(alice_id);
            calculator::DeleteStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.DeleteStrategy(ctx.get(), req, &resp);
            check(st.ok() && resp.deleted(), "Alice deletes her own scenario");
            check(fake->count_for(kAlice) == 0, "Alice's scenario is gone");

            // Idempotent: deleting it again is not an error.
            calculator::DeleteStrategyResponse again;
            const auto ctx2 = auth_context(alice_pro);
            const auto st2 = s.DeleteStrategy(ctx2.get(), req, &again);
            check(st2.ok() && !again.deleted(), "deleting the same id twice is OK, deleted=false");
        }

        // ------------------------------------------------------------------
        // 5e. No database configured
        // ------------------------------------------------------------------
        {
            SavedFixture none{nullptr};
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = none.stub->ListStrategies(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
                  "with no store configured the RPC is FAILED_PRECONDITION, not a crash (got " +
                      std::to_string(static_cast<int>(st.error_code())) + ")");
        }

        ::unsetenv("PRO_GATE_MODE");
        ::unsetenv("SUPABASE_JWT_SECRET");
    }

    // =======================================================================
    section("7. MATRIX PRICE BOUNDS: a window on the grid, not on the answer");
    // =======================================================================
    // The matrix gets its OWN price grid, and the reason is the invariant
    // asserted below rather than tidiness. price_grid is swept four times --
    // the expiry curve (which is where max_profit, max_loss and break_even
    // come from), the breakeven interpolation, the matrix, and the lognormal
    // probability distribution. Narrowing a SHARED grid to inspect 480..520
    // would have reported the maximum P&L *inside that window* as the
    // position's max profit: a smaller number, correctly computed, answering a
    // question nobody asked. That is the failure this whole design exists to
    // prevent, so it is the first thing tested.
    //
    // Single-leg throughout, so it holds under any ambient PRO_GATE_MODE --
    // the same control section 3 relies on.
    {
        const auto base = [] {
            StrategyRequest req;
            req.set_underlying_symbol("SPY");
            req.set_current_price(500.0);
            req.set_risk_free_rate(0.04);
            req.set_price_range_percent(0.20);   // default window: 400..600
            req.set_price_steps(41);
            req.set_date_steps(3);
            add_leg(req, Leg::BUY, Leg::CALL, 500.0, 45.0, 15.0, 0.25);
            return req;
        };

        const auto cell_range = [](const StrategyResponse& r) {
            double lo = std::numeric_limits<double>::infinity();
            double hi = -std::numeric_limits<double>::infinity();
            for (const auto& c : r.matrix()) {
                lo = std::min(lo, c.price());
                hi = std::max(hi, c.price());
            }
            return std::pair{lo, hi};
        };

        auto [st_free, unbounded] = call_strategy(stub, base());
        check(st_free.ok(), "unbounded request succeeds");

        {
            auto req = base();
            req.set_matrix_price_min(480.0);
            req.set_matrix_price_max(520.0);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "a bounded request succeeds");

            const auto [lo, hi] = cell_range(resp);
            check(std::abs(lo - 480.0) < 1e-9 && std::abs(hi - 520.0) < 1e-9,
                  "the matrix spans EXACTLY the requested 480..520 (got " +
                      std::to_string(lo) + ".." + std::to_string(hi) + ")");
            check(resp.matrix_size() == 41 * 3,
                  "and still has the requested 41 x 3 cells -- bounds change the WINDOW, "
                  "not the resolution (got " + std::to_string(resp.matrix_size()) + ")");

            // THE INVARIANT. Equality, not a tolerance: these are computed by
            // sweeping a grid the bounds must not have touched, so any
            // difference at all means the grids got shared again.
            check(resp.max_profit() == unbounded.max_profit() &&
                      resp.max_loss() == unbounded.max_loss() &&
                      resp.break_even() == unbounded.break_even(),
                  "max_profit, max_loss and break_even are IDENTICAL to the unbounded "
                  "request -- bounding the matrix does not rewrite the headline numbers");

            bool curve_same = resp.pnl_matrix_size() == unbounded.pnl_matrix_size();
            for (int i = 0; curve_same && i < resp.pnl_matrix_size(); ++i) {
                curve_same = resp.pnl_matrix(i).underlying_price() == unbounded.pnl_matrix(i).underlying_price() &&
                             resp.pnl_matrix(i).pnl() == unbounded.pnl_matrix(i).pnl();
            }
            check(curve_same,
                  "and the expiry curve is unmoved, point for point -- the probability "
                  "distribution is drawn over that same grid");
        }
        {
            // Each side falls back independently, so a caller can pin the
            // upside alone. Asserting the UNPINNED end is the point: it must
            // still be the default 400, not 0 and not the pinned value.
            auto req = base();
            req.set_matrix_price_max(700.0);
            auto [status, resp] = call_strategy(stub, req);
            const auto [lo, hi] = cell_range(resp);
            check(status.ok() && std::abs(lo - 400.0) < 1e-9 && std::abs(hi - 700.0) < 1e-9,
                  "pinning only the upper bound leaves the lower at the default 400 (got " +
                      std::to_string(lo) + ".." + std::to_string(hi) + ")");
        }
        {
            auto req = base();
            req.set_matrix_price_min(550.0);
            auto [status, resp] = call_strategy(stub, req);
            const auto [lo, hi] = cell_range(resp);
            check(status.ok() && std::abs(lo - 550.0) < 1e-9 && std::abs(hi - 600.0) < 1e-9,
                  "pinning only the lower bound leaves the upper at the default 600");
        }
        {
            // Refused, not swapped. Two plausible repairs exist and choosing
            // one silently answers a different question.
            auto req = base();
            req.set_matrix_price_min(520.0);
            req.set_matrix_price_max(480.0);
            auto [status, resp] = call_strategy(stub, req);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an inverted pair is REFUSED, not silently swapped");
        }
        {
            // The one-sided inversion, which is the easy case to miss: 300 is
            // a perfectly ordinary lower bound and only inverts because the
            // unset upper falls back to 200.
            auto req = base();
            req.set_price_range_percent(0.60);
            req.set_current_price(500.0);
            auto narrow = base();
            narrow.set_price_range_percent(0.02);  // default window 490..510
            narrow.set_matrix_price_min(600.0);
            auto [status, resp] = call_strategy(stub, narrow);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a lower bound above the UNSET upper's fallback is refused, and the "
                  "message names both numbers");
        }
        {
            auto req = base();
            req.set_matrix_price_min(-10.0);
            auto [status, resp] = call_strategy(stub, req);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a negative bound is REFUSED -- 0 is the sentinel for unset, so a "
                  "negative one cannot be a typo for it");
        }
        {
            auto req = base();
            req.set_matrix_price_max(std::numeric_limits<double>::quiet_NaN());
            auto [status, resp] = call_strategy(stub, req);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a NaN bound is REFUSED -- unchecked it renders as a grid of blanks, "
                  "which reads as 'no data' rather than as a bad argument");
        }
    }

    // =======================================================================
    section("8. THE POSITION'S EXTREMES, NOT THE GRID'S");
    // =======================================================================
    // max_profit and max_loss are the supremum and infimum of the expiry payoff
    // over every price in (0, infinity). The curve is drawn on a +/-25% window,
    // and before this section existed the extremes were read off that window:
    // a SPY 779 call bought at 4.49 reported $19,037.25 (the grid's top edge)
    // as its max profit and a risk/reward of 42:1, where the truth is
    // "unlimited"; a long 779 put reported $19,114 where the truth is
    // (779 - 3.54) * 100 = $77,546 at a price of zero.
    //
    // Every expected value below is the closed form of a payoff written out
    // beside it, not a figure the engine handed itself. Every request is
    // single-expiry, so the curve date IS each leg's expiry and every leg is
    // settled: the payoff is piecewise linear with kinks at the strikes.
    {
        constexpr double kSpot = 779.0;

        {   // -- long call: unbounded profit, loss is the premium ----------------
            auto req = make_request(kSpot);
            add_leg(req, Leg::BUY, Leg::CALL, 779.0, 30.0, 4.49, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "long call: RPC succeeds");
            check(resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "long call: profit is UNBOUNDED, loss is bounded");
            check(near(resp.max_loss(), -449.0),
                  "long call: max_loss == -(4.49 * 100) (got " + std::to_string(resp.max_loss()) + ")");
            // What max_profit now means when the flag is set: the edge of the
            // grid, a value the position reaches and not a maximum. Pinned so a
            // client reading the number without the flag is a known quantity.
            check(near(resp.max_profit(), (779.0 * 1.25 - 779.0 - 4.49) * 100.0),
                  "long call: max_profit is the grid's top edge, (973.75 - 779 - 4.49) * 100 (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(resp.risk_reward_ratio() == 0.0,
                  "long call: no risk/reward ratio is built from a grid-edge profit (got " +
                      std::to_string(resp.risk_reward_ratio()) + ")");
            check(resp.pop() > 0.0 && resp.probability_of_target_profit() == 0.0,
                  "long call: probabilities were computed (pop > 0) and 50%-of-max has no "
                  "target, so it is left at zero");
        }

        {   // -- long put: bounded, and the maximum is at a price of ZERO --------
            auto req = make_request(kSpot);
            add_leg(req, Leg::BUY, Leg::PUT, 779.0, 30.0, 3.54, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "long put: RPC succeeds");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "long put: bounded in both directions");
            check(near(resp.max_profit(), (779.0 - 3.54) * 100.0),
                  "long put: max_profit == (779 - 3.54) * 100 = 77,546.00, the payoff at S = 0 (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(near(resp.max_loss(), -354.0),
                  "long put: max_loss == -(3.54 * 100) (got " + std::to_string(resp.max_loss()) + ")");
            check(near(resp.risk_reward_ratio(), 77546.0 / 354.0, 1e-6),
                  "long put: risk/reward is the real ratio, 77546 / 354 (got " +
                      std::to_string(resp.risk_reward_ratio()) + ")");
            // The extremes are folded in from beyond the grid and the CURVE is
            // not: it is still exactly the 81-point window.
            check(resp.pnl_matrix_size() == 81,
                  "long put: the curve is still the requested 81 grid points");
            check(curve_extremes(resp).second < resp.max_profit() - 1.0,
                  "long put: no point of the drawn curve reaches max_profit -- it is not on the grid");
        }

        {   // -- short call: unbounded loss, profit is the credit -----------------
            auto req = make_request(kSpot);
            add_leg(req, Leg::SELL, Leg::CALL, 779.0, 30.0, 4.49, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "short call: RPC succeeds");
            check(resp.max_loss_unbounded() && !resp.max_profit_unbounded(),
                  "short call: loss is UNBOUNDED, profit is bounded");
            check(near(resp.max_profit(), 449.0),
                  "short call: max_profit == the 4.49 credit * 100 (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(near(resp.max_loss(), curve_extremes(resp).first),
                  "short call: max_loss is the grid's edge, the lowest point of the drawn curve");
            check(resp.risk_reward_ratio() == 0.0, "short call: no risk/reward ratio");
        }

        {   // -- bull call spread INSIDE the grid: nothing moves ------------------
            auto req = make_request(590.0);
            add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
            add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok() && near(resp.max_profit(), 1275.0) && near(resp.max_loss(), -725.0),
                  "bull call spread inside the grid: 1275 / -725, exactly as before");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "bull call spread: the flags default to false on a bounded position");
            check(near(resp.risk_reward_ratio(), 1275.0 / 725.0, 1e-4),
                  "bull call spread: risk/reward is unchanged");
            check(resp.probability_of_target_profit() > 0.0,
                  "bull call spread: a bounded max_profit still defines a 50% target");
        }

        // -- a spread struck BEYOND the grid: the plateau it has not reached ----
        // Spot 500 with a +/-10% window is 450..550. The 560/600 vertical sits
        // wholly above it, so every point the grid samples is flat at the debit
        // and the old figure was -500 for BOTH extremes.
        auto spread_beyond_grid = [] {
            auto req = make_request(500.0, 0.10);
            add_leg(req, Leg::BUY, Leg::CALL, 560.0, 30.0, 6.00, 0.20);
            add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 1.00, 0.20);
            return req;
        };
        StrategyResponse beyond;
        {
            auto [status, resp] = call_strategy(stub, spread_beyond_grid());
            beyond = resp;
            check(status.ok(), "bull call spread beyond the grid: RPC succeeds");
            check(near(resp.max_profit(), (600.0 - 560.0 - 5.0) * 100.0),
                  "bull call spread beyond the grid: max_profit is the PLATEAU, "
                  "(40 - 5) * 100 = 3500 (got " + std::to_string(resp.max_profit()) + ")");
            check(near(resp.max_loss(), -500.0), "bull call spread beyond the grid: max_loss == -500");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "bull call spread beyond the grid: bounded both ways -- a capped spread is "
                  "not 'unlimited' just because its cap is off screen");
            check(near(curve_extremes(resp).second, -500.0),
                  "bull call spread beyond the grid: the drawn curve is still flat at -500 -- "
                  "the plateau is not painted into it");
        }

        {   // -- the downside twin: a put spread struck below the grid ------------
            auto req = make_request(500.0, 0.10);
            add_leg(req, Leg::BUY, Leg::PUT, 440.0, 30.0, 4.00, 0.20);
            add_leg(req, Leg::SELL, Leg::PUT, 400.0, 30.0, 1.00, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "bear put spread below the grid: RPC succeeds");
            check(near(resp.max_profit(), (440.0 - 400.0 - 3.0) * 100.0),
                  "bear put spread below the grid: max_profit == (40 - 3) * 100 = 3700 (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(near(resp.max_loss(), -300.0), "bear put spread below the grid: max_loss == -300");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "bear put spread below the grid: bounded both ways");
        }

        {   // -- iron condor: bounded, extremes are the closed form ---------------
            auto req = make_request(590.0);
            add_leg(req, Leg::BUY, Leg::PUT, 560.0, 30.0, 1.50, 0.20);
            add_leg(req, Leg::SELL, Leg::PUT, 570.0, 30.0, 3.00, 0.20);
            add_leg(req, Leg::SELL, Leg::CALL, 610.0, 30.0, 3.00, 0.20);
            add_leg(req, Leg::BUY, Leg::CALL, 620.0, 30.0, 1.50, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok() && near(resp.max_profit(), 300.0) && near(resp.max_loss(), -700.0),
                  "iron condor: max_profit == the 3.00 net credit * 100, max_loss == -(10 - 3) * 100");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "iron condor: its long and short calls net to no upside slope, so bounded");
        }

        {   // -- covered call: a STOCK leg is a linear leg too --------------------
            auto req = make_request(kSpot);
            add_linear_leg(req, Leg::BUY, Leg::STOCK, 779.0, 30.0, 100.0);
            add_leg(req, Leg::SELL, Leg::CALL, 800.0, 30.0, 5.00, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "covered call: RPC succeeds");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "covered call: +100 shares against a short call nets to no upside slope");
            check(near(resp.max_profit(), (800.0 - 779.0 + 5.0) * 100.0),
                  "covered call: max_profit == (800 - 779 + 5) * 100 = 2600 (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(near(resp.max_loss(), (-779.0 + 5.0) * 100.0),
                  "covered call: max_loss == the shares going to zero less the credit, -77,400 (got " +
                      std::to_string(resp.max_loss()) + ")");
        }

        {   // -- a diagonal whose worst case is the TAIL: only the far point sees it
            // Long a 90-day 600 call (8.00), short a 30-day 500 call (52.00), on
            // a 550 underlying. The curve is drawn at the near expiry, where the
            // short call is settled and the long one still has 60 days of life.
            // Above 500 the position is C_far(S) - (S - 500) + 44, which falls
            // monotonically toward its asymptote
            //     500 - 600 * exp(-r * 60/365) + 44   per share,
            // because the long call's value tends to S - K * exp(-rT). That
            // asymptote is the worst the position can do, and the +25% grid
            // (up to 687.5) stops short of it by about $77 per contract. Its
            // calls net to zero slope, so the loss is bounded and must be the
            // asymptote, not the grid's edge.
            auto req = make_request(550.0);
            add_leg(req, Leg::BUY, Leg::CALL, 600.0, 90.0, 8.00, 0.20);
            add_leg(req, Leg::SELL, Leg::CALL, 500.0, 30.0, 52.00, 0.20);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "call diagonal: RPC succeeds");
            check(!resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "call diagonal: a long and a short call net to no upside slope, so bounded");
            const double tail = (500.0 - 600.0 * std::exp(-0.04 * 60.0 / 365.0) + 44.0) * 100.0;
            check(near(resp.max_loss(), tail, 1e-3),
                  "call diagonal: max_loss is the S -> infinity asymptote " + std::to_string(tail) +
                      ", not the grid's edge (got " + std::to_string(resp.max_loss()) + ")");
            check(curve_extremes(resp).first > resp.max_loss() + 10.0,
                  "call diagonal: the drawn curve never reaches that loss -- it lies beyond the grid");
            check(resp.max_profit() > 44.0 * 100.0,
                  "call diagonal: max_profit exceeds the credit kept at S = 0, the peak being at 500");
        }

        {   // -- a call ratio backspread: net long calls, unbounded profit --------
            // Sell 1 call at 500 for 12, buy 2 calls at 520 for 4. Per share the
            // payoff is 4 up to 500, then 4 - (S - 500) to 520 where it bottoms
            // at -16, then S - 536 climbing without limit. The +/-2% grid is
            // 490..510, so the 520 kink where the loss is worst is OFF the grid.
            auto req = make_request(500.0, 0.02);
            add_leg(req, Leg::SELL, Leg::CALL, 500.0, 30.0, 12.00, 0.20, 1);
            add_leg(req, Leg::BUY, Leg::CALL, 520.0, 30.0, 4.00, 0.20, 2);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "call backspread: RPC succeeds");
            check(resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "call backspread: net long calls (2 against 1) are UNBOUNDED profit");
            check(near(resp.max_loss(), -1600.0),
                  "call backspread: max_loss == -16 * 100, at the 520 kink beyond the grid (got " +
                      std::to_string(resp.max_loss()) + ")");
            check(resp.risk_reward_ratio() == 0.0, "call backspread: no risk/reward ratio");
        }

        {   // -- the matrix window is a window on the matrix, not on the answer ---
            auto pinned = spread_beyond_grid();
            // A window across both strikes, where the payoff runs from -500 to
            // 3500: a matrix grid leaking into the headline numbers would add a
            // breakeven the +/-10% curve does not have.
            pinned.set_matrix_price_min(560.0);
            pinned.set_matrix_price_max(640.0);
            auto [status, resp] = call_strategy(stub, pinned);
            check(status.ok(), "pinned matrix window: RPC succeeds");
            check(resp.max_profit() == beyond.max_profit() && resp.max_loss() == beyond.max_loss() &&
                      resp.max_profit_unbounded() == beyond.max_profit_unbounded() &&
                      resp.max_loss_unbounded() == beyond.max_loss_unbounded() &&
                      resp.break_even() == beyond.break_even(),
                  "pinning the matrix to 560..640 leaves max_profit, max_loss, break_even and BOTH "
                  "flags identical -- the extremes never read the matrix grid");
            double lo = std::numeric_limits<double>::infinity();
            for (const auto& c : resp.matrix()) lo = std::min(lo, c.price());
            check(near(lo, 560.0, 1e-9), "...while the matrix really is on the pinned window");
            check(resp.breakeven_prices_size() == beyond.breakeven_prices_size(),
                  "...and the breakevens, read off the curve, are the unpinned curve's");
        }
    }

    // =======================================================================
    section("9. FUTURES NEED NO VOLATILITY");
    // =======================================================================
    // A future's P&L is (price - entry) * multiplier * quantity. It has no
    // implied volatility, and a request made only of futures legs has none
    // anywhere. The engine must price it, flag it, and leave the probability
    // fields at zero -- without an error and without a NaN -- because the
    // client is about to stop demanding an IV for a position that has no use
    // for one.
    {
        constexpr double kEs = 7855.19;
        constexpr double kMult = 50.0;
        const auto es_request = [&] {
            auto req = make_request(kEs);
            req.set_underlying_symbol("ES");
            return req;
        };

        {   // -- long ES, December, 72 days, NO implied volatility anywhere --------
            auto req = es_request();
            add_linear_leg(req, Leg::BUY, Leg::FUTURE, kEs, 72.0, kMult);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "long future, no IV: RPC succeeds (got " + status.error_message() + ")");
            check(resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "long future: profit unbounded, loss bounded");
            check(near(resp.max_loss(), -kEs * kMult),
                  "long future: max_loss == -(7855.19 * 50), the contract going to zero (got " +
                      std::to_string(resp.max_loss()) + ")");
            check(near(resp.max_profit(), kEs * 0.25 * kMult),
                  "long future: max_profit is the grid's edge, 0.25 * 7855.19 * 50 (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(near(resp.net_greeks().delta(), kMult), "long future: net delta == +50");
            check(resp.leg_risk_size() == 1 && near(resp.leg_risk(0).open_pnl(), 0.0) &&
                      near(resp.leg_risk(0).greeks().delta(), kMult),
                  "long future: one leg_risk, open P&L 0 at entry, delta 50");
            check(near(resp.curve_days_to_expiration(), 72.0),
                  "long future: the curve is drawn at the 72-day expiry");
            check(resp.pnl_matrix_size() == 81 && resp.matrix_size() == 81 * 12,
                  "long future: curve and matrix have the requested dimensions");
            check(resp.pop() == 0.0 && resp.expected_value() == 0.0 &&
                      resp.probability_of_touch() == 0.0 &&
                      resp.probability_of_target_profit() == 0.0 &&
                      resp.risk_metrics().var_parametric_95() == 0.0 &&
                      resp.risk_metrics().cvar_parametric_99() == 0.0,
                  "long future: every probability field is left at zero");
            check(all_finite(resp), "long future: no NaN or infinity anywhere in the response");
            check(resp.breakeven_prices_size() == 1 && near(resp.break_even(), kEs, 1e-3),
                  "long future: one breakeven, at the entry price");
        }

        {   // -- short ES: the mirror -------------------------------------------
            auto req = es_request();
            add_linear_leg(req, Leg::SELL, Leg::FUTURE, kEs, 72.0, kMult);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "short future, no IV: RPC succeeds");
            check(resp.max_loss_unbounded() && !resp.max_profit_unbounded(),
                  "short future: loss unbounded, profit bounded");
            check(near(resp.max_profit(), kEs * kMult),
                  "short future: max_profit == entry * 50 * 1, at a price of zero (got " +
                      std::to_string(resp.max_profit()) + ")");
            check(near(resp.net_greeks().delta(), -kMult), "short future: net delta == -50");
            check(resp.risk_reward_ratio() == 0.0 && all_finite(resp),
                  "short future: no ratio, nothing non-finite");
        }

        {   // -- a future AND an option with an IV: protective put ---------------
            auto req = es_request();
            add_linear_leg(req, Leg::BUY, Leg::FUTURE, kEs, 72.0, kMult);
            add_leg(req, Leg::BUY, Leg::PUT, 7800.0, 72.0, 120.50, 0.18, 1, kMult);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "future + put: RPC succeeds (got " + status.error_message() + ")");
            check(resp.max_profit_unbounded() && !resp.max_loss_unbounded(),
                  "future + put: the put adds no upside slope, so profit is still unbounded");
            check(near(resp.max_loss(), (7800.0 - kEs - 120.50) * kMult),
                  "future + put: max_loss == (7800 - 7855.19 - 120.50) * 50 = -8,784.50 (got " +
                      std::to_string(resp.max_loss()) + ")");
            check(resp.pop() > 0.0 && resp.pop() < 1.0 && all_finite(resp),
                  "future + put: the option's IV drives the probabilities, which are finite");
            check(resp.leg_risk_size() == 2, "future + put: one leg_risk per leg");
        }
    }

    // =======================================================================
    section("10. ONE TODAY: the chain's dates and the matrix's dates agree");
    // =======================================================================
    // OFC-018. The expiration list is cached for 12 hours and each entry used to
    // carry `days` and a "(7d)" label computed WHEN IT WAS FILLED. Read after a
    // UTC midnight the count was a day high; a client built its leg from it,
    // and the matrix -- counting from today -- dated its last column a day past
    // the expiry the user chose. Observed live: expiry 2026-10-13, last matrix
    // column 2026-10-14 reading "0 days to expiry", the real expiry column
    // reading "1 days".
    //
    // The calendar is injected, so midnight is crossed by assignment and not by
    // sleeping. The fake provider counts its own calls, which is what proves the
    // second read was SERVED FROM THE CACHE: without it, a re-fetch would
    // recompute the count and this section would pass on a build with the bug.
    {
        auto fake = std::make_shared<FakeChainProvider>();
        md::set_provider(fake);
        md::set_rate_provider(std::make_shared<FakeRateProvider>());
        md::set_calendar_provider(fake_today);
        md::quote_cache().clear();
        md::expiration_cache().clear();
        md::chain_cache().clear();
        md::rate_cache().clear();
        md::clear_last_good_chain();

        // Dates in 2035, far from any day this test can run on: a mutant that
        // reads the REAL clock instead of the injected one must disagree with
        // the injected dates, which it would not on the day they coincided.
        constexpr std::string_view kExpiry = "2035-10-12";
        const auto day_one = std::chrono::sys_days{std::chrono::year{2035} / std::chrono::October / 5};
        const auto expiry_days = std::chrono::sys_days{std::chrono::year{2035} / std::chrono::October / 12};

        const auto served = [&](const ChainResponse& chain) {
            for (const auto& e : chain.available_expirations()) {
                if (e.date_str() == kExpiry) return std::pair{e.days_to_expiry(), e.label()};
            }
            return std::pair{-999, std::string{"<expiry not listed>"}};
        };

        g_fake_today = day_one;
        auto [st1, chain1] = get_chain(stub, "SPY", "EQUITY", std::string{kExpiry});
        const auto [days1, label1] = served(chain1);
        check(st1.ok() && days1 == 7 && label1 == "Oct 12, 2035 (7d)",
              "day one: 2035-10-12 is 7 days away and labelled so (got " + std::to_string(days1) +
                  ", '" + label1 + "')");
        check(fake->chain_calls() == 1 && fake->expirations_calls() == 1,
              "day one: the provider was asked once for each");

        // Midnight passes. Nothing is evicted: the chain cache holds 15 minutes
        // and the expiration cache 12 hours, so every entry below is served warm.
        g_fake_today = day_one + std::chrono::days{1};
        auto [st2, chain2] = get_chain(stub, "SPY", "EQUITY", std::string{kExpiry});
        const auto [days2, label2] = served(chain2);
        check(fake->chain_calls() == 1 && fake->expirations_calls() == 1,
              "day two: served from the cache -- the provider was NOT asked again");
        check(st2.ok() && days2 == 6 && label2 == "Oct 12, 2035 (6d)",
              "day two: the SAME cached entry now reads 6 days and '(6d)' (got " +
                  std::to_string(days2) + ", '" + label2 + "')");

        // The leg the client builds from what it was served, priced by the
        // engine on the same day. Every cell must be dated exactly as many days
        // before the expiry as it says it is.
        auto req = make_request(779.0, 0.25, 7);
        add_leg(req, Leg::BUY, Leg::CALL, 779.0, static_cast<double>(days2), 4.49, 0.20);
        auto [st3, resp] = call_strategy(stub, req);
        check(st3.ok() && resp.matrix_size() == 81 * 7, "the matrix is 7 columns of 81 prices");

        bool every_cell_consistent = true;
        std::string first_date;
        std::string last_date;
        std::uint32_t first_dte = 0;
        std::uint32_t last_dte = 99;
        for (const auto& cell : resp.matrix()) {
            const auto implied = expiry_days - std::chrono::days{cell.days_to_expiration()};
            every_cell_consistent =
                every_cell_consistent && cell.date_str() == std::format("{:%Y-%m-%d}", implied);
        }
        if (resp.matrix_size() > 0) {
            first_date = resp.matrix(0).date_str();
            first_dte = resp.matrix(0).days_to_expiration();
            last_date = resp.matrix(resp.matrix_size() - 1).date_str();
            last_dte = resp.matrix(resp.matrix_size() - 1).days_to_expiration();
        }
        check(every_cell_consistent,
              "every matrix cell is dated (expiry - days_to_expiration): no column is off by a day");
        check(last_date == kExpiry && last_dte == 0,
              "the LAST column is the chosen expiry, 2035-10-12, reading 0 days (got " + last_date +
                  ", " + std::to_string(last_dte) + ")");
        check(first_date == "2035-10-06" && first_dte == 6,
              "the FIRST column is today on the injected calendar, 2035-10-06, reading 6 days (got " +
                  first_date + ", " + std::to_string(first_dte) + ")");

        // The third place that reads a date: the futures term structure.
        // ESZ35 settles on the third Friday of December 2035.
        auto [st4, futures] = get_chain(stub, "ES", "FUTURES", "");
        check(st4.ok() && futures.futures_contracts_size() > 0 &&
                  futures.futures_contracts(0).code() == "ESZ35",
              "the term structure's first contract is ESZ35");
        const auto settle =
            std::chrono::sys_days{std::chrono::year{2035} / std::chrono::December / std::chrono::Friday[3]};
        if (futures.futures_contracts_size() > 0) {
            check(futures.futures_contracts(0).days_to_expiry() == (settle - g_fake_today).count(),
                  "the term structure counts from the same injected today (day two: " +
                      std::to_string(futures.futures_contracts(0).days_to_expiry()) + ")");
        }

        md::set_calendar_provider(nullptr);
        const auto before = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        const auto restored = md::today();
        const auto after = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        check(restored >= before && restored <= after,
              "clearing the provider restores the real UTC date");
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
