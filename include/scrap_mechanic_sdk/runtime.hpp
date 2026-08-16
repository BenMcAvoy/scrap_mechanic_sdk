#pragma once

#include "scrap_mechanic_sdk/lua_inspector.hpp"
#include "scrap_mechanic_sdk/sdk.hpp"

namespace scrap::sdk::runtime {

using ConsoleWriteFn = std::int64_t(__fastcall *)(int,
    unsigned int,
    const char *,
    unsigned int,
    const char **,
    std::uintptr_t,
    const unsigned int *,
    std::uintptr_t,
    const char *);

struct ConsoleState {
    bool resolved{};
    std::string error;
    std::uint64_t writes{};
};

struct GameModeState {
    bool scalar_resolved{};
    std::int32_t raw_value{};
    std::uintptr_t scalar_address{};
    std::string mode;
    std::string class_name;
};

struct ScriptIdentity {
    std::string mode;
    std::string class_name;
};

[[nodiscard]] SM_SDK_API bool resolve_console(HMODULE game_module);
[[nodiscard]] SM_SDK_API const ConsoleState &console_state() noexcept;
[[nodiscard]] SM_SDK_API ConsoleWriteFn console_writer() noexcept;
SM_SDK_API void record_console_write() noexcept;

[[nodiscard]] SM_SDK_API bool resolve_game_mode(HMODULE game_module, const void *script_function);
[[nodiscard]] SM_SDK_API GameModeState refresh_game_mode() noexcept;
[[nodiscard]] SM_SDK_API ScriptIdentity identify_script_path(std::string_view path);
[[nodiscard]] SM_SDK_API ScriptIdentity identify_lua_vm(const LuaVMState &vm);
[[nodiscard]] SM_SDK_API const char *lifecycle_dispatch_mode_name(std::uint8_t kind) noexcept;

} // namespace scrap::sdk::runtime
