#include "scrap_mechanic_sdk/events.hpp"
#include "scrap_mechanic_sdk/lifecycle.hpp"
#include "scrap_mechanic_sdk/lua_events.hpp"
#include "scrap_mechanic_sdk/lua_interceptors.hpp"

namespace scrap::sdk {

void AsyncEventBus::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { worker_main(); });
}

void AsyncEventBus::stop() noexcept {
    if (!running_.load(std::memory_order_acquire) && !worker_.joinable())
        return;
    stopping_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard lock(queue_mutex_);
        queue_.clear();
    }
    running_.store(false, std::memory_order_release);
}

std::size_t AsyncEventBus::queued() const noexcept {
    std::lock_guard lock(queue_mutex_);
    return queue_.size();
}

void AsyncEventBus::worker_main() noexcept {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return stopping_.load(std::memory_order_acquire) || !queue_.empty(); });
            if (stopping_.load(std::memory_order_acquire))
                return;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            if (task)
                task();
        } catch (...) { // Event isolation: one subscriber must not stop the bus. NOLINT(bugprone-empty-catch)
        }
    }
}

namespace {
    std::mutex g_lifecycle_mutex;
    std::uint32_t g_lifecycle_refs{};
    AsyncEventBus g_events;
    lua::Interceptors g_interceptors;
} // namespace

AsyncEventBus &events() noexcept {
    return g_events;
}

namespace lifecycle {
    std::uint32_t abi_version() noexcept {
        return abi_version_value;
    }

    bool acquire() noexcept {
        std::lock_guard lock(g_lifecycle_mutex);
        if (g_lifecycle_refs++ == 0)
            g_events.start();
        return true;
    }

    void release() noexcept {
        bool stop = false;
        {
            std::lock_guard lock(g_lifecycle_mutex);
            if (g_lifecycle_refs == 0)
                return;
            stop = (--g_lifecycle_refs == 0);
        }
        if (stop)
            g_events.stop();
    }

    void shutdown() noexcept {
        {
            std::lock_guard lock(g_lifecycle_mutex);
            g_lifecycle_refs = 0;
        }
        g_events.stop();
    }

    std::uint32_t references() noexcept {
        std::lock_guard lock(g_lifecycle_mutex);
        return g_lifecycle_refs;
    }
} // namespace lifecycle

} // namespace scrap::sdk

SM_SDK_C_API std::uint32_t scrap_mechanic_sdk_abi_version() noexcept {
    return scrap::sdk::lifecycle::abi_version();
}

SM_SDK_C_API bool scrap_mechanic_sdk_acquire() noexcept {
    return scrap::sdk::lifecycle::acquire();
}

SM_SDK_C_API void scrap_mechanic_sdk_release() noexcept {
    scrap::sdk::lifecycle::release();
}

namespace scrap::sdk::lua {

Interceptors &interceptors() noexcept {
    return scrap::sdk::g_interceptors;
}

AsyncEventBus &events() noexcept {
    return scrap::sdk::events();
}

} // namespace scrap::sdk::lua
