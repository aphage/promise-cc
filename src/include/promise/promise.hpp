#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>
#include <exception>

namespace promise {

enum class PromiseState {
    PENDING,
    FULFILLED,
    REJECTED
};

struct SharedStateBase {
    std::mutex mtx;
    PromiseState state = PromiseState::PENDING;
    std::optional<std::exception_ptr> exception;
    std::vector<std::function<void()>> callbacks;

    inline void trigger_callbacks() {
        for (const auto& callback : callbacks) {
            callback();
        }
        callbacks.clear();
    }
};

template<typename T>
struct SharedState : SharedStateBase {
    std::optional<T> value;
};

template<>
struct SharedState<void> : SharedStateBase {
};

template<typename T, typename Executor>
class Promise {
private:
    std::shared_ptr<SharedState<T>> state_;
    std::shared_ptr<Executor> executor_;

    template<typename, typename>
    friend class Promise;
    
    explicit Promise(std::shared_ptr<SharedState<T>> state, std::shared_ptr<Executor> executor)
        : state_(std::move(state)), executor_(std::move(executor)) {}
public:
    template<typename Task>
    explicit Promise(
        Task task,
        std::shared_ptr<Executor> executor = std::make_shared<Executor>()
    ) {
        assert(executor != nullptr);
        state_ = std::make_shared<SharedState<T>>();
        executor_ = std::move(executor);

        auto reject = [state = this->state_](std::exception_ptr e) {
            std::unique_lock<std::mutex> lock(state->mtx);
            
            assert(state->state == PromiseState::PENDING);

            state->state = PromiseState::REJECTED;
            state->exception = e;
            state->trigger_callbacks();
        };

        using reject_t = decltype(reject);

        if constexpr (std::is_void_v<T>) {
            auto resolve = [state = this->state_]() {
                std::unique_lock<std::mutex> lock(state->mtx);
                
                assert(state->state == PromiseState::PENDING);

                state->state = PromiseState::FULFILLED;
                state->trigger_callbacks();
            };

            using resolve_t = decltype(resolve);
            static_assert(std::is_invocable_r_v<void, Task, resolve_t, reject_t>, "Task must be invocable with resolve() and reject(exception_ptr), and return void");

            auto callback = [t = std::move(task), res = std::move(resolve), rej = std::move(reject)]() {
                try {
                    t(res, rej);
                } catch (...) {
                    rej(std::current_exception());
                }
            };

            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback()");

            (*executor_)(std::move(callback));
        } else {
            auto resolve = [state = this->state_](T value) {
                std::unique_lock<std::mutex> lock(state->mtx);
                
                assert(state->state == PromiseState::PENDING);

                state->state = PromiseState::FULFILLED;
                state->value = std::move(value);
                state->trigger_callbacks();
            };

            using resolve_t = decltype(resolve);
            static_assert(std::is_invocable_r_v<void, Task, resolve_t, reject_t>, "Task must be invocable with resolve(value) and reject(exception_ptr), and return void");

            auto callback = [t = std::move(task), res = std::move(resolve), rej = std::move(reject)]() {
                try {
                    t(res, rej);
                } catch (...) {
                    rej(std::current_exception());
                }
            };

            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback()");
            
            (*executor_)(std::move(callback));
        }
    }

    template<
        typename FulfilledFn,
        typename RejectedFn
    >
    auto then (
        FulfilledFn onFulfilled,
        RejectedFn onRejected
    ) {

        static_assert(std::is_invocable_v<RejectedFn, std::exception_ptr>, "RejectedFn must be invocable with exception_ptr");
        if constexpr (std::is_void_v<T>) {
            static_assert(std::is_invocable_v<FulfilledFn>, "FulfilledFn must be invocable");
        } else {
            static_assert(std::is_invocable_v<FulfilledFn, T>, "FulfilledFn must be invocable with T");
        }

        if constexpr (std::is_void_v<T>) {
            using RetType = std::invoke_result_t<FulfilledFn>;
            static_assert(std::is_same_v<RetType, std::invoke_result_t<RejectedFn, std::exception_ptr>>, "RejectedFn must return the same type as FulfilledFn");

            auto next_promise_state = std::make_shared<SharedState<RetType>>();
            auto next_promise = std::shared_ptr<Promise<RetType, Executor>>(new Promise<RetType, Executor>(next_promise_state, executor_));

            auto callback = [state = this->state_, next_promise_state, onFulfilled = std::move(onFulfilled), onRejected = std::move(onRejected)]() {
                try {
                    if (state->state == PromiseState::FULFILLED) {
                        if constexpr (std::is_void_v<RetType>) {
                            onFulfilled();
                            
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->trigger_callbacks();
                        } else {
                            RetType value = onFulfilled();
                            
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->trigger_callbacks();
                        }
                    } else {
                        if constexpr (std::is_void_v<RetType>) {
                            onRejected(*state->exception);

                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->trigger_callbacks();
                        } else {
                            RetType value = onRejected(*state->exception);

                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->trigger_callbacks();
                        }
                    }
                } catch (...) {
                    std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                    next_promise_state->state = PromiseState::REJECTED;
                    next_promise_state->exception = std::current_exception();
                    next_promise_state->trigger_callbacks();
                }
            };

            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback");

            std::unique_lock<std::mutex> lock(state_->mtx);
            if (state_->state != PromiseState::PENDING) {
                (*executor_)(std::move(callback));
            } else {
                state_->callbacks.push_back([executor = std::move(executor_), callback = std::move(callback)]() mutable {
                    (*executor)(std::move(callback));
                });
            }

            return next_promise;
        } else {
            using RetType = std::invoke_result_t<FulfilledFn, T>;
            static_assert(std::is_same_v<RetType, std::invoke_result_t<RejectedFn, std::exception_ptr>>, "RejectedFn must return the same type as FulfilledFn");

            auto next_promise_state = std::make_shared<SharedState<RetType>>();
            auto next_promise = std::shared_ptr<Promise<RetType, Executor>>(new Promise<RetType, Executor>(next_promise_state, executor_));

            auto callback = [state = this->state_, next_promise_state, onFulfilled = std::move(onFulfilled), onRejected = std::move(onRejected)]() {
                try {
                    if (state->state == PromiseState::FULFILLED) {
                        if constexpr (std::is_void_v<RetType>) {
                            onFulfilled(*state->value);
                            
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->trigger_callbacks();
                        } else {
                            RetType value = onFulfilled(*state->value);
                            
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->trigger_callbacks();
                        }
                    } else {
                        if constexpr (std::is_void_v<RetType>) {
                            onRejected(*state->exception);

                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->trigger_callbacks();
                        } else {
                            RetType value = onRejected(*state->exception);

                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->trigger_callbacks();
                        }
                    }
                } catch (...) {
                    std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                    next_promise_state->state = PromiseState::REJECTED;
                    next_promise_state->exception = std::current_exception();
                    next_promise_state->trigger_callbacks();
                }
            };

            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback");

            std::unique_lock<std::mutex> lock(state_->mtx);
            if (state_->state != PromiseState::PENDING) {
                (*executor_)(std::move(callback));
            } else {
                state_->callbacks.push_back([executor = std::move(executor_), callback = std::move(callback)]() mutable {
                    (*executor)(std::move(callback));
                });
            }

            return next_promise;
        }
    }

    template<typename FulfilledFn>
    inline auto then (FulfilledFn onFulfilled) {
        if constexpr (std::is_void_v<T>) {
            using RetType = std::invoke_result_t<FulfilledFn>;
            return then(std::forward<FulfilledFn>(onFulfilled), [] (auto e) -> RetType {
                std::rethrow_exception(e);
            });
        } else {
            using RetType = std::invoke_result_t<FulfilledFn, T>;
            return then(std::forward<FulfilledFn>(onFulfilled), [] (auto e) -> RetType {
                std::rethrow_exception(e);
            });
        }
    }

    template<typename RejectedFn>
    inline auto catch_err(RejectedFn onRejected) -> std::shared_ptr<Promise<T, Executor>> {
        static_assert(std::is_invocable_r_v<T, RejectedFn, std::exception_ptr>, "RejectedFn must be invocable with std::exception_ptr, and return T");
        if constexpr (std::is_void_v<T>) {
            return then([] {}, std::forward(onRejected));
        } else {
            return then([] (T v) {
                return v;
            }, std::forward<RejectedFn>(onRejected));
        }
    }

    template<typename F>
    inline auto finally(F onFinally) -> std::shared_ptr<Promise<void, Executor>> {
        if constexpr (std::is_void_v<T>) {
            auto onFulfilled = [onFinally]() { onFinally(); };
            auto onRejected = [onFinally](std::exception_ptr) { onFinally(); };
            return then(std::move(onFulfilled), std::move(onRejected));
        } else {
            auto onFulfilled = [onFinally](T) { onFinally(); };
            auto onRejected = [onFinally](std::exception_ptr) { onFinally(); };
            return then(std::move(onFulfilled), std::move(onRejected));
        }
    }

    template<typename U = T, typename = std::enable_if_t<not std::is_void_v<U>>>
    static auto resolve(U v, std::shared_ptr<Executor> executor)
        -> std::shared_ptr<Promise<U, Executor>> {
        auto state = std::make_shared<SharedState<U>>();
        auto promise = std::shared_ptr<Promise<U, Executor>>(new Promise<U, Executor>(state, executor));

        state->state = PromiseState::FULFILLED;
        state->value = std::move(v);

        return promise;
    }

    template<typename U = T, typename = std::enable_if_t<not std::is_void_v<U>>>
    static auto reject(std::exception_ptr exception, std::shared_ptr<Executor> executor)
        -> std::shared_ptr<Promise<U, Executor>> {
        auto state = std::make_shared<SharedState<U>>();
        auto promise = std::shared_ptr<Promise<U, Executor>>(new Promise<U, Executor>(state, executor));

        state->state = PromiseState::REJECTED;
        state->exception = std::move(exception);

        return promise;
    }
};

template<typename T>
struct UsePromise {
    template<typename Task, typename Executor>
    inline auto operator()(Task task, std::shared_ptr<Executor> executor = std::make_shared<Executor>()) -> std::shared_ptr<Promise<T, Executor>> {
        return std::make_shared<Promise<T, Executor>>(std::forward<Task>(task), std::move(executor));
    }
};

template<typename T>
struct UseResolve {
    static_assert(not std::is_void_v<T>, "T must not be void when using resolve");
    template<typename Executor>
    inline auto operator()(T v, std::shared_ptr<Executor> executor = std::make_shared<Executor>()) -> std::shared_ptr<Promise<T, Executor>> {
        return Promise<T, Executor>::resolve(std::forward<T>(v), std::move(executor));
    }
};

template<typename T>
struct UseReject {
    static_assert(not std::is_void_v<T>, "T must not be void when using reject");
    template<typename Executor>
    inline auto operator()(std::exception_ptr exception, std::shared_ptr<Executor> executor = std::make_shared<Executor>()) -> std::shared_ptr<Promise<T, Executor>> {
        return Promise<T, Executor>::reject(std::move(exception), std::move(executor));
    }
};

template<typename T>
UsePromise<T> usePromise = {};

template<typename T>
UseResolve<T> useResolve = {};

template<typename T>
UseReject<T> useReject = {};

}