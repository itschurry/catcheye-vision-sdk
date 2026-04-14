#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace test_support {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline void add_test(const std::string& name, std::function<void()> fn)
{
    registry().push_back(TestCase {name, std::move(fn)});
}

inline int run_all()
{
    int failures = 0;
    for (const auto& test : registry()) {
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cout << "[FAIL] " << test.name << ": " << ex.what() << '\n';
        }
    }
    return failures;
}

inline void assert_true(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace test_support

#define TEST_CASE(name) \
    static void name(); \
    namespace { \
    struct name##_registrar { \
        name##_registrar() { test_support::add_test(#name, name); } \
    } name##_registrar_instance; \
    } \
    static void name()
