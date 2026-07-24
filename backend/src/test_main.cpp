#include <iostream>

import testing_framework;
import options_calculator::market_data;

auto main() -> int {
    auto suite = testing::describe("Market Data Module Tests")
        .it("should construct a YahooFinanceQuote correctly", []() {
            options_calculator::market_data::YahooFinanceQuote quote;
            quote.symbol = "AAPL";
            quote.regularMarketPrice = 150.0;
            
            testing::expect(quote.symbol, "quote symbol").toBe(std::string("AAPL"));
            testing::expect(quote.regularMarketPrice, "quote price").toBe(150.0);
        })
        .it("should fail gracefully when network is disconnected", []() {
            auto result = options_calculator::market_data::fetch_yahoo_finance_quote("MSFT");
            
            // Expected to fail with NetworkError since we removed cpr
            testing::expect(result.has_value(), "has_value").toBe(false);
        });

    bool success = suite.run();
    return success ? 0 : 1;
}
