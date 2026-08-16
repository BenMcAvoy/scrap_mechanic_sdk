#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace scrap::sdk {

// Owns hook lifetime without knowing which detour library a game build uses.
// Mods register installation/removal actions; the SDK controls ordering and
// guarantees reverse-order cleanup.
class HookRegistry {
  public:

    using Install = std::function<bool()>;
    using Remove = std::function<void()>;

    void add(std::string name, Install install, Remove remove) {
        entries_.push_back({std::move(name), std::move(install), std::move(remove), false});
    }

    [[nodiscard]] bool install_all() {
        for (auto &entry : entries_) {
            if (!entry.install || !entry.install()) {
                remove_all();
                return false;
            }
            entry.installed = true;
        }
        return true;
    }

    void remove_all() noexcept {
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->installed && it->remove)
                it->remove();
            it->installed = false;
        }
    }

    void clear() noexcept {
        remove_all();
        entries_.clear();
    }

    [[nodiscard]] bool installed(std::string_view name) const noexcept {
        for (const auto &entry : entries_)
            if (entry.name == name)
                return entry.installed;
        return false;
    }

  private:

    struct Entry {
        std::string name;
        Install install;
        Remove remove;
        bool installed{};
    };

    std::vector<Entry> entries_;
};

} // namespace scrap::sdk
