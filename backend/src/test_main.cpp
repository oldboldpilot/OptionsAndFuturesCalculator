#include <iostream>
import calculator.testing;
import market_data;

auto main() -> int {
    auto result = calculator::testing::run_all_tests();
    
    if (result.passed) {
        std::cout << result.message << std::endl;
        return 0;
    } else {
        std::cerr << result.message << std::endl;
        return 1;
    }
}
