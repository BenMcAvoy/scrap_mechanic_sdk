#include "lua_reload_coordinator.hpp"

#include <algorithm>

namespace scrap::sdk::lua::internal {
namespace {

    ReloadCoordinator g_coordinator;

} // namespace

void ReloadCoordinator::remember_manager_vm(LuaManager *manager, LuaVM *vm) {
    if (!manager || !vm)
        return;

    std::lock_guard lock(mutex_);
    for (std::size_t index = 0; index < manager_vm_count_; ++index) {
        if (manager_vms_[index].manager == manager) {
            manager_vms_[index].vm = vm;
            return;
        }
    }

    if (manager_vm_count_ < manager_vms_.size())
        manager_vms_[manager_vm_count_++] = {manager, vm};
}

LuaManager *ReloadCoordinator::manager_for_vm(LuaVM *vm) {
    if (!vm)
        return nullptr;

    std::lock_guard lock(mutex_);
    LuaManager *result = nullptr;
    for (std::size_t index = 0; index < manager_vm_count_; ++index) {
        const auto &pair = manager_vms_[index];
        if (pair.vm != vm)
            continue;
        if (result && result != pair.manager)
            return nullptr;
        result = pair.manager;
    }
    return result;
}

bool ReloadCoordinator::contains_pending(LuaVM *vm, const std::string &logical_path, RefreshRequest *existing) {
    std::lock_guard lock(mutex_);
    const auto iterator = std::find_if(pending_.begin(),
        pending_.end(),
        [vm, &logical_path](
            const RefreshRequest &request) { return request.vm == vm && request.logical_path == logical_path; });

    if (iterator == pending_.end())
        return false;
    if (existing)
        *existing = *iterator;
    return true;
}

RefreshRequest ReloadCoordinator::enqueue(LuaVM *vm,
    LuaManager *manager,
    std::wstring physical_path,
    std::string logical_path) {
    std::lock_guard lock(mutex_);
    RefreshRequest request{vm, manager, std::move(physical_path), std::move(logical_path), ++next_sequence_};
    pending_.push_back(request);
    return request;
}

bool ReloadCoordinator::take_for_manager(LuaManager *manager, LuaVM *vm, RefreshRequest &request) {
    if (!manager || !vm)
        return false;

    std::lock_guard lock(mutex_);
    if (refresh_in_progress_)
        return false;

    const auto iterator = std::find_if(pending_.begin(),
        pending_.end(),
        [manager, vm](const RefreshRequest &candidate) {
            return candidate.vm == vm && (candidate.manager == nullptr || candidate.manager == manager);
        });
    if (iterator == pending_.end())
        return false;
    if (iterator->manager && iterator->manager != manager)
        return false;

    request = *iterator;
    pending_.erase(iterator);
    refresh_in_progress_ = true;
    return true;
}

void ReloadCoordinator::defer(RefreshRequest request) {
    std::lock_guard lock(mutex_);
    pending_.push_front(std::move(request));
    refresh_in_progress_ = false;
}

void ReloadCoordinator::finish_refresh() noexcept {
    std::lock_guard lock(mutex_);
    refresh_in_progress_ = false;
}

bool ReloadCoordinator::refresh_in_progress() const noexcept {
    std::lock_guard lock(mutex_);
    return refresh_in_progress_;
}

void ReloadCoordinator::reset() noexcept {
    std::lock_guard lock(mutex_);
    manager_vm_count_ = 0;
    pending_.clear();
    next_sequence_ = 0;
    refresh_in_progress_ = false;
}

ReloadCoordinator &reload_coordinator() noexcept {
    return g_coordinator;
}

} // namespace scrap::sdk::lua::internal
