module;

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <concepts>
#include <format>

export module testing_framework;

import logger;

export namespace testing {

    struct AssertionError : public std::runtime_error {
        explicit AssertionError(const std::string& msg) : std::runtime_error(msg) {}
    };

    template<typename T>
    class Expectation {
        T value_;
        std::string_view name_;
        
    public:
        explicit Expectation(T val, std::string_view name = "") 
            : value_(std::move(val)), name_(name) {}

        auto toBe(const T& expected) -> Expectation& {
            if (value_ != expected) {
                std::string msg = std::format("Assertion failed: Expected [{}] to be [{}], but got [{}]", 
                                              name_.empty() ? "value" : name_, expected, value_);
                throw AssertionError(msg);
            }
            return *this;
        }

        auto toNotBe(const T& expected) -> Expectation& {
            if (value_ == expected) {
                std::string msg = std::format("Assertion failed: Expected [{}] to NOT be [{}]", 
                                              name_.empty() ? "value" : name_, expected);
                throw AssertionError(msg);
            }
            return *this;
        }
        
        auto toSatisfy(std::function<bool(const T&)> predicate, std::string_view conditionName = "custom condition") -> Expectation& {
            if (!predicate(value_)) {
                std::string msg = std::format("Assertion failed: [{}] did not satisfy [{}]", 
                                              name_.empty() ? "value" : name_, conditionName);
                throw AssertionError(msg);
            }
            return *this;
        }
    };

    template<typename T>
    auto expect(T val, std::string_view name = "") -> Expectation<T> {
        return Expectation<T>(std::move(val), name);
    }

    class TestCase {
        std::string name_;
        std::function<void()> func_;
    public:
        TestCase(std::string name, std::function<void()> func) 
            : name_(std::move(name)), func_(std::move(func)) {}

        [[nodiscard]] auto name() const -> std::string_view { return name_; }
        
        auto run() const -> bool {
            auto& log = logger::Logger::getInstance();
            try {
                func_();
                return true;
            } catch (const AssertionError& e) {
                log.error("    Assertion Error in '{}': {}", name_, e.what());
                return false;
            } catch (const std::exception& e) {
                log.error("    Exception in '{}': {}", name_, e.what());
                return false;
            } catch (...) {
                log.error("    Unknown Exception in '{}'", name_);
                return false;
            }
        }
    };

    class TestSuite {
        std::string name_;
        std::vector<TestCase> tests_;
    public:
        explicit TestSuite(std::string name) : name_(std::move(name)) {}
        
        // Fluent API for adding tests
        auto it(std::string test_name, std::function<void()> func) -> TestSuite& {
            tests_.emplace_back(std::move(test_name), std::move(func));
            return *this;
        }

        auto run() const -> bool {
            auto& log = logger::Logger::getInstance();
            log.info("=========================================");
            log.info("Running Test Suite: {}", name_);
            log.info("=========================================");
            
            std::size_t passed = 0;
            std::size_t failed = 0;
            
            for (const auto& test : tests_) {
                if (test.run()) {
                    passed++;
                    log.info("  [✓] {}", test.name());
                } else {
                    failed++;
                    log.error("  [✗] {}", test.name());
                }
            }
            
            log.info("-----------------------------------------");
            log.info("Suite '{}' Results: {} passed, {} failed, {} total", 
                     name_, passed, failed, tests_.size());
            log.info("=========================================");
            
            return failed == 0;
        }
    };
    
    // Entry point for describing a suite
    auto describe(std::string name) -> TestSuite {
        return TestSuite(std::move(name));
    }
}
