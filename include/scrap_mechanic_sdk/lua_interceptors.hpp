#pragma once

#include "scrap_mechanic_sdk/interceptors.hpp"
#include "scrap_mechanic_sdk/sdk.hpp"

namespace scrap::sdk::lua {

struct ManagerInitialized {
    LuaManager *manager{};
    bool client{};
};

struct WorldChanged {
    LuaManager *previous{};
    LuaManager *current{};
};

struct ScriptLoading {
    LuaManager *manager{};
    void *argument{};
};

struct ScriptLoaded {
    LuaManager *manager{};
    LuaVM *vm{};
    std::string path;
};

struct CallbackDispatching {
    LuaManager *manager{};
    std::uint64_t callback_hash{};
    std::uint8_t script_type{};
    bool instant{};
};

struct FixedUpdate {
    LuaManager *manager{};
    float delta_time{};
};

struct ClientUpdate {
    LuaManager *manager{};
    float delta_time{};
};

struct NativeSourceReloaded {
    LuaVM *vm{};
    std::wstring physical_path;
    bool source_reload_succeeded{};
};

enum class HookEventType : std::uint8_t {
    initialize,
    script_load,
    client_update,
    fixed_update,
    vm_refresh,
    client_data,
    lifecycle
};

struct HookActivity {
    HookEventType type{};
    LuaManager *manager{};
    std::intptr_t argument{};
    std::uint64_t identity_hash{};
    std::uint8_t kind{};
    int flags{};
    std::string detail;
};

struct Interceptors {
    Interceptor<ManagerInitialized> manager_initialized;
    Interceptor<WorldChanged> world_changed;
    Interceptor<ScriptLoading> script_loading;
    Interceptor<ScriptLoaded> script_loaded;
    Interceptor<CallbackDispatching> callback_dispatching;
    Interceptor<FixedUpdate> fixed_update;
    Interceptor<ClientUpdate> client_update;
    Interceptor<NativeSourceReloaded> source_reloaded;
    Interceptor<HookActivity> hook_activity;
};

[[nodiscard]] SM_SDK_API Interceptors &interceptors() noexcept;

} // namespace scrap::sdk::lua
