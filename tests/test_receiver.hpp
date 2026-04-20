#pragma once

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexec/execution.hpp>
#include <tuple>

namespace ex = stdexec;

// Ресивер для тестирования асинхронных сендеров
template <typename... Ts>
class TestReceiver {
    // Внутреннее состояние, разделяемое между ресивером и тестом
    struct SharedState {
        std::optional<std::tuple<Ts...>> value;
        std::exception_ptr error;
        bool stopped = false;
        bool done = false;
        std::mutex mtx;
        std::condition_variable cv;
    };

public:
    using receiver_concept = ex::receiver_t;

    TestReceiver() : state_(std::make_shared<SharedState>()) {}

    // Методы ресивера (вызываются сендером в разных потоках)

    void set_value(Ts... vals) noexcept {
        std::lock_guard lock(state_->mtx);
        state_->value.emplace(std::move(vals)...);
        state_->done = true;
        state_->cv.notify_one();
    }

    void set_error(std::exception_ptr e) noexcept {
        std::lock_guard lock(state_->mtx);
        state_->error = std::move(e);
        state_->done = true;
        state_->cv.notify_one();
    }

    void set_stopped() noexcept {
        std::lock_guard lock(state_->mtx);
        state_->stopped = true;
        state_->done = true;
        state_->cv.notify_one();
    }

    auto get_env() const noexcept { return ex::env<>{}; }

    // Методы для использования в тестах (вызываются в основном потоке)

    // Ждет завершения работы сендера
    void wait() const {
        std::unique_lock lock(state_->mtx);
        state_->cv.wait(lock, [this] { return state_->done; });
    }

    bool has_value() const {
        std::lock_guard lock(state_->mtx);
        return state_->value.has_value();
    }

    auto get_value() const {
        std::lock_guard lock(state_->mtx);
        return *(state_->value);
    }

    bool was_stopped() const {
        std::lock_guard lock(state_->mtx);
        return state_->stopped;
    }

    std::exception_ptr get_error() const {
        std::lock_guard lock(state_->mtx);
        return state_->error;
    }

private:
    std::shared_ptr<SharedState> state_;
};

// Специализация для void-сендеров
template <>
class TestReceiver<> {
    struct SharedState {
        bool value_received = false;
        std::exception_ptr error;
        bool stopped = false;
        bool done = false;
        std::mutex mtx;
        std::condition_variable cv;
    };

public:
    using receiver_concept = ex::receiver_t;

    TestReceiver() : state_(std::make_shared<SharedState>()) {}

    void set_value() noexcept {
        std::lock_guard lock(state_->mtx);
        state_->value_received = true;
        state_->done = true;
        state_->cv.notify_one();
    }

    void set_error(std::exception_ptr e) noexcept {
        std::lock_guard lock(state_->mtx);
        state_->error = std::move(e);
        state_->done = true;
        state_->cv.notify_one();
    }

    void set_stopped() noexcept {
        std::lock_guard lock(state_->mtx);
        state_->stopped = true;
        state_->done = true;
        state_->cv.notify_one();
    }

    auto get_env() const noexcept { return ex::env<>{}; }

    void wait() const {
        std::unique_lock lock(state_->mtx);
        state_->cv.wait(lock, [this] { return state_->done; });
    }

    bool has_value() const {
        std::lock_guard lock(state_->mtx);
        return state_->value_received;
    }

    bool was_stopped() const {
        std::lock_guard lock(state_->mtx);
        return state_->stopped;
    }

    std::exception_ptr get_error() const {
        std::lock_guard lock(state_->mtx);
        return state_->error;
    }

private:
    std::shared_ptr<SharedState> state_;
};