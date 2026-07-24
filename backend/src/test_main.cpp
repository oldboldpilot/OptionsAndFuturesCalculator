import std;

import testing_framework;
import options_calculator::market_data;

auto main() -> int {
    auto suite = testing::describe("Market Data Module Tests")
        .it("should construct a YahooFinanceQuote correctly", []() -> std::expected<void, std::string> {
            options_calculator::market_data::YahooFinanceQuote quote;
            quote.symbol = "AAPL";
            quote.regularMarketPrice = 150.0;
            
            return testing::expect(quote.symbol, "quote symbol").toBe(std::string("AAPL"))
                .and_then([&]() { return testing::expect(quote.regularMarketPrice, "quote price").toBe(150.0); });
        })
        .it("should fetch real market data for MSFT", []() -> std::expected<void, std::string> {
            auto result = options_calculator::market_data::fetch_yahoo_finance_quote("MSFT");
            
            // Expected to succeed with a valid network request
            return testing::expect(result.has_value(), "has_value").toBe(true)
                .and_then([&]() { return testing::expect(result->symbol, "symbol").toBe(std::string("MSFT")); })
                .and_then([&]() { return testing::expect(result->regularMarketPrice > 0.0, "price > 0").toBe(true); });
        });

    bool success = suite.run();
    return success ? 0 : 1;
}
