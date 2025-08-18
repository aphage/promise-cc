#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>
#include <exception>

namespace promise {

template<typename T, typename Executor>
class Promise;
template<typename Executor>
class Promise<void, Executor>;

enum class PromiseState {
    PENDING,
    FULFILLED,
    REJECTED
};

template<typename T>
struct SharedState {
    std::mutex mtx;
    PromiseState state = PromiseState::PENDING;
    std::optional<T> value;
    std::optional<std::exception_ptr> exception;
    std::vector<std::function<void()>> callbacks;

    inline void trigger_callbacks() {
        for (const auto& callback : callbacks) {
            callback();
        }
        callbacks.clear();
    }
};

template<>
struct SharedState<void> {
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

template<typename T, typename Executor>
class Promise {
private:
    std::shared_ptr<SharedState<T>> state_;
    std::shared_ptr<Executor> executor_;

    template<typename, typename>
    friend class Promise;
    
    explicit Promise(std::shared_ptr<SharedState<T>> state, const std::shared_ptr<Executor>& executor)
        : state_(state), executor_(executor) {}
public:
    explicit Promise(std::function<void(std::function<void(T)>,
        std::function<void(std::exception_ptr)>)> task,
        const std::shared_ptr<Executor>& executor = std::make_shared<Executor>()
    ) : executor_(executor) {
        state_ = std::make_shared<SharedState<T>>();
        
        auto resolve = [state = this->state_](T value) {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->state != PromiseState::PENDING) return;
            state->state = PromiseState::FULFILLED;
            state->value = std::move(value);
            state->trigger_callbacks();
        };

        auto reject = [state = this->state_](std::exception_ptr e) {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->state != PromiseState::PENDING) return;
            state->state = PromiseState::REJECTED;
            state->exception = e;
            state->trigger_callbacks();
        };

        (*executor_)([t = std::move(task), res = std::move(resolve), rej = std::move(reject)]() {
            try {
                t(res, rej);
            } catch (...) {
                rej(std::current_exception());
            }
        });
    }

    template<typename U, typename V = Promise<U, Executor>>
    auto then(std::function<U(T)> onFulfilled, std::function<void(std::exception_ptr)> onRejected = nullptr)
        -> std::shared_ptr<V> {
        
        auto next_promise_state = std::make_shared<SharedState<U>>();
        auto next_promise = std::shared_ptr<V>(new V(next_promise_state, executor_));

        auto callback = [state = this->state_, next_promise_state, onFulfilled = std::move(onFulfilled), onRejected = std::move(onRejected)]() {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->state == PromiseState::FULFILLED) {
                if (onFulfilled) {
                    try {
                        if constexpr (std::is_same_v<U, void>) {
                            onFulfilled(*state->value);
                            
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->trigger_callbacks();
                        } else {
                            U value = onFulfilled(*state->value);

                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->trigger_callbacks();
                        }
                    } catch (...) {
                        std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                        next_promise_state->state = PromiseState::REJECTED;
                        next_promise_state->exception = std::current_exception();
                        next_promise_state->trigger_callbacks();
                    }
                }
            } else {
                if (onRejected) {
                    try {
                        onRejected(*state->exception);

                        std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                        next_promise_state->state = PromiseState::FULFILLED;
                        next_promise_state->trigger_callbacks();
                    } catch (...) {
                        std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                        next_promise_state->state = PromiseState::REJECTED;
                        next_promise_state->exception = std::current_exception();
                        next_promise_state->trigger_callbacks();
                    }
                } else {
                    std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                    next_promise_state->state = PromiseState::REJECTED;
                    next_promise_state->exception = state->exception;
                    next_promise_state->trigger_callbacks();
                }
            }
        };

        std::unique_lock<std::mutex> lock(state_->mtx);
        if (state_->state != PromiseState::PENDING) {
            lock.unlock();
            (*executor_)(callback);
        } else {
            state_->callbacks.push_back([callback = std::move(callback), executor = executor_]() {
                (*executor)(callback);
            });
        }

        return next_promise;
    }

    inline auto then(std::function<void(T)> onFulfilled, std::function<void(std::exception_ptr)> onRejected = nullptr)
        -> std::shared_ptr<Promise<void, Executor>> {
        return then<void, Promise<void, Executor>>(std::move(onFulfilled), std::move(onRejected));
    }

    inline auto catch_err(std::function<void(std::exception_ptr)> onRejected) -> std::shared_ptr<Promise<Executor, void>> {
        return then(nullptr, std::move(onRejected));
    }

    inline auto finally(std::function<void()> onFinally) -> std::shared_ptr<Promise<Executor, void>> {
        auto onFulfilled = [onFinally](T) { onFinally(); };
        auto onRejected = [onFinally](std::exception_ptr) { onFinally(); };
        return then(std::move(onFulfilled), std::move(onRejected));
    }
};

template<typename Executor>
class Promise<void, Executor> {
private:
    std::shared_ptr<SharedState<void>> state_;
    std::shared_ptr<Executor> executor_;

    template<typename, typename>
    friend class Promise;

    explicit Promise(std::shared_ptr<SharedState<void>> state, const std::shared_ptr<Executor>& executor)
        : state_(state), executor_(executor) {}

public:
    explicit Promise(std::function<void(std::function<void()>,
        std::function<void(std::exception_ptr)>)> task,
        const std::shared_ptr<Executor>& executor = std::make_shared<Executor>()
    ) : executor_(executor) {
        state_ = std::make_shared<SharedState<void>>();
        
        auto resolve = [state = this->state_]() {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->state != PromiseState::PENDING) return;
            state->state = PromiseState::FULFILLED;
            state->trigger_callbacks();
        };

        auto reject = [state = this->state_](std::exception_ptr e) {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->state != PromiseState::PENDING) return;
            state->state = PromiseState::REJECTED;
            state->exception = e;
            state->trigger_callbacks();
        };

        (*executor_)([t = std::move(task), res = std::move(resolve), rej = std::move(reject)]() {
            try {
                t(res, rej);
            } catch (...) {
                rej(std::current_exception());
            }
        });
    }

    template<typename U, typename V = Promise<U, Executor>>
    auto then(std::function<U()> onFulfilled, std::function<void(std::exception_ptr)> onRejected = nullptr)
        -> std::shared_ptr<V> {
        
        auto next_promise_state = std::make_shared<SharedState<U>>();
        auto next_promise = std::shared_ptr<V>(new V(next_promise_state, executor_));

        auto callback = [state = this->state_, next_promise_state, onFulfilled = std::move(onFulfilled), onRejected = std::move(onRejected)]() {
            std::unique_lock<std::mutex> lock(state->mtx);
            if (state->state == PromiseState::FULFILLED) {
                if (onFulfilled) {
                    try {
                        if constexpr (std::is_same_v<U, void>) {
                            onFulfilled();
                            
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->trigger_callbacks();
                        } else {
                            U value = onFulfilled();
                            std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                            next_promise_state->state = PromiseState::FULFILLED;
                            next_promise_state->value = std::move(value);
                            next_promise_state->trigger_callbacks();
                        }
                    } catch (...) {
                        std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                        next_promise_state->state = PromiseState::REJECTED;
                        next_promise_state->exception = std::current_exception();
                        next_promise_state->trigger_callbacks();
                    }
                }
            } else {
                if (onRejected) {
                    try {
                        onRejected(*state->exception);
                        std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                        next_promise_state->state = PromiseState::FULFILLED;
                        next_promise_state->trigger_callbacks();
                    } catch (...) {
                        std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                        next_promise_state->state = PromiseState::REJECTED;
                        next_promise_state->exception = std::current_exception();
                        next_promise_state->trigger_callbacks();
                    }
                } else {
                    std::unique_lock<std::mutex> next_lock(next_promise_state->mtx);
                    next_promise_state->state = PromiseState::REJECTED;
                    next_promise_state->exception = state->exception;
                    next_promise_state->trigger_callbacks();
                }
            }
        };

        std::unique_lock<std::mutex> lock(state_->mtx);
        if (state_->state != PromiseState::PENDING) {
            lock.unlock();
            (*executor_)(callback);
        } else {
            state_->callbacks.push_back([callback = std::move(callback), executor = executor_]() {
                (*executor)(callback);
            });
        }

        return next_promise;
    }

    inline auto then(std::function<void()> onFulfilled, std::function<void(std::exception_ptr)> onRejected = nullptr)
        -> std::shared_ptr<Promise<void, Executor>> {
        return then<void, Promise<void, Executor>>(std::move(onFulfilled), std::move(onRejected));
    }
    
    inline auto catch_err(std::function<void(std::exception_ptr)> onRejected) -> std::shared_ptr<Promise<Executor, void>> {
        return then(nullptr, std::move(onRejected));
    }

    inline auto finally(std::function<void()> onFinally) -> std::shared_ptr<Promise<Executor, void>> {
        auto onFulfilled = [onFinally]() { onFinally(); };
        auto onRejected = [onFinally](std::exception_ptr) { onFinally(); };
        return then(std::move(onFulfilled), std::move(onRejected));
    }
};

}