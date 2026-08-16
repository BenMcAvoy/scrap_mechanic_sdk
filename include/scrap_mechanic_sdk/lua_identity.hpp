#pragma once

#include "scrap_mechanic_sdk/runtime.hpp"

namespace scrap::sdk::lua::identity {
using State = scrap::sdk::runtime::ScriptIdentity;

inline State from_path(std::string_view path) {
    return scrap::sdk::runtime::identify_script_path(path);
}

inline State from_vm(const scrap::sdk::LuaVMState &vm) {
    return scrap::sdk::runtime::identify_lua_vm(vm);
}

inline const char *dispatch_mode(std::uint8_t kind) noexcept {
    return scrap::sdk::runtime::lifecycle_dispatch_mode_name(kind);
}
} // namespace scrap::sdk::lua::identity
