#pragma once

#include "scrap_mechanic_sdk/export.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scrap::sdk {

// Process-wide asynchronous event dispatcher.
//
// Publishing is non-blocking with respect to listeners: the payload and a
// snapshot of the current listeners are copied into a bounded queue, and the
// listeners run later on the SDK-owned worker thread. Event listeners must
// therefore use snapshots and stable identifiers rather than live game
// pointers.
class AsyncEventBus {
  public:

    class Connection {
      public:

        Connection() = default;

        Connection(std::weak_ptr<void> channel, std::function<void()> disconnect)
            : channel_(std::move(channel)), disconnect_(std::move(disconnect)) {}

        Connection(const Connection &) = delete;
        Connection &operator=(const Connection &) = delete;

        Connection(Connection &&other) noexcept
            : channel_(std::move(other.channel_)), disconnect_(std::move(other.disconnect_)) {}

        Connection &operator=(Connection &&other) noexcept {
            if (this != &other) {
                disconnect();
                channel_ = std::move(other.channel_);
                disconnect_ = std::move(other.disconnect_);
            }
            return *this;
        }

        ~Connection() {
            disconnect();
        }

        void disconnect() noexcept {
            if (disconnect_ && !channel_.expired()) {
                try {
                    disconnect_();
                } catch (...) {
                }
            }
            disconnect_ = {};
            channel_.reset();
        }

      private:

        std::weak_ptr<void> channel_;
        std::function<void()> disconnect_;
    };

    AsyncEventBus() = default;

    ~AsyncEventBus() {
        stop();
    }

    AsyncEventBus(const AsyncEventBus &) = delete;
    AsyncEventBus &operator=(const AsyncEventBus &) = delete;

    // Lifecycle is controlled by sdk::lifecycle. These functions are public
    // so the shared SDK can own the worker independently of its consumers.
    void start();
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t queued() const noexcept;

    [[nodiscard]] std::uint64_t published() const noexcept {
        return published_.load();
    }

    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load();
    }

    // Subscriptions are copied into each queued task. Disconnecting prevents
    // future publications, but cannot cancel a task that is already queued.
    template <typename Event, typename Listener> Connection subscribe(Listener listener) {
        auto channel = channel_for<Event>();
        const auto id = channel->add(std::function<void(const Event &)>(std::move(listener)));
        std::weak_ptr<Channel<Event>> weak = channel;
        return Connection(weak, [weak, id] {
            if (auto locked = weak.lock())
                locked->remove(id);
        });
    }

    // Events published while the bus is stopped, or after the queue reaches
    // capacity, are dropped and reflected in dropped().
    template <typename Event> void publish(Event event) {
        if (!running()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto channel = channel_for<Event>();
        std::vector<std::function<void(const Event &)>> listeners;
        channel->snapshot(listeners);
        if (listeners.empty())
            return;
        std::function<void()> task = [event = std::move(event), listeners = std::move(listeners)]() mutable {
            for (const auto &listener : listeners) {
                try {
                    listener(event);
                } catch (...) {
                }
            }
        };
        {
            std::lock_guard lock(queue_mutex_);
            if (queue_.size() >= capacity_) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            queue_.push_back(std::move(task));
            published_.fetch_add(1, std::memory_order_relaxed);
        }
        queue_cv_.notify_one();
    }

  private:

    template <typename Event> struct Channel : std::enable_shared_from_this<Channel<Event>> {
        using Listener = std::function<void(const Event &)>;

        std::size_t add(Listener listener) {
            std::lock_guard lock(mutex);
            const auto id = next_id++;
            listeners.push_back({id, std::move(listener)});
            return id;
        }

        void remove(std::size_t id) noexcept {
            std::lock_guard lock(mutex);
            listeners.erase(std::remove_if(listeners.begin(),
                                listeners.end(),
                                [id](const auto &entry) { return entry.first == id; }),
                listeners.end());
        }

        void snapshot(std::vector<Listener> &out) const {
            std::lock_guard lock(mutex);
            for (const auto &entry : listeners)
                out.push_back(entry.second);
        }

        mutable std::mutex mutex;
        std::vector<std::pair<std::size_t, Listener>> listeners;
        std::size_t next_id{1};
    };

    template <typename Event> std::shared_ptr<Channel<Event>> channel_for() {
        std::lock_guard lock(channels_mutex_);
        const auto key = std::type_index(typeid(Event));
        auto it = channels_.find(key);
        if (it != channels_.end())
            return std::static_pointer_cast<Channel<Event>>(it->second);
        auto channel = std::make_shared<Channel<Event>>();
        channels_.emplace(key, channel);
        return channel;
    }

    void worker_main() noexcept;
    static constexpr std::size_t capacity_ = 4096;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::function<void()>> queue_;
    std::mutex channels_mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> channels_;
    std::thread worker_;
    std::atomic_bool running_{false};
    std::atomic_bool stopping_{false};
    std::atomic_uint64_t published_{0};
    std::atomic_uint64_t dropped_{0};
};

SM_SDK_API AsyncEventBus &events() noexcept;

} // namespace scrap::sdk
