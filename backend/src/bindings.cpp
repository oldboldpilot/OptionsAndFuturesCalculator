/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Python bindings (nanobind) for the market-data surface of the engine.
 *
 * This is deliberately narrow. The two C++23 modules that make up the rest
 * of the engine's public surface are not candidates:
 *
 *   - `calculator_service` (module `calculator_service`) exports exactly two
 *     functions, both of the form `void(grpc::ServerBuilder&)` -- there is
 *     nothing here for Python to call that does not first require standing
 *     up a gRPC server, which defeats the point of an in-process binding.
 *   - `pricing_engine` (module `calculator.engine`) is test-only: it is not
 *     part of `calculator_engine`'s own FILE_SET, only `test_runner`'s, and
 *     its `calculate_strategy` is a simplified stand-in the production RPC
 *     path does not use (see calculator_service.cpp, which drives sensen's
 *     option pricers through an SGEE-built interpreter graph instead).
 *
 * `market_data` (module `market_data`, namespace
 * `options_calculator::market_data`) is neither: it is the same
 * Alpaca-backed quote/chain/risk-free-rate path the production gRPC service
 * calls, it depends on nothing beyond fastjson/logger/httplib/OpenSSL/sgee,
 * and its public functions are plain values in, `std::expected` out -- an
 * honest fit for a language boundary.
 *
 * A note on the type that used to be here: the previous version of this file
 * bound `options_calculator::market_data::YahooFinanceQuote`, a type that no
 * longer exists. It was the shape of a Yahoo Finance quote from a provider
 * this module replaced with Alpaca (see market_data.cppm's file banner) --
 * the binding was already dead before this file failed to compile for an
 * unrelated reason (`import options_calculator::market_data;`, which is not
 * valid C++23 module syntax; module names are dot-separated identifiers, not
 * namespace paths, and the two need not match -- this module's C++ namespace
 * is `options_calculator::market_data` but its module name is `market_data`).
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <stdexcept>
#include <string>

namespace nb = nanobind;

import market_data;

NB_MODULE(options_futures_engine, m) {
    m.doc() = "Options and Futures Calculator -- native market-data bindings "
              "(quotes and the risk-free rate curve), backed by the same "
              "Alpaca/Treasury path the gRPC service uses.";

    namespace md = options_calculator::market_data;

    nb::class_<md::Quote>(m, "Quote")
        .def(nb::init<>())
        .def_rw("symbol", &md::Quote::symbol)
        .def_rw("price", &md::Quote::price)
        .def_rw("previous_close", &md::Quote::previous_close)
        .def_rw("timestamp", &md::Quote::timestamp);

    nb::class_<md::RatePoint>(m, "RatePoint")
        .def(nb::init<>())
        .def_rw("tenor", &md::RatePoint::tenor)
        .def_rw("days", &md::RatePoint::days)
        .def_rw("rate_bey", &md::RatePoint::rate_bey)
        .def_rw("rate_continuous", &md::RatePoint::rate_continuous);

    nb::class_<md::RiskFreeRate>(m, "RiskFreeRate")
        .def(nb::init<>())
        .def_rw("rate", &md::RiskFreeRate::rate)
        .def_rw("rate_published", &md::RiskFreeRate::rate_published)
        .def_rw("tenor", &md::RiskFreeRate::tenor)
        .def_rw("as_of_date", &md::RiskFreeRate::as_of_date)
        .def_rw("source", &md::RiskFreeRate::source)
        .def_rw("fetched_at", &md::RiskFreeRate::fetched_at)
        .def_rw("curve", &md::RiskFreeRate::curve);

    m.def(
        "fetch_quote",
        [](const std::string& symbol) -> md::Quote {
            auto result = md::fetch_quote(symbol);
            if (!result) {
                throw std::runtime_error(result.error().message());
            }
            return *result;
        },
        nb::arg("symbol"),
        "Fetch a live equity quote through the same Alpaca-backed path the "
        "gRPC service uses. Raises RuntimeError with the engine's own error "
        "message (e.g. \"ALPACA_API_KEY / ALPACA_API_SECRET are not set\") "
        "on failure rather than returning an invented price.");

    m.def(
        "fetch_risk_free_rate",
        []() -> md::RiskFreeRate {
            auto result = md::fetch_risk_free_rate();
            if (!result) {
                throw std::runtime_error(result.error().message());
            }
            return *result;
        },
        "Fetch the current US Treasury par-yield risk-free rate curve. "
        "Keyless -- unlike fetch_quote, this works without ALPACA_* "
        "credentials configured.");
}
