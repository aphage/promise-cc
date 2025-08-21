#include <exception>
#include <future>
#include <memory>
#include <optional>
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

    usePromise<int>([](auto resolve, auto reject) {
        resolve(42);
    }, std::make_shared<ExecutorAsync>())->then([&](auto v) -> int {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    })->then([&](auto v) -> std::optional<int> {
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

    usePromise<int>([](auto resolve, auto reject) {
        resolve(42);
    }, std::make_shared<ExecutorSync>())->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("catch") {

    std::promise<int> p;
    auto f = p.get_future();
    std::string s = "hello";

    auto executor = new auto([s] (std::function<void()> f) { f(); });

    usePromise<int>(
        [](auto resolve, auto reject) {
            throw std::runtime_error("error");
        },
        std::shared_ptr<std::remove_pointer_t<decltype(executor)>>(executor)
    )->then([&](const auto& v) {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    })->catch_err([](auto e) {
        SPDLOG_INFO("rejected with {}", e.what());

        // resume
        return 42 * 2;
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("resolve") {
    std::promise<int> p;
    auto f = p.get_future();

    useResolve<int>(42, std::make_shared<ExecutorSync>())->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        return v * 2;
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
    }, [&] (auto e) {
        SPDLOG_INFO("rejected with {}", e.what());
        std::rethrow_exception(e);
    })->then([]() {

    })->then([&]() {
        
        return std::string("hello");
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);
    });

    REQUIRE(f.get() == 42 * 2);
}

TEST_CASE("reject") {
    std::promise<int> p;
    auto f = p.get_future();

    auto v = 42;

    useReject<int>(std::make_exception_ptr(std::runtime_error("error")),
        std::make_shared<ExecutorSync>())
    ->catch_err([&](auto e) -> int {

        // resume
        return v * 2;
    })->then([&](auto v) {
        SPDLOG_INFO("resolved with {}", v);

        p.set_value(v);
    }, [&] (auto e) {
        SPDLOG_INFO("rejected with {}", e.what());
        std::rethrow_exception(e);
    });

    REQUIRE(f.get() == 42 * 2);
}