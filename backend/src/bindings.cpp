#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

namespace nb = nanobind;

import options_calculator::market_data;

NB_MODULE(options_futures_engine, m) {
    m.doc() = "Options and Futures Calculator Native Engine Bindings";

    // Since we're bridging C++23 modules with nanobind, we wrap our data types here
    nb::class_<options_calculator::market_data::YahooFinanceQuote>(m, "YahooFinanceQuote")
        .def(nb::init<>())
        .def_rw("symbol", &options_calculator::market_data::YahooFinanceQuote::symbol)
        .def_rw("regularMarketPrice", &options_calculator::market_data::YahooFinanceQuote::regularMarketPrice)
        .def_rw("regularMarketPreviousClose", &options_calculator::market_data::YahooFinanceQuote::regularMarketPreviousClose)
        .def_rw("forwardPE", &options_calculator::market_data::YahooFinanceQuote::forwardPE)
        .def_rw("impliedVolatility", &options_calculator::market_data::YahooFinanceQuote::impliedVolatility);
        
    // In a real application, we would bind the ComputeStrategyPnL gRPC server directly,
    // or expose the core PnL calculation functions for FastAPI to invoke.
}
