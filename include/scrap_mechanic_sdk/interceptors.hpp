#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace scrap::sdk {

// Synchronous interception point. Subscribers execute on the thread that
// publishes the interception, so callbacks may observe or change game state
// but must remain short, non-blocking, and reentrancy-safe.
template <typename Payload> class Interceptor {
  public:

    using Listener = std::function<void(const Payload &)>;

    class Connection {
      public:

        Connection() = default;

        Connection(Interceptor *owner, std::size_t id) : owner_(owner), id_(id) {}

        Connection(const Connection &) = delete;
        Connection &operator=(const Connection &) = delete;

        Connection(Connection &&other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)), id_(std::exchange(other.id_, 0)) {}

        Connection &operator=(Connection &&other) noexcept {
            if (this != &other) {
                disconnect();
                owner_ = std::exchange(other.owner_, nullptr);
                id_ = std::exchange(other.id_, 0);
            }
            return *this;
        }

        ~Connection() {
            disconnect();
        }

        void disconnect() noexcept {
            if (owner_) {
                owner_->disconnect(id_);
                owner_ = nullptr;
                id_ = 0;
            }
        }

      private:

        Interceptor *owner_{};
        std::size_t id_{};
    };

    // Register a synchronous listener. The returned connection owns the
    // subscription and removes it when destroyed.
    Connection subscribe(Listener listener) {
        std::lock_guard lock(mutex_);
        const auto id = next_id_++;
        listeners_.push_back({id, std::move(listener)});
        return Connection(this, id);
    }

    // Publish on the current thread. Listener exceptions are contained so a
    // consumer cannot unwind through a native game hook.
    void publish(const Payload &payload) const noexcept {
        std::vector<Listener> listeners;
        {
            std::lock_guard lock(mutex_);
            listeners.reserve(listeners_.size());
            for (const auto &entry : listeners_)
                listeners.push_back(entry.listener);
        }
        for (const auto &listener : listeners) {
            if (!listener)
                continue;
            try {
                listener(payload);
            } catch (...) {
                // A consumer must never throw through a native game hook.
            }
        }
    }

  private:

    struct Entry {
        std::size_t id;
        Listener listener;
    };

    void disconnect(std::size_t id) noexcept {
        std::lock_guard lock(mutex_);
        listeners_.erase(std::remove_if(listeners_.begin(),
                             listeners_.end(),
                             [id](const Entry &entry) { return entry.id == id; }),
            listeners_.end());
    }

    mutable std::mutex mutex_;
    std::vector<Entry> listeners_;
    std::size_t next_id_{1};
};

} // namespace scrap::sdk
