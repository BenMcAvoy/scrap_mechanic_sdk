#pragma once

#include "scrap_mechanic_sdk/lua_hooks.hpp"
#include "scrap_mechanic_sdk/lua_inspector.hpp"
#include <utility>

namespace scrap::sdk::lua::diagnostics {
using ManagerState = scrap::sdk::LuaManagerState;
using VMState = scrap::sdk::LuaVMState;
using HookState = scrap::sdk::lua::HookState;
using LifecycleEventDetails = scrap::sdk::LifecycleEventDetails;

inline bool snapshot(HMODULE module, ManagerState &manager, VMState &vm) {
    return scrap::sdk::refresh_lua_state(module, manager, vm);
}

inline const HookState &hooks() noexcept {
    return scrap::sdk::lua::hook_state();
}
} // namespace scrap::sdk::lua::diagnostics

namespace scrap::sdk::lua::hooks {
inline bool install(HMODULE module, bool client_update = false) {
    return scrap::sdk::lua::install_hooks(module, client_update);
}

inline void remove() noexcept {
    scrap::sdk::lua::remove_hooks();
}
} // namespace scrap::sdk::lua::hooks

namespace scrap::sdk::lua::reload_bridge {
using SourceBuffer = scrap::sdk::lua::SourceBuffer;
using NativeSourceProvider = scrap::sdk::lua::NativeSourceProvider;

inline void configure(const NativeSourceProvider &provider) noexcept {
    scrap::sdk::lua::configure_native_source(provider);
}

inline void notify_changed(std::wstring physical_path) noexcept {
    scrap::sdk::lua::notify_native_source_changed(std::move(physical_path));
}

inline void clear() noexcept {
    scrap::sdk::lua::clear_native_source();
}
} // namespace scrap::sdk::lua::reload_bridge
