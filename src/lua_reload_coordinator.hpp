#pragma once

#include "scrap_mechanic_sdk/sdk.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace scrap::sdk::lua::internal {

struct RefreshRequest {
    LuaVM *vm{};
    LuaManager *manager{};
    std::wstring physical_path;
    std::string logical_path;
    std::uint64_t sequence{};
};

// Owns all manager/VM pairing and deferred refresh state. Hooks may publish
// requests from arbitrary game threads, but only the safe-point hook consumes
// them. Callers must not hold the coordinator lock while invoking game code.
class ReloadCoordinator {
  public:

    void remember_manager_vm(LuaManager *manager, LuaVM *vm);
    [[nodiscard]] LuaManager *manager_for_vm(LuaVM *vm);

    [[nodiscard]] bool contains_pending(LuaVM *vm, const std::string &logical_path, RefreshRequest *existing = nullptr);
    [[nodiscard]] RefreshRequest enqueue(LuaVM *vm,
        LuaManager *manager,
        std::wstring physical_path,
        std::string logical_path);

    [[nodiscard]] bool take_for_manager(LuaManager *manager, LuaVM *vm, RefreshRequest &request);
    void defer(RefreshRequest request);
    void finish_refresh() noexcept;
    [[nodiscard]] bool refresh_in_progress() const noexcept;

    void reset() noexcept;

  private:

    struct ManagerVmPair {
        LuaManager *manager{};
        LuaVM *vm{};
    };

    mutable std::mutex mutex_;
    std::array<ManagerVmPair, 32> manager_vms_{};
    std::size_t manager_vm_count_{};
    std::deque<RefreshRequest> pending_;
    std::uint64_t next_sequence_{};
    bool refresh_in_progress_{};
};

ReloadCoordinator &reload_coordinator() noexcept;

} // namespace scrap::sdk::lua::internal
