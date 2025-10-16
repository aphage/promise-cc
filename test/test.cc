#include <exception>
#include <future>
#include <string>
#include <thread>
#include <utility>

#include <promise/promise.hpp>

#include <catch2/catch_test_macros.hpp>

#if __has_include(<spdlog/spdlog.h>)
    #include <spdlog/spdlog.h>
#else
    #define SPDLOG_DEBUG(...)
    #define SPDLOG_INFO(...)
    #define SPDLOG_WARN(...)
    #define SPDLOG_ERROR(...)
#endif

using promise::usePromise;
using promise::useResolve;
using promise::useReject;
using promise::usePromiseEx;

TEST_CASE("Sample test") {
    REQUIRE(1 + 1 == 2);
}

struct ExecutorSync {
    template<typename F, typename... Args>
    inline void operator()(F f, Args... args) {
        f(args...);
    }
};

struct ExecutorAsync {
    template<typename F, typename... Args>
    inline void operator()(F f, Args... args) {
        std::thread(std::forward<F>(f), std::forward<Args>(args)...).detach();
    }
};

TEST_CASE("Promise") {
    std::promise<int> p;
    auto f = p.get_future();

    usePromise<int>(
        [](auto resolve, auto reject) {
            resolve(42);
        },
        ExecutorAsync()
    ).then([&](auto v) -> int {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    }).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
        return true;
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("Promise2") {
    std::promise<std::string> p;
    auto f = p.get_future();

    usePromise<std::string>(
        [](auto resolve, auto reject) {
            resolve("hello");
        }, 
        ExecutorSync()
    ).then([&](const auto& v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
        return true;
    });

    REQUIRE(f.get() == "hello");
}

TEST_CASE("catch") {

    std::promise<int> p;
    auto f = p.get_future();
    std::string s = "hello";

    auto executor = [s] (std::function<void()> f) { f(); };

    usePromise<int>(
        [](auto resolve, auto reject) {
            throw std::runtime_error("error");
        },
        executor
    ).catch_err([](auto e) {
        SPDLOG_INFO("rejected with {}", e.what());

        // resume
        return 42 * 2;
    }).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
        return true;
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("finally") {
    std::promise<int> p;
    auto f = p.get_future();

    usePromise<int>(
        [](auto resolve, auto reject) {
            resolve(42);
        },
        ExecutorSync()
    ).finally([] {
        SPDLOG_INFO("finally");
    }).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
        return true;
    });

    REQUIRE(f.get() == 42);
}

TEST_CASE("resolve") {
    std::promise<int> p;
    auto f = p.get_future();

    useResolve<int>(
        42,
        ExecutorSync()
    ).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    }).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
        return true;
    }, [&] (auto e) -> bool {
        SPDLOG_INFO("rejected with {}", e.what());
        std::rethrow_exception(e);
    }).then([&](auto v) {
        
        return std::string("hello");
    }).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);
        return true;
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("reject") {
    std::promise<int> p;
    auto f = p.get_future();

    auto v = 42;

    useReject<int>(
        std::runtime_error("error"),
        ExecutorSync()
    ).catch_err([&](auto e) -> int {

        // resume
        return v * 2;
    }).then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
        return true;
    }, [&] (auto e) -> bool {
        SPDLOG_INFO("rejected with {}", e.what());
        std::rethrow_exception(e);
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("promise void") {
    std::promise<void> p;
    auto f = p.get_future();

    usePromiseEx<void, ExecutorAsync>(
        [](auto resolve, auto reject) {
            resolve();
        }
    ).then([&]() {
        SPDLOG_INFO("resolved with {}", v);
        p.set_value();
    });

    f.get();
}

TEST_CASE("is_promise") {
    auto v = usePromiseEx<void, ExecutorAsync>(
        [](auto resolve, auto reject) {
            resolve();
        }
    );
    static_assert(promise::internal::is_promise_v<decltype(v)>);
    static_assert(!promise::internal::is_promise_v<int>);
}