#include <future>
#include <memory>
#include <optional>
#include <thread>

#include <promise/promise.h>

#include <catch2/catch_test_macros.hpp>

#if __has_include(<spdlog/spdlog.h>)
    #include <spdlog/spdlog.h>
#else
    #define SPDLOG_DEBUG(...)
    #define SPDLOG_INFO(...)
    #define SPDLOG_WARN(...)
    #define SPDLOG_ERROR(...)
#endif


using promise::Promise;

TEST_CASE("Sample test") {
    REQUIRE(1 + 1 == 2);
}

struct Executor {
    template<typename F>
    void operator()(F f) {
        f();
    }
};

struct Async {
    template<typename F>
    void operator()(F f) {
        std::thread{f}.detach();
    }
};

TEST_CASE("Promise") {
    std::promise<int> p;
    auto f = p.get_future();

    std::make_shared<Promise<int, Executor>>([](auto resolve, auto reject) {
        resolve(42);
    })->then<int>([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    })->then<std::optional<bool>>([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);

        return std::nullopt;
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("Promise2") {
    std::promise<int> p;
    auto f = p.get_future();

    std::make_shared<Promise<int, Async>>([](auto resolve, auto reject) {
        resolve(42);
    })->then<int>([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
    });

    REQUIRE(f.get() == 42 * 2);
}