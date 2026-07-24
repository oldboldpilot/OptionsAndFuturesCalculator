module;

#include <string>
#include <system_error>

export module market_data;

import std;
import fastjson;
import logger;

namespace options_calculator::market_data {

export enum class MarketDataError {
    NetworkError = 1,
    HttpError,
    ParseError,
    MissingData
};

const std::error_category& market_data_category() noexcept {
    struct category_impl : std::error_category {
        const char* name() const noexcept override { return "market_data"; }
        std::string message(int ev) const override {
            switch (static_cast<MarketDataError>(ev)) {
                case MarketDataError::NetworkError: return "Network connection failed";
                case MarketDataError::HttpError: return "HTTP response returned an error status code";
                case MarketDataError::ParseError: return "Failed to parse JSON response";
                case MarketDataError::MissingData: return "Requested data field is missing from the payload";
                default: return "Unknown market data error";
            }
        }
    };
    static const category_impl impl;
    return impl;
}

std::error_code make_error_code(MarketDataError e) {
    return {static_cast<int>(e), market_data_category()};
}

export struct YahooFinanceQuote {
    std::string symbol;
    double regularMarketPrice;
    double regularMarketPreviousClose;
    double forwardPE;
    double impliedVolatility; // We'll try to extract options IV if available, else standard historical
};

// C++23 std::expected Railway-Oriented Programming (ROP)
export auto fetch_yahoo_finance_quote(const std::string& symbol) -> std::expected<YahooFinanceQuote, std::error_code> {
    auto& log = logger::Logger::getInstance();
    log.info("Fetching market data for symbol: {}", symbol);

    // Removed cpr HTTP fetching to minimize external dependencies.
    // The market data fetching logic will be shifted to the Python layer 
    // using httpx/yfinance, which will then feed this C++ module via nanobind.
    
    // For now, return a simulated network error if called directly from C++
    // until the Python layer is fully wired up to inject the data.
    return std::unexpected(make_error_code(MarketDataError::NetworkError));
}

} // namespace options_calculator::market_data

namespace std {
    template <>
    struct is_error_code_enum<options_calculator::market_data::MarketDataError> : true_type {};
}
