module;

#include <cpr/cpr.h>
#include <string>
#include <system_error>

export module market_data;

import std;
import fastjson;

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
    std::string url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + symbol;
    
    // Perform synchronous HTTP GET using CPR
    cpr::Response r = cpr::Get(cpr::Url{url}, 
                               cpr::Header{{"User-Agent", "OptionsFuturesCalculatorEngine/1.0"}});

    if (r.error) {
        return std::unexpected(make_error_code(MarketDataError::NetworkError));
    }
    
    if (r.status_code != 200) {
        return std::unexpected(make_error_code(MarketDataError::HttpError));
    }

    try {
        // fastjson parsing
        auto parsed = fastjson::parse(r.text);
        if (parsed.is_null() || !parsed.has("quoteResponse") || !parsed["quoteResponse"].has("result")) {
            return std::unexpected(make_error_code(MarketDataError::MissingData));
        }
        
        auto result_arr = parsed["quoteResponse"]["result"];
        if (result_arr.size() == 0) {
            return std::unexpected(make_error_code(MarketDataError::MissingData));
        }

        auto result = result_arr[0];
        
        YahooFinanceQuote quote;
        quote.symbol = symbol;
        
        if (result.has("regularMarketPrice")) {
            quote.regularMarketPrice = result["regularMarketPrice"].as_double();
        } else {
            return std::unexpected(make_error_code(MarketDataError::MissingData));
        }

        if (result.has("regularMarketPreviousClose")) {
            quote.regularMarketPreviousClose = result["regularMarketPreviousClose"].as_double();
        } else {
            quote.regularMarketPreviousClose = quote.regularMarketPrice;
        }

        if (result.has("forwardPE")) {
            quote.forwardPE = result["forwardPE"].as_double();
        } else {
            quote.forwardPE = 0.0;
        }

        // Standard quote API doesn't usually expose IV directly, stubbing it to 20% for now
        quote.impliedVolatility = 0.20; 

        return quote;
    } catch (...) {
        // fastjson usually throws std::runtime_error on parse failure
        return std::unexpected(make_error_code(MarketDataError::ParseError));
    }
}

} // namespace options_calculator::market_data

namespace std {
    template <>
    struct is_error_code_enum<options_calculator::market_data::MarketDataError> : true_type {};
}
