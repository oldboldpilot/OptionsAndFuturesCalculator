module;
#include <cctype>
#include <cstddef>
#include <expected>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "finance.pb.h"
#include "finance.grpc.pb.h"

module finance_service;

import sensen.bigdecimal;
import sensen.financial;
import sensen.options;
import sensen.portfolio;
import sensen.linear_algebra;
import logger;
import quota;
import api_key;

namespace options_calculator::finance {

using grpc::ServerContext;
using grpc::Status;
using sensen::BigDecimal;

namespace {

// ---------------------------------------------------------------------------
// Decimal marshalling
// ---------------------------------------------------------------------------

/**
 * Strict decimal parsing, because BigDecimal's own is not.
 *
 * `BigDecimal::parse` skips any character that is not a digit. "abc" therefore
 * parses to 0 and "12x3" parses to 123 -- silently, with no error. That is
 * survivable inside a library whose callers are compiled against it; it is not
 * survivable on a public API, where the input is whatever arrived over the
 * wire. A typo becoming a number is exactly the class of failure a financial
 * service must not have.
 *
 * So the string is validated HERE, against the grammar BigDecimal actually
 * documents, before it is handed over: optional sign, digits, at most one
 * point, at least one digit somewhere. An empty string is proto3's default for
 * an unset field and legitimately means zero -- every optional decimal in
 * finance.proto relies on that.
 */
[[nodiscard]] auto parse_decimal(const std::string& s, BigDecimal& out) noexcept -> bool {
    std::string_view v{s};
    while (!v.empty() && (std::isspace(static_cast<unsigned char>(v.front())) != 0)) {
        v.remove_prefix(1);
    }
    while (!v.empty() && (std::isspace(static_cast<unsigned char>(v.back())) != 0)) {
        v.remove_suffix(1);
    }
    if (v.empty()) {
        out = BigDecimal(0);
        return true;
    }

    std::string_view body = v;
    if (body.front() == '-' || body.front() == '+') body.remove_prefix(1);
    if (body.empty()) return false;

    bool seen_digit = false;
    bool seen_point = false;
    for (const char c : body) {
        if (c == '.') {
            if (seen_point) return false;   // two points is not a number
            seen_point = true;
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            seen_digit = true;
        } else {
            return false;
        }
    }
    if (!seen_digit) return false;          // "." and "-" alone are not numbers

    out = BigDecimal(v);
    return true;
}

/** The error a malformed decimal earns. Names the field so it is actionable. */
[[nodiscard]] auto bad_decimal(std::string_view field, const std::string& value) -> Status {
    return Status(grpc::StatusCode::INVALID_ARGUMENT,
                  std::string(field) + " is not a decimal number: \"" + value + "\"");
}

/**
 * Reads one decimal field or fails the RPC.
 *
 * A macro rather than a function because it returns from the CALLER on
 * failure, which is the whole point: there is no sensible value to substitute
 * for a number that could not be read, and continuing with a zero would be the
 * silent-default this service exists to avoid.
 */
#define READ_DECIMAL(dest, expr, name)                       \
    BigDecimal dest;                                         \
    do {                                                     \
        const std::string& _s = (expr);                      \
        if (!parse_decimal(_s, dest)) {                      \
            return bad_decimal((name), _s);                  \
        }                                                    \
    } while (false)

[[nodiscard]] auto timing_of(sensen::finance::AnnuityTiming t) noexcept -> int {
    return (t == sensen::finance::BEGINNING_OF_PERIOD) ? 1 : 0;
}

[[nodiscard]] auto option_type_of(sensen::finance::OptionType t) noexcept -> sensen::OptionType {
    return (t == sensen::finance::PUT) ? sensen::OptionType::Put : sensen::OptionType::Call;
}

[[nodiscard]] auto asian_type_of(sensen::finance::AsianType t) noexcept -> sensen::AsianType {
    switch (t) {
        case sensen::finance::AVERAGE_PRICE:  return sensen::AsianType::AveragePrice;
        case sensen::finance::AVERAGE_STRIKE: return sensen::AsianType::AverageStrike;
        default:                              return sensen::AsianType::None;
    }
}

/** A std::expected failure becomes the gRPC error carrying its own message. */
template <typename T>
[[nodiscard]] auto fail(const std::expected<T, std::string>& e) -> Status {
    return Status(grpc::StatusCode::FAILED_PRECONDITION, e.error());
}

/**
 * Charges this call against the caller's quota, or refuses it.
 *
 * A macro because it must RETURN from the RPC on refusal -- the whole point is
 * that the work below never runs. Placed before any computation and after the
 * null check, so a refused call costs the server a hash lookup rather than a
 * Monte Carlo.
 *
 * `cost` is priced from the request's own arguments, which is why this sits at
 * the call site instead of in a gRPC interceptor: an interceptor sees the
 * method name and the metadata, but not `paths`, `steps` or `term_months`, so
 * it could only ever charge every RPC the same.
 */
/**
 * The admission guard every finance RPC runs first: authenticate, then charge.
 *
 * The order is not arbitrary. Authentication is the cheaper check and the more
 * decisive one, and it produces the identity that quota then meters against --
 * charging first would spend an unauthenticated caller's allowance on a request
 * that was going to be refused anyway, and would meter a verified partner
 * against the shared anonymous bucket.
 *
 * It stays a macro, and stays at the top of each RPC, for the reason quota.cppm
 * documents: a synchronous C++ server interceptor cannot return a Status to
 * reject a call, and cannot see the deserialized request, so it could not price
 * the work. One visible line per RPC is debuggable and greppable; an RPC that
 * forgets it is a visible omission rather than an invisible hole.
 */
#define CHARGE(method_name, cost)                                                \
    ::options_calculator::auth::Identity _id;                                    \
    do {                                                                         \
        if (auto _a = ::options_calculator::auth::KeyRegistry::instance()         \
                          .authenticate(*context, "finance", (method_name), _id); \
            !_a.ok()) {                                                          \
            return _a;                                                           \
        }                                                                        \
        ::options_calculator::quota::TierLimits _lim{_id.requests_per_minute,     \
                                                    _id.compute_units_per_hour}; \
        if (auto _q = ::options_calculator::quota::QuotaEnforcer::instance()      \
                          .admit_identity(_id.id, _id.tier, (method_name),        \
                                          (cost),                                 \
                                          _id.has_limits ? &_lim : nullptr);      \
            !_q.ok()) {                                                          \
            return _q;                                                           \
        }                                                                        \
    } while (false)

// ---------------------------------------------------------------------------
// Service
// ---------------------------------------------------------------------------

class FinanceServiceImpl final : public sensen::finance::Finance::Service {
  public:
    // -- Time value of money -------------------------------------------------

    auto ComputePayment(ServerContext* context, const sensen::finance::PaymentRequest* request,
                        sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePayment", quota::cost_default());
        READ_DECIMAL(rate, request->rate(), "rate");
        READ_DECIMAL(pv, request->present_value(), "present_value");
        READ_DECIMAL(fv, request->future_value(), "future_value");
        const auto r = sensen::pmt(rate, request->periods(), pv, fv, timing_of(request->timing()));
        response->set_value(r.to_string());
        return Status::OK;
    }

    auto ComputePresentValue(ServerContext* context, const sensen::finance::PresentValueRequest* request,
                             sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePresentValue", quota::cost_default());
        READ_DECIMAL(rate, request->rate(), "rate");
        READ_DECIMAL(pmt_v, request->payment(), "payment");
        READ_DECIMAL(fv, request->future_value(), "future_value");
        const auto r = sensen::pv(rate, request->periods(), pmt_v, fv, timing_of(request->timing()));
        response->set_value(r.to_string());
        return Status::OK;
    }

    auto ComputeFutureValue(ServerContext* context, const sensen::finance::FutureValueRequest* request,
                            sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeFutureValue", quota::cost_default());
        READ_DECIMAL(rate, request->rate(), "rate");
        READ_DECIMAL(pmt_v, request->payment(), "payment");
        READ_DECIMAL(pv_v, request->present_value(), "present_value");
        const auto r = sensen::fv(rate, request->periods(), pmt_v, pv_v, timing_of(request->timing()));
        response->set_value(r.to_string());
        return Status::OK;
    }

    auto ComputeFutureValueDetailed(ServerContext* context,
                                    const sensen::finance::FutureValueDetailedRequest* request,
                                    sensen::finance::FutureValueDetailedResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeFutureValueDetailed", quota::cost_default());
        // Refused rather than defaulted. A compounding frequency changes the
        // answer materially, and picking one the caller did not state would be
        // this service inventing an assumption on their behalf.
        if (request->compound_frequency() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "compound_frequency must be positive (12 monthly, 4 quarterly, 1 annual)");
        }
        if (request->years() < 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "years cannot be negative");
        }
        READ_DECIMAL(rate, request->annual_rate(), "annual_rate");
        READ_DECIMAL(contrib, request->annual_contribution(), "annual_contribution");
        READ_DECIMAL(principal, request->current_principal(), "current_principal");
        READ_DECIMAL(inflation, request->annual_inflation_rate(), "annual_inflation_rate");

        const auto s = sensen::calculate_future_value_detailed(
            rate, request->years(), contrib, principal, inflation, request->compound_frequency());
        response->set_nominal_fv(s.nominal_fv.to_string());
        response->set_inflation_adjusted_fv(s.inflation_adjusted_fv.to_string());
        response->set_total_contributions(s.total_contributions.to_string());
        response->set_total_interest_earned(s.total_interest_earned.to_string());
        return Status::OK;
    }

    auto ComputeInterestPayment(ServerContext* context, const sensen::finance::PeriodPaymentRequest* request,
                                sensen::finance::DecimalResponse* response) -> Status override {
        CHARGE("ComputeInterestPayment", quota::cost_default());
        return period_payment(request, response, /*interest=*/true);
    }

    auto ComputePrincipalPayment(ServerContext* context, const sensen::finance::PeriodPaymentRequest* request,
                                 sensen::finance::DecimalResponse* response) -> Status override {
        CHARGE("ComputePrincipalPayment", quota::cost_default());
        return period_payment(request, response, /*interest=*/false);
    }

    auto ComputeRate(ServerContext* context, const sensen::finance::RateRequest* request,
                     sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeRate", quota::cost_default());
        READ_DECIMAL(pmt_v, request->payment(), "payment");
        READ_DECIMAL(pv_v, request->present_value(), "present_value");
        READ_DECIMAL(fv_v, request->future_value(), "future_value");
        READ_DECIMAL(guess, request->guess(), "guess");
        // rate_fn solves iteratively in double, unlike the closed-form annuity
        // functions above -- so the inputs narrow here rather than pretending
        // to a precision the solver does not carry.
        //
        // An unset guess is 0, which is a legitimate rate but a poor seed; the
        // engine's own default is better, so an unstated guess uses it.
        const auto r = guess.is_zero()
            ? sensen::rate_fn(request->periods(), pmt_v.to_double(), pv_v.to_double(),
                              fv_v.to_double(), timing_of(request->timing()))
            : sensen::rate_fn(request->periods(), pmt_v.to_double(), pv_v.to_double(),
                              fv_v.to_double(), timing_of(request->timing()), guess.to_double());
        if (!r) return fail(r);
        response->set_value(BigDecimal(*r).to_string());
        return Status::OK;
    }

    auto ComputePeriods(ServerContext* context, const sensen::finance::PeriodsRequest* request,
                        sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePeriods", quota::cost_default());
        READ_DECIMAL(rate, request->rate(), "rate");
        READ_DECIMAL(pmt_v, request->payment(), "payment");
        READ_DECIMAL(pv_v, request->present_value(), "present_value");
        READ_DECIMAL(fv_v, request->future_value(), "future_value");
        // nper_fn is a double-domain closed form, same as rate_fn above.
        const auto r = sensen::nper_fn(rate.to_double(), pmt_v.to_double(), pv_v.to_double(),
                                       fv_v.to_double(), timing_of(request->timing()));
        if (!r) return fail(r);
        response->set_value(BigDecimal(*r).to_string());
        return Status::OK;
    }

    auto ConvertInterestRate(ServerContext* context, const sensen::finance::RateConversionRequest* request,
                             sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ConvertInterestRate", quota::cost_default());
        if (request->periods_per_year() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "periods_per_year must be positive");
        }
        const double v =
            (request->direction() == sensen::finance::RateConversionRequest::EFFECTIVE_TO_NOMINAL)
                ? sensen::nominal(request->rate(), request->periods_per_year())
                : sensen::effect(request->rate(), request->periods_per_year());
        response->set_value(v);
        return Status::OK;
    }

    auto ComputeFisherRate(ServerContext* context, const sensen::finance::FisherRequest* request,
                           sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeFisherRate", quota::cost_default());
        const double v =
            (request->direction() == sensen::finance::FisherRequest::REAL_TO_NOMINAL)
                ? sensen::fisher_nominal_rate(request->rate(), request->inflation_rate())
                : sensen::fisher_real_rate(request->rate(), request->inflation_rate());
        response->set_value(v);
        return Status::OK;
    }

    // -- Mortgages -----------------------------------------------------------

    auto ComputeAmortization(ServerContext* context, const sensen::finance::AmortizationRequest* request,
                             sensen::finance::AmortizationResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeAmortization", quota::cost_amortization(request->term_months()));
        if (auto s = check_term(request->term_months()); !s.ok()) return s;
        READ_DECIMAL(loan, request->loan_amount(), "loan_amount");
        READ_DECIMAL(rate, request->annual_rate(), "annual_rate");
        READ_DECIMAL(extra, request->monthly_overpayment(), "monthly_overpayment");
        READ_DECIMAL(pmi, request->pmi_annual_rate(), "pmi_annual_rate");
        READ_DECIMAL(home, request->original_home_value(), "original_home_value");

        auto [schedule, summary] = sensen::calculate_mortgage_amortization(
            loan, rate, request->term_months(), extra, pmi, home);

        for (const auto& row : schedule) {
            auto& r = *response->add_schedule();
            r.set_period(row.period);
            r.set_start_balance(row.start_balance.to_string());
            r.set_scheduled_payment(row.scheduled_payment.to_string());
            r.set_extra_payment(row.extra_payment.to_string());
            r.set_interest_paid(row.interest_paid.to_string());
            r.set_principal_paid(row.principal_paid.to_string());
            r.set_pmi_paid(row.pmi_paid.to_string());
            r.set_end_balance(row.end_balance.to_string());
        }
        auto& s = *response->mutable_summary();
        s.set_total_principal_paid(summary.total_principal_paid.to_string());
        s.set_total_interest_paid(summary.total_interest_paid.to_string());
        s.set_total_pmi_paid(summary.total_pmi_paid.to_string());
        s.set_total_payments_paid(summary.total_payments_paid.to_string());
        s.set_actual_term_months(summary.actual_term_months);
        return Status::OK;
    }

    auto ComputeDetailedAmortization(ServerContext* context,
                                     const sensen::finance::DetailedAmortizationRequest* request,
                                     sensen::finance::DetailedAmortizationResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeDetailedAmortization", quota::cost_amortization(request->term_months()));
        if (auto s = check_term(request->term_months()); !s.ok()) return s;
        READ_DECIMAL(loan, request->loan_amount(), "loan_amount");
        READ_DECIMAL(rate, request->annual_rate(), "annual_rate");
        READ_DECIMAL(extra, request->monthly_overpayment(), "monthly_overpayment");
        READ_DECIMAL(pmi, request->pmi_annual_rate(), "pmi_annual_rate");
        READ_DECIMAL(home, request->original_home_value(), "original_home_value");
        READ_DECIMAL(tax, request->annual_tax_rate(), "annual_tax_rate");

        auto [schedule, summary] = sensen::calculate_detailed_mortgage_amortization(
            loan, rate, request->term_months(), extra, pmi, home, tax);

        for (const auto& row : schedule) {
            auto& r = *response->add_schedule();
            r.set_period(row.period);
            r.set_start_balance(row.start_balance.to_string());
            r.set_scheduled_payment(row.scheduled_payment.to_string());
            r.set_extra_payment(row.extra_payment.to_string());
            r.set_interest_paid(row.interest_paid.to_string());
            r.set_principal_paid(row.principal_paid.to_string());
            r.set_pmi_paid(row.pmi_paid.to_string());
            r.set_tax_savings(row.tax_savings.to_string());
            r.set_end_balance(row.end_balance.to_string());
        }
        auto& s = *response->mutable_summary();
        s.set_total_principal_paid(summary.total_principal_paid.to_string());
        s.set_total_interest_paid(summary.total_interest_paid.to_string());
        s.set_total_pmi_paid(summary.total_pmi_paid.to_string());
        s.set_total_payments_paid(summary.total_payments_paid.to_string());
        s.set_total_tax_savings(summary.total_tax_savings.to_string());
        s.set_actual_term_months(summary.actual_term_months);
        return Status::OK;
    }

    auto ComputeAmortizationBatch(ServerContext* context,
                                  const sensen::finance::AmortizationBatchRequest* request,
                                  sensen::finance::AmortizationBatchResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeAmortizationBatch",
               quota::cost_amortization_batch(request->loan_amounts_size(),
                                              request->term_months_size() > 0
                                                  ? request->term_months(0) : 0));
        const int n = request->loan_amounts_size();
        if (n == 0) return Status::OK;

        // A ragged batch is refused, not truncated to the shortest column.
        // Silently dropping loans would return a shorter list that looks like a
        // complete answer.
        const auto same = [n](int m, const char* which) -> Status {
            if (m != n) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string(which) + " has " + std::to_string(m) +
                                  " entries but loan_amounts has " + std::to_string(n) +
                                  "; every field must describe the same loans");
            }
            return Status::OK;
        };
        if (auto s = same(request->annual_rates_size(), "annual_rates"); !s.ok()) return s;
        if (auto s = same(request->term_months_size(), "term_months"); !s.ok()) return s;
        if (auto s = same(request->extra_payments_size(), "extra_payments"); !s.ok()) return s;
        if (auto s = same(request->pmi_rates_size(), "pmi_rates"); !s.ok()) return s;
        if (auto s = same(request->home_values_size(), "home_values"); !s.ok()) return s;

        for (int i = 0; i < n; ++i) {
            if (auto s = check_term(request->term_months(i)); !s.ok()) return s;
        }

        const std::vector<double> loans(request->loan_amounts().begin(), request->loan_amounts().end());
        const std::vector<double> rates(request->annual_rates().begin(), request->annual_rates().end());
        const std::vector<int> terms(request->term_months().begin(), request->term_months().end());
        const std::vector<double> extras(request->extra_payments().begin(), request->extra_payments().end());
        const std::vector<double> pmis(request->pmi_rates().begin(), request->pmi_rates().end());
        const std::vector<double> homes(request->home_values().begin(), request->home_values().end());

        const auto summaries =
            sensen::calculate_mortgage_batch_cpu(loans, rates, terms, extras, pmis, homes);
        for (const auto& summary : summaries) {
            auto& s = *response->add_summaries();
            s.set_total_principal_paid(summary.total_principal_paid.to_string());
            s.set_total_interest_paid(summary.total_interest_paid.to_string());
            s.set_total_pmi_paid(summary.total_pmi_paid.to_string());
            s.set_total_payments_paid(summary.total_payments_paid.to_string());
            s.set_actual_term_months(summary.actual_term_months);
        }
        return Status::OK;
    }

    auto ComputeHeloc(ServerContext* context, const sensen::finance::HelocRequest* request,
                      sensen::finance::HelocResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeHeloc", quota::cost_default());
        if (request->payments_per_year() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "payments_per_year must be positive (12 monthly, 26 bi-weekly)");
        }
        if (request->repayment_term_years() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "repayment_term_years must be positive");
        }
        READ_DECIMAL(home, request->home_value(), "home_value");
        READ_DECIMAL(balance, request->current_mortgage_balance(), "current_mortgage_balance");
        READ_DECIMAL(ltv, request->max_ltv_rate(), "max_ltv_rate");
        READ_DECIMAL(drawn, request->drawn_amount(), "drawn_amount");
        READ_DECIMAL(rate, request->annual_rate(), "annual_rate");

        const auto s = sensen::calculate_heloc_metrics(home, balance, ltv, drawn, rate,
                                                       request->repayment_term_years(),
                                                       request->payments_per_year());
        response->set_available_equity(s.available_equity.to_string());
        response->set_draw_period_payment(s.draw_period_payment.to_string());
        response->set_repayment_period_payment(s.repayment_period_payment.to_string());
        return Status::OK;
    }

    // -- Cash flow -----------------------------------------------------------

    auto ComputeNpv(ServerContext* context, const sensen::finance::NpvRequest* request,
                    sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeNpv", quota::cost_cash_flow(request->values_size()));
        const std::vector<double> values(request->values().begin(), request->values().end());
        response->set_value(sensen::npv_double(request->rate(), values));
        return Status::OK;
    }

    auto ComputeIrr(ServerContext* context, const sensen::finance::IrrRequest* request,
                    sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeIrr", quota::cost_cash_flow(request->values_size()));
        const std::vector<double> values(request->values().begin(), request->values().end());
        const auto r = (request->guess() == 0.0) ? sensen::irr(values)
                                                 : sensen::irr(values, request->guess());
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputeXnpv(ServerContext* context, const sensen::finance::DatedCashFlowRequest* request,
                     sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeXnpv", quota::cost_cash_flow(request->values_size()));
        if (auto s = check_dated(request); !s.ok()) return s;
        const std::vector<double> values(request->values().begin(), request->values().end());
        const std::vector<double> dates(request->dates().begin(), request->dates().end());
        const auto r = sensen::xnpv(request->rate(), values, dates);
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputeXirr(ServerContext* context, const sensen::finance::DatedCashFlowRequest* request,
                     sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeXirr", quota::cost_cash_flow(request->values_size()));
        if (auto s = check_dated(request); !s.ok()) return s;
        const std::vector<double> values(request->values().begin(), request->values().end());
        const std::vector<double> dates(request->dates().begin(), request->dates().end());
        const auto r = (request->guess() == 0.0) ? sensen::xirr(values, dates)
                                                 : sensen::xirr(values, dates, request->guess());
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputePaybackPeriod(ServerContext* context, const sensen::finance::PaybackRequest* request,
                              sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePaybackPeriod", quota::cost_cash_flow(request->values_size()));
        const std::vector<double> values(request->values().begin(), request->values().end());
        const auto r = request->discounted()
                           ? sensen::discounted_payback_period(request->rate(), values)
                           : sensen::payback_period(values);
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputeCumulative(ServerContext* context, const sensen::finance::CumulativeRequest* request,
                           sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeCumulative", quota::cost_amortization(request->periods()));
        const BigDecimal rate{request->rate()};
        const BigDecimal pv_v{request->present_value()};
        const auto r =
            (request->component() == sensen::finance::CumulativeRequest::PRINCIPAL)
                ? sensen::cumprinc(rate, request->periods(), pv_v, request->start_period(),
                                   request->end_period(), timing_of(request->timing()))
                : sensen::cumipmt(rate, request->periods(), pv_v, request->start_period(),
                                  request->end_period(), timing_of(request->timing()));
        response->set_value(r.to_double());
        return Status::OK;
    }

    // -- Depreciation --------------------------------------------------------

    auto ComputeDepreciation(ServerContext* context, const sensen::finance::DepreciationRequest* request,
                             sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeDepreciation", quota::cost_default());
        double v = 0.0;
        switch (request->method()) {
            case sensen::finance::DepreciationRequest::STRAIGHT_LINE:
                if (request->life() <= 0.0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "life must be positive");
                }
                v = sensen::sln(request->cost(), request->salvage(), request->life());
                break;
            case sensen::finance::DepreciationRequest::SUM_OF_YEARS_DIGITS:
                if (request->life() <= 0.0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "life must be positive");
                }
                v = sensen::syd(request->cost(), request->salvage(), request->life(),
                                request->period());
                break;
            case sensen::finance::DepreciationRequest::DECLINING_BALANCE: {
                if (request->life() <= 0.0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "life must be positive");
                }
                // 2.0 is the double-declining convention. Unlike a compounding
                // frequency, an unstated factor has one overwhelmingly standard
                // value, and the field name says which.
                const double factor = (request->factor() > 0.0) ? request->factor() : 2.0;
                v = sensen::ddb(request->cost(), request->salvage(), request->life(),
                                request->period(), factor);
                break;
            }
            case sensen::finance::DepreciationRequest::MACRS:
                if (request->recovery_period() <= 0 || request->year() <= 0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT,
                                  "MACRS needs a positive recovery_period and year");
                }
                v = sensen::macrs(request->cost(), request->recovery_period(), request->year());
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT, "unknown depreciation method");
        }
        response->set_value(v);
        return Status::OK;
    }

    // -- Fixed income --------------------------------------------------------

    auto AnalyzeBond(ServerContext* context, const sensen::finance::BondRequest* request,
                     sensen::finance::BondResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("AnalyzeBond", quota::cost_default());
        if (request->frequency() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "frequency must be positive (2 = semi-annual)");
        }
        if (request->years_to_maturity() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "years_to_maturity must be positive");
        }
        if (request->par() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "par must be positive");
        }
        // Redemption is quoted as a percent of par; 100 means redeemed at par.
        const double redemption = (request->redemption() > 0.0) ? request->redemption() : 100.0;

        double yld = 0.0;
        double price = 0.0;
        switch (request->known_case()) {
            case sensen::finance::BondRequest::kYield:
                yld = request->yield();
                price = sensen::price_bond(request->par(), request->coupon_rate(), yld,
                                           request->frequency(), request->years_to_maturity(),
                                           redemption);
                break;
            case sensen::finance::BondRequest::kPrice: {
                price = request->price();
                const double guess =
                    (request->yield_guess() != 0.0) ? request->yield_guess() : 0.05;
                const auto y = sensen::yield_bond(request->par(), request->coupon_rate(), price,
                                                  request->frequency(),
                                                  request->years_to_maturity(), redemption, guess);
                if (!y) return fail(y);
                yld = *y;
                break;
            }
            default:
                // Neither supplied. There is no bond figure that can be derived
                // from the remaining parameters alone, so this is a request
                // that cannot be answered rather than one worth guessing at.
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "supply either yield or price; the other is derived from it");
        }

        response->set_price(price);
        response->set_yield(yld);
        response->set_macaulay_duration(sensen::duration_bond(
            request->par(), request->coupon_rate(), yld, request->frequency(),
            request->years_to_maturity(), redemption));
        response->set_convexity(sensen::convexity_bond(
            request->par(), request->coupon_rate(), yld, request->frequency(),
            request->years_to_maturity(), redemption));
        return Status::OK;
    }

    auto AnalyzeTreasuryBill(ServerContext* context, const sensen::finance::TreasuryBillRequest* request,
                             sensen::finance::TreasuryBillResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("AnalyzeTreasuryBill", quota::cost_default());
        if (request->days_to_maturity() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "days_to_maturity must be positive");
        }
        if (request->face_value() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "face_value must be positive");
        }

        double price = 0.0;
        switch (request->known_case()) {
            case sensen::finance::TreasuryBillRequest::kDiscountRate:
                price = sensen::price_tbill(request->face_value(), request->discount_rate(),
                                            request->days_to_maturity());
                break;
            case sensen::finance::TreasuryBillRequest::kPrice:
                price = request->price();
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "supply either discount_rate or price");
        }
        if (price <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "price must be positive");
        }

        response->set_price(price);
        response->set_bond_equivalent_yield(
            sensen::yield_tbill_bey(price, request->face_value(), request->days_to_maturity()));
        response->set_money_market_yield(
            sensen::yield_tbill_mmy(price, request->face_value(), request->days_to_maturity()));
        response->set_bank_discount_yield(
            sensen::yield_tbill_bdy(price, request->face_value(), request->days_to_maturity()));
        return Status::OK;
    }

    // -- Futures -------------------------------------------------------------

    auto PriceFutures(ServerContext* context, const sensen::finance::FuturesPricingRequest* request,
                      sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceFutures", quota::cost_default());
        sensen::FuturesPricingParams p{};
        p.spot = request->spot();
        p.rate = request->rate();
        p.cost_of_carry = request->cost_of_carry();
        p.years_to_maturity = request->years_to_maturity();
        p.continuous = request->continuous();
        response->set_value(sensen::price_futures(p));
        return Status::OK;
    }

    auto ValueFutures(ServerContext* context, const sensen::finance::FuturesValuationRequest* request,
                      sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ValueFutures", quota::cost_default());
        response->set_value(sensen::value_futures(request->current_spot(), request->delivery_price(),
                                                  request->rate(), request->years_to_maturity(),
                                                  request->is_long()));
        return Status::OK;
    }

    auto SimulateMarginAccount(ServerContext* context, const sensen::finance::MarginSimulationRequest* request,
                               sensen::finance::MarginSimulationResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("SimulateMarginAccount",
               quota::cost_margin_simulation(request->daily_prices_size()));
        if (request->contract_size() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "contract_size must be positive");
        }
        const std::vector<double> path(request->daily_prices().begin(),
                                       request->daily_prices().end());
        const auto s = sensen::simulate_futures_margin_account(
            request->initial_deposit(), request->initial_margin_requirement(),
            request->maintenance_margin_requirement(), request->contract_size(),
            request->entry_price(), path, request->is_long());
        response->set_balance(s.balance);
        response->set_initial_margin(s.initial_margin);
        response->set_maintenance_margin(s.maintenance_margin);
        response->set_contract_size(s.contract_size);
        response->set_margin_call(s.margin_call);
        return Status::OK;
    }

    auto ComputeHedge(ServerContext* context, const sensen::finance::HedgeRequest* request,
                      sensen::finance::HedgeResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeHedge", quota::cost_default());
        if (request->futures_volatility() == 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "futures_volatility cannot be zero; the hedge ratio divides by it");
        }
        const double ratio = sensen::calculate_hedge_ratio(
            request->asset_volatility(), request->futures_volatility(), request->correlation());
        response->set_hedge_ratio(ratio);

        // A contract count needs a position size and a contract spec. Without
        // them the count is left absent and SAID to be absent, rather than
        // returned as a zero that reads like "no contracts needed".
        const bool have_position = request->spot_value() != 0.0 &&
                                   request->contract_multiplier() != 0.0 &&
                                   request->futures_price() != 0.0;
        if (have_position) {
            response->set_contracts(sensen::calculate_hedge_contracts(
                request->spot_value(), request->contract_multiplier(), request->futures_price(),
                ratio));
            response->set_contracts_computed(true);
        }
        return Status::OK;
    }

    auto ComputeCommoditySpread(ServerContext* context,
                                const sensen::finance::CommoditySpreadRequest* request,
                                sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeCommoditySpread", quota::cost_default());
        double v = 0.0;
        switch (request->spread()) {
            case sensen::finance::CommoditySpreadRequest::CRACK_321:
                v = sensen::calculate_crack_spread_321(request->a(), request->b(), request->c());
                break;
            case sensen::finance::CommoditySpreadRequest::SPARK:
                v = sensen::calculate_spark_spread(request->a(), request->b(), request->c());
                break;
            case sensen::finance::CommoditySpreadRequest::CRUSH:
                v = sensen::calculate_crush_spread(request->a(), request->b(), request->c());
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT, "unknown spread");
        }
        response->set_value(v);
        return Status::OK;
    }

    // -- Real estate ---------------------------------------------------------

    auto ComputeRentalRoi(ServerContext* context, const sensen::finance::RentalRoiRequest* request,
                          sensen::finance::RentalRoiResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeRentalRoi", quota::cost_default());
        if (request->periods_per_year() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "periods_per_year must be positive (12 monthly, 26 bi-weekly)");
        }
        READ_DECIMAL(value, request->property_value(), "property_value");
        READ_DECIMAL(cash, request->total_cash_invested(), "total_cash_invested");
        READ_DECIMAL(rent, request->periodic_gross_rent(), "periodic_gross_rent");
        READ_DECIMAL(opex, request->periodic_operating_expenses(), "periodic_operating_expenses");
        READ_DECIMAL(debt, request->periodic_mortgage_payment(), "periodic_mortgage_payment");

        const auto s = sensen::calculate_rental_roi(value, cash, rent, opex, debt,
                                                    request->periods_per_year());
        response->set_net_operating_income(s.net_operating_income.to_string());
        response->set_annual_cash_flow(s.annual_cash_flow.to_string());
        response->set_cash_on_cash_return(s.cash_on_cash_return.to_string());
        response->set_cap_rate(s.cap_rate.to_string());
        response->set_gross_rent_multiplier(s.gross_rent_multiplier.to_string());
        return Status::OK;
    }

    // -- Options -------------------------------------------------------------

    auto PriceOptionTree(ServerContext* context, const sensen::finance::OptionTreeRequest* request,
                         sensen::finance::OptionPricingResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceOptionTree",
               quota::cost_option_tree(request->steps(), request->averaging_states()));
        // price_option_double THROWS on these rather than returning an error,
        // so they are checked here and reported as the caller's mistake they
        // are, not as an internal fault.
        if (request->steps() <= 0 || request->years_to_expiry() <= 0.0 || request->spot() <= 0.0 ||
            request->strike() <= 0.0 || request->volatility() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "steps, years_to_expiry, spot, strike and volatility must all be positive");
        }
        const auto exercise = request->exercise_type();
        if (exercise == sensen::finance::BERMUDAN && request->bermudan_dates_size() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "a Bermudan option needs bermudan_dates; without them it is European");
        }

        sensen::ExerciseType ex = sensen::ExerciseType::European;
        if (exercise == sensen::finance::AMERICAN) ex = sensen::ExerciseType::American;
        if (exercise == sensen::finance::BERMUDAN) ex = sensen::ExerciseType::Bermudan;

        const std::vector<double> dates(request->bermudan_dates().begin(),
                                        request->bermudan_dates().end());
        const int avg_states =
            (request->averaging_states() > 0) ? request->averaging_states() : 50;
        // sqrt(1.5), the spacing that makes the middle branch probability 1/3.
        const double lambda =
            (request->lambda() > 0.0) ? request->lambda() : 1.224744871391589;

        try {
            const auto r = sensen::price_option_double(
                request->spot(), request->strike(), request->rate(), request->volatility(),
                request->years_to_expiry(), request->steps(), option_type_of(request->option_type()),
                ex, dates, asian_type_of(request->asian_type()), avg_states, lambda);
            response->set_value(r.value);
            response->set_delta(r.delta);
            response->set_gamma(r.gamma);
            response->set_theta(r.theta);
            return Status::OK;
        } catch (const std::exception& e) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
        }
    }

    auto PriceBlackScholes(ServerContext* context, const sensen::finance::BlackScholesRequest* request,
                           sensen::finance::BlackScholesResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceBlackScholes", quota::cost_default());
        const auto r = sensen::price_black_scholes(
            request->spot(), request->strike(), request->rate(), request->volatility(),
            request->years_to_expiry(), option_type_of(request->option_type()));
        response->set_value(r.value);
        response->set_delta(r.delta);
        response->set_gamma(r.gamma);
        response->set_theta(r.theta);
        response->set_vega(r.vega);
        response->set_rho(r.rho);
        response->set_vanna(r.vanna);
        response->set_volga(r.volga);
        response->set_charm(r.charm);
        response->set_color(r.color);
        response->set_speed(r.speed);
        return Status::OK;
    }

    auto PriceOptionMonteCarlo(ServerContext* context, const sensen::finance::MonteCarloRequest* request,
                               sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceOptionMonteCarlo",
               quota::cost_monte_carlo(request->paths(), request->steps()));
        if (request->paths() <= 0 || request->steps() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "paths and steps must be positive");
        }
        const int threads = (request->num_threads() > 0) ? request->num_threads() : -1;
        response->set_value(sensen::price_option_monte_carlo(
            request->spot(), request->strike(), request->rate(), request->volatility(),
            request->years_to_expiry(), request->paths(), request->steps(),
            option_type_of(request->option_type()), asian_type_of(request->asian_type()), threads));
        return Status::OK;
    }

    auto ComputeProbabilityTree(ServerContext* context, const sensen::finance::ProbabilityTreeRequest* request,
                                sensen::finance::ProbabilityTreeResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeProbabilityTree", quota::cost_probability_tree(request->steps()));
        if (request->steps() <= 0 || request->years_to_expiry() <= 0.0 ||
            request->volatility() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "steps, years_to_expiry and volatility must be positive");
        }
        const double lambda =
            (request->lambda() > 0.0) ? request->lambda() : 1.224744871391589;
        const auto t = sensen::calculate_probability_tree(
            request->rate(), request->volatility(), request->years_to_expiry(), request->steps(),
            lambda);
        for (const double p : t.stock_prices) response->add_stock_prices(p);
        for (const double p : t.state_probabilities) response->add_state_probabilities(p);
        response->set_steps(t.steps);
        return Status::OK;
    }

    // -- Portfolio -----------------------------------------------------------

    auto ComputePortfolioStats(ServerContext* context, const sensen::finance::PortfolioStatsRequest* request,
                               sensen::finance::PortfolioStatsResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePortfolioStats",
               quota::cost_portfolio_stats(request->portfolio_returns_size()));
        if (request->portfolio_returns_size() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "portfolio_returns is empty");
        }
        const bool benchmarked = request->market_returns_size() > 0;
        if (benchmarked && request->market_returns_size() != request->portfolio_returns_size()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "market_returns has " + std::to_string(request->market_returns_size()) +
                              " entries but portfolio_returns has " +
                              std::to_string(request->portfolio_returns_size()) +
                              "; a benchmark must cover the same periods");
        }

        const std::vector<double> pr(request->portfolio_returns().begin(),
                                     request->portfolio_returns().end());
        const std::vector<double> mr(request->market_returns().begin(),
                                     request->market_returns().end());
        const auto s = sensen::calculate_portfolio_stats(pr, mr, request->risk_free_rate());

        response->set_sharpe_ratio(s.sharpe_ratio);
        response->set_sortino_ratio(s.sortino_ratio);
        response->set_treynor_ratio(s.treynor_ratio);
        response->set_beta(s.beta);
        response->set_alpha(s.alpha);
        response->set_max_drawdown(s.max_drawdown);
        response->set_var_historical_95(s.var_historical_95);
        response->set_var_historical_99(s.var_historical_99);
        response->set_cvar_historical_95(s.cvar_historical_95);
        response->set_cvar_historical_99(s.cvar_historical_99);
        response->set_var_parametric_95(s.var_parametric_95);
        response->set_var_parametric_99(s.var_parametric_99);
        response->set_cvar_parametric_95(s.cvar_parametric_95);
        response->set_cvar_parametric_99(s.cvar_parametric_99);
        response->set_omega_ratio(s.omega_ratio);
        response->set_calmar_ratio(s.calmar_ratio);
        response->set_information_ratio(s.information_ratio);
        response->set_tracking_error(s.tracking_error);
        // Lets a caller tell an absent beta from a measured beta of zero.
        response->set_benchmark_supplied(benchmarked);
        return Status::OK;
    }

    auto OptimizePortfolio(ServerContext* context, const sensen::finance::PortfolioOptimizeRequest* request,
                           sensen::finance::PortfolioOptimizeResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("OptimizePortfolio", quota::cost_portfolio_optimize(request->size()));
        sensen::MatrixT<double> cov;
        if (auto s = read_covariance(request->size(), request->expected_returns_size(),
                                     request->covariance(), cov);
            !s.ok()) {
            return s;
        }
        const std::vector<double> mu(request->expected_returns().begin(),
                                     request->expected_returns().end());
        const auto r = sensen::optimize_portfolio(mu, cov, request->risk_free_rate(),
                                                  request->max_sharpe());
        if (!r) return fail(r);
        for (const double w : r->weights) response->add_weights(w);
        response->set_expected_return(r->expected_return);
        response->set_volatility(r->volatility);
        response->set_sharpe_ratio(r->sharpe_ratio);
        return Status::OK;
    }

    auto ComputeRiskContributions(ServerContext* context,
                                  const sensen::finance::RiskContributionRequest* request,
                                  sensen::finance::RiskContributionResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // Same O(n^3) covariance solve as OptimizePortfolio, so the same price.
        CHARGE("ComputeRiskContributions", quota::cost_portfolio_optimize(request->size()));
        sensen::MatrixT<double> cov;
        if (auto s = read_covariance(request->size(), request->weights_size(), request->covariance(),
                                     cov);
            !s.ok()) {
            return s;
        }
        const std::vector<double> w(request->weights().begin(), request->weights().end());
        const auto r = sensen::calculate_risk_contributions(w, cov);
        if (!r) return fail(r);
        for (const double c : *r) response->add_contributions(c);
        return Status::OK;
    }

  private:
    [[nodiscard]] static auto check_term(int term_months) -> Status {
        if (term_months <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "term_months must be positive");
        }
        // A schedule is one row per period and the engine reserves that many up
        // front. Without a ceiling a caller could ask for a term long enough to
        // exhaust memory on the server, which is a denial of service dressed as
        // a mortgage. 1200 months is a hundred years -- beyond any real loan.
        if (term_months > 1200) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "term_months exceeds 1200 (a hundred years); no real loan runs that long");
        }
        return Status::OK;
    }

    [[nodiscard]] static auto check_dated(const sensen::finance::DatedCashFlowRequest* request)
        -> Status {
        if (request->values_size() != request->dates_size()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "values has " + std::to_string(request->values_size()) +
                              " entries but dates has " + std::to_string(request->dates_size()) +
                              "; every cash flow needs its own date");
        }
        if (request->values_size() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "no cash flows supplied");
        }
        return Status::OK;
    }

    /**
     * Reads a square covariance matrix sent flat, row-major.
     *
     * Refuses a size that disagrees with the vector it must multiply, and a
     * flat array whose length is not size*size. Reshaping either to fit would
     * produce a number rather than an answer.
     */
    template <typename Repeated>
    [[nodiscard]] static auto read_covariance(int size, int vector_len, const Repeated& flat,
                                              sensen::MatrixT<double>& out) -> Status {
        if (size <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "size must be positive");
        }
        if (vector_len != size) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "size is " + std::to_string(size) + " but the accompanying vector has " +
                              std::to_string(vector_len) + " entries");
        }
        const long long expected = static_cast<long long>(size) * size;
        if (static_cast<long long>(flat.size()) != expected) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "covariance must hold size*size = " + std::to_string(expected) +
                              " entries, got " + std::to_string(flat.size()));
        }
        out = sensen::MatrixT<double>(static_cast<std::size_t>(size),
                                      static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                out(static_cast<std::size_t>(i), static_cast<std::size_t>(j)) =
                    flat.Get(i * size + j);
            }
        }
        return Status::OK;
    }

    [[nodiscard]] auto period_payment(const sensen::finance::PeriodPaymentRequest* request,
                                      sensen::finance::DecimalResponse* response, bool interest)
        -> Status {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // No charge here: this is the shared body of ComputeInterestPayment
        // and ComputePrincipalPayment, and both charge at their own entry
        // point. Charging again would bill one call twice.
        READ_DECIMAL(rate, request->rate(), "rate");
        READ_DECIMAL(pv_v, request->present_value(), "present_value");
        READ_DECIMAL(fv_v, request->future_value(), "future_value");
        const int timing = timing_of(request->timing());
        const auto r = interest ? sensen::ipmt(rate, request->period(), request->periods(), pv_v,
                                               fv_v, timing)
                                : sensen::ppmt(rate, request->period(), request->periods(), pv_v,
                                               fv_v, timing);
        response->set_value(r.to_string());
        return Status::OK;
    }
};

}  // namespace

auto RegisterFinanceService(grpc::ServerBuilder& builder) -> void {
    // Static storage duration for the same reason the calculator service uses
    // it: gRPC's RegisterService takes the address and does not take ownership,
    // so the service must outlive both the builder and the server.
    static FinanceServiceImpl service;
    builder.RegisterService(&service);
    logger::Logger::getInstance().info("Registered {} on the same port as the calculator",
                                       sensen::finance::Finance::service_full_name());

    // Force the enforcer to load its policy NOW rather than on the first RPC.
    // Otherwise "are quotas on?" is a question only traffic can answer, and a
    // policy that failed to parse stays silent until the moment it matters.
    const bool on = quota::QuotaEnforcer::instance().enabled();
    logger::Logger::getInstance().info(
        "Quota enforcement is {}", on ? "ON" : "OFF (set QUOTA_POLICY to enable)");
}

}  // namespace options_calculator::finance
