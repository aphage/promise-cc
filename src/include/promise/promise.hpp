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

namespace internal {

enum class PromiseState {
    PENDING,
    FULFILLED,
    REJECTED
};


template<typename Executor>
struct SharedStateBase {
    SharedStateBase(Executor executor) : executor(std::move(executor)) {}

    std::mutex mtx;
    PromiseState state = PromiseState::PENDING;
    std::optional<std::exception_ptr> exception;
    std::vector<std::function<void()>> callbacks;
    Executor executor;

    inline void trigger_callbacks(std::vector<std::function<void()>>& callbacks) {
        for (auto& callback : callbacks) {
            executor(std::move(callback));
        }
    }
};

template<typename T, typename Executor>
struct SharedState : SharedStateBase<Executor> {
    SharedState(Executor executor) : SharedStateBase<Executor>(std::forward<Executor>(executor)) {}

    std::optional<T> value;
};

template<typename Executor>
struct SharedState<void, Executor> : SharedStateBase<Executor> {
    SharedState(Executor executor) : SharedStateBase<Executor>(std::forward<Executor>(executor)) {}
};

template<typename T, typename Executor>
class Promise {
    static_assert(std::is_invocable_v<Executor, std::function<void()>>, "Executor must be invocable with std::function<void()>");
private:
    using SharedStatePtr = std::shared_ptr<SharedState<T, Executor>>;

    SharedStatePtr _state;

    template<typename, typename>
    friend class Promise;
    
    explicit Promise(SharedStatePtr state)
        : _state(std::move(state)) {}
    
public:
    template<typename Task>
    explicit Promise(
        Task task,
        Executor executor
    ) : _state(std::make_shared<SharedState<T, Executor>>(std::forward<Executor>(executor))) {

        auto reject = [state = this->_state](std::exception_ptr e) {
            std::vector<std::function<void()>> callbacks;
            {
                std::lock_guard<std::mutex> lock(state->mtx);
                assert(state->state == PromiseState::PENDING);
                state->state = PromiseState::REJECTED;
                state->exception = e;
                state->callbacks.swap(callbacks);
            }
            state->trigger_callbacks(callbacks);
        };

        using reject_t = decltype(reject);

        if constexpr (std::is_void_v<T>) {
            auto resolve = [state = this->_state]() {
                std::vector<std::function<void()>> callbacks;
                {
                    std::lock_guard<std::mutex> lock(state->mtx);
                    assert(state->state == PromiseState::PENDING);
                    state->state = PromiseState::FULFILLED;
                    state->callbacks.swap(callbacks);
                }
                state->trigger_callbacks(callbacks);
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

            _state->executor(std::move(callback));
            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback()");
        } else {
            auto resolve = [state = this->_state](T value) {
                std::vector<std::function<void()>> callbacks;
                {
                    std::lock_guard<std::mutex> lock(state->mtx);
                    assert(state->state == PromiseState::PENDING);
                    state->state = PromiseState::FULFILLED;
                    state->value = std::move(value);
                    state->callbacks.swap(callbacks);
                }
                state->trigger_callbacks(callbacks);
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

            _state->executor(std::move(callback));
            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback()");
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
        static_assert(std::is_invocable_v<RejectedFn, std::exception_ptr>, "RejectedFn must be invocable with std::exception_ptr");
        if constexpr (std::is_void_v<T>) {
            using NextT = std::invoke_result_t<FulfilledFn>;
            static_assert(std::is_invocable_v<FulfilledFn>, "FulfilledFn must be invocable");
            
            auto next_promise_state = std::make_shared<SharedState<NextT, Executor>>(_state->executor);
            auto next_promise = Promise<NextT, Executor>(next_promise_state);

            auto callback = [state = this->_state, next_promise_state, onFulfilled = std::move(onFulfilled), onRejected = std::move(onRejected)] {
                std::vector<std::function<void()>> callbacks;
                try {
                    if (state->state == PromiseState::FULFILLED) {
                        if constexpr (std::is_void_v<NextT>) {
                            onFulfilled();

                            std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->callbacks.swap(callbacks);
                        } else {
                            NextT value = onFulfilled();

                            std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->callbacks.swap(callbacks);
                        }
                    } else {
                        using RejType = std::invoke_result_t<RejectedFn, std::exception_ptr>;
                        if constexpr (std::is_void_v<RejType>) {
                            onRejected(*state->exception);

                            // If RejectedFn returns void and next value expect not void
                            if constexpr (!std::is_void_v<NextT>) {
                                //Oops!, this will never happen
                                throw std::runtime_error("Oops!, RejectedFn returns void and next value expect not void");
                            }
                        } else {
                            static_assert(std::is_same_v<NextT, RejType>, "FulfilledFn and RejectedFn must be the same type");
                            NextT value = onRejected(*state->exception);

                            std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->callbacks.swap(callbacks);
                        }
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                    next_promise_state->state = PromiseState::REJECTED;
                    next_promise_state->exception = std::current_exception();
                    next_promise_state->callbacks.swap(callbacks);
                }
                
                next_promise_state->trigger_callbacks(callbacks);
            };

            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback");

            std::lock_guard<std::mutex> lock(_state->mtx);
            if (_state->state != PromiseState::PENDING) {
                _state->executor(std::move(callback));
            } else {
                _state->callbacks.push_back(std::move(callback));
            }

            return next_promise;
        } else {
            using NextT = std::invoke_result_t<FulfilledFn, T>;
            static_assert(std::is_invocable_v<FulfilledFn, T>, "FulfilledFn must be invocable with T");

            auto next_promise_state = std::make_shared<SharedState<NextT, Executor>>(_state->executor);
            auto next_promise = Promise<NextT, Executor>(next_promise_state);

            auto callback = [state = this->_state, next_promise_state, onFulfilled = std::move(onFulfilled), onRejected = std::move(onRejected)] {
                std::vector<std::function<void()>> callbacks;
                try {
                    if (state->state == PromiseState::FULFILLED) {
                        if constexpr (std::is_void_v<NextT>) {
                            onFulfilled(*state->value);

                            std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->callbacks.swap(callbacks);
                        } else {
                            NextT value = onFulfilled(*state->value);

                            std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->callbacks.swap(callbacks);
                        }
                    } else {
                        using RejType = std::invoke_result_t<RejectedFn, std::exception_ptr>;
                        if constexpr (std::is_void_v<RejType>) {
                            onRejected(*state->exception);

                            // If RejectedFn returns void and next value expect not void
                            if constexpr (!std::is_void_v<NextT>) {
                                //Oops!, this will never happen
                                throw std::runtime_error("Oops!, RejectedFn returns void and next value expect not void");
                            }
                        } else {
                            static_assert(std::is_same_v<NextT, RejType>, "FulfilledFn and RejectedFn must be the same type");
                            NextT value = onRejected(*state->exception);

                            std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->callbacks.swap(callbacks);
                        }
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> next_lock(next_promise_state->mtx);
                    next_promise_state->state = PromiseState::REJECTED;
                    next_promise_state->exception = std::current_exception();
                    next_promise_state->callbacks.swap(callbacks);
                }
                
                next_promise_state->trigger_callbacks(callbacks);
            };

            static_assert(std::is_invocable_v<Executor, decltype(callback)>, "Executor must be invocable with callback");

            std::lock_guard<std::mutex> lock(_state->mtx);
            if (_state->state != PromiseState::PENDING) {
                _state->executor(std::move(callback));
            } else {
                _state->callbacks.emplace_back(std::move(callback));
            }

            return next_promise;
        }
    }

    template<typename FulfilledFn>
    inline auto then (FulfilledFn onFulfilled) {
        return then(std::forward<FulfilledFn>(onFulfilled), [] (auto e) {
            std::rethrow_exception(e);
        });
    }

    template<typename RejectedFn>
    inline auto catch_err(RejectedFn onRejected) {
        return then([] (const T& v) { return v; }, std::forward<RejectedFn>(onRejected));
    }

    template<typename F>
    inline auto finally(F onFinally) {
        return then(
            [onFinally](const T& v) {
                onFinally();
                return v;
            },
            [onFinally](std::exception_ptr e) {
                onFinally();
                std::rethrow_exception(e);
            }
        );
    }

    template<typename U = T>
    static auto resolve(U v, Executor executor)
        -> std::enable_if_t<!std::is_void_v<U>, Promise<U, Executor>> {
        auto state = std::make_shared<SharedState<U, Executor>>(std::forward<Executor>(executor));
        auto promise = Promise<U, Executor>(state);

        state->executor = std::move(executor);
        state->state = PromiseState::FULFILLED;
        state->value = std::move(v);

        return promise;
    }

    template<typename U = T>
    static auto resolve(Executor executor)
        -> std::enable_if_t<std::is_void_v<U>, Promise<U, Executor>> {
        auto state = std::make_shared<SharedState<U, Executor>>(std::forward<Executor>(executor));
        auto promise = Promise<U, Executor>(state);

        state->executor = std::move(executor);
        state->state = PromiseState::FULFILLED;

        return promise;
    }

    static auto reject(std::exception e, Executor executor) {
        auto state = std::make_shared<SharedState<T, Executor>>(std::forward<Executor>(executor));
        auto promise = Promise<T, Executor>(state);

        state->executor = std::move(executor);
        state->state = PromiseState::REJECTED;
        state->exception = std::make_exception_ptr(std::move(e));

        return promise;
    }
};

}

template<typename T, typename Executor>
using Promise = internal::Promise<T, Executor>;

template<typename T, typename Executor>
struct UsePromise {
    template<typename Task>
    inline auto operator()(Task task, Executor executor = Executor()) {
        return Promise<T, Executor>(std::forward<Task>(task), std::forward<Executor>(executor));
    }
};


template<typename T>
struct UsePromise<T, void> {
    template<typename Task, typename Executor>
    inline auto operator()(Task task, Executor executor) {
        return Promise<T, Executor>(std::forward<Task>(task), std::forward<Executor>(executor));
    }
};

template<typename T, typename Executor>
struct UseResolve {
    template<typename U = T>
    inline auto operator()(T v, Executor executor = Executor())
        -> std::enable_if_t<!std::is_void_v<U>, Promise<T, Executor>> {
        return Promise<T, Executor>::resolve(std::forward<T>(v), std::forward<Executor>(executor));
    }

    template<typename U = T>
    inline auto operator()(Executor executor = Executor())
        -> std::enable_if_t<std::is_void_v<U>, Promise<T, Executor>> {
        return Promise<T, Executor>::resolve(std::forward<Executor>(executor));
    }
};

template<typename T>
struct UseResolve<T, void> {
    template<typename Executor, typename U = T>
    inline auto operator()(T v, Executor executor)
        -> std::enable_if_t<!std::is_void_v<U>, Promise<T, Executor>> {
        return Promise<T, Executor>::resolve(std::forward<T>(v), std::forward<Executor>(executor));
    }

    template<typename U = T, typename Executor>
    inline auto operator()(Executor executor)
        -> std::enable_if_t<std::is_void_v<U>, Promise<T, Executor>> {
        return Promise<T, Executor>::resolve(std::forward<Executor>(executor));
    }
};

template<typename T, typename Executor>
struct UseReject {
    template<typename E>
    inline auto operator()(E e, Executor executor = Executor()) {
        return Promise<T, Executor>::reject(
            std::forward<E>(e),
            std::forward<Executor>(executor)
        );
    }
};

template<typename T>
struct UseReject<T, void> {
    template<typename E, typename Executor>
    inline auto operator()(E e, Executor executor) {
        return Promise<T, Executor>::reject(
            std::forward<E>(e),
            std::forward<Executor>(executor)
        );
    }
};

template<typename T, typename Executor>
UsePromise<T, Executor> usePromiseEx = {};
template<typename T>
UsePromise<T, void> usePromise = {};

template<typename T, typename Executor>
UseResolve<T, Executor> useResolveEx = {};
template<typename T>
UseResolve<T, void> useResolve = {};

template<typename T, typename Executor>
UseReject<T, Executor> useRejectEx = {};
template<typename T>
UseReject<T, void> useReject = {};

}