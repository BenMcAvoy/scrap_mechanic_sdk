#pragma once

#include "scrap_mechanic_sdk/sdk.hpp"

namespace scrap::sdk {

[[nodiscard]] SM_SDK_API bool read_lua_string(const void *object, std::size_t offset, LuaVMStringState &result);

[[nodiscard]] SM_SDK_API bool read_known_lifecycle_callback_name(HMODULE game_module,
    std::uintptr_t callback_hash,
    std::array<char, 96> &destination);

struct LifecycleEventDetails {
    std::uint64_t identity_hash{};
    std::uint8_t script_type{};
    bool callback_resolved{};
    std::string callback_name;
};

[[nodiscard]] SM_SDK_API bool decode_lifecycle_event(HMODULE game_module,
    LuaManager *manager,
    std::intptr_t argument,
    LifecycleEventDetails &result);

[[nodiscard]] SM_SDK_API bool read_userdata_type_name(std::uintptr_t descriptor, std::string &name);

// Refreshes a coherent, guarded view of the live LuaManager and its current
// LuaVM. The returned objects are SDK-owned snapshots; no game STL object is
// copied or destroyed by the caller.
[[nodiscard]] SM_SDK_API bool refresh_lua_state(HMODULE game_module, LuaManagerState &manager, LuaVMState &vm);

} // namespace scrap::sdk
