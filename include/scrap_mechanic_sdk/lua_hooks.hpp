#pragma once

#include "scrap_mechanic_sdk/lua_interceptors.hpp"

#include <cstddef>
#include <string>

namespace scrap::sdk::lua {

// Diagnostics for one resolved native entry point. Addresses are informational
// only; consumers must use the hook lifecycle functions rather than calling
// them directly.
struct HookStatus {
    const char *label{};
    const char *anchor{};
    void *address{};
    bool resolved{};
    std::string error;
    std::uint64_t calls{};
    std::uintptr_t last_this{};
    DWORD last_thread{};
    std::uint64_t last_tick{};
};

struct HookState {
    HookStatus initialize;
    HookStatus script_load;
    HookStatus client_update;
    HookStatus fixed_update;
    HookStatus vm_refresh;
    HookStatus client_data;
    HookStatus lifecycle;
};

// Buffer ABI used by the native script loader. The provider must return data
// owned according to the game's loader contract; it must not retain pointers
// into temporary provider storage.
struct SourceBuffer {
    void *data{};
    std::uint32_t size{};
    std::uint8_t owned{1};
    std::uint8_t reserved[3]{};
};

// Native loader ABI boundary. The SDK consumes this provider; it does not
// watch files or decide when a reload should occur.
struct NativeSourceProvider {
    void *context{};
    bool (*is_lua_source)(void *context, const std::string *path) noexcept {};
    bool (*read_source)(void *context, SourceBuffer *buffer, const std::string *path) noexcept {};
    bool (*resolve_path)(void *context, const std::string *path, std::wstring &physical) noexcept {};
    bool (*logical_path)(void *context, const std::wstring &physical, std::string &logical) noexcept {};
};

[[nodiscard]] SM_SDK_API const HookState &hook_state() noexcept;
// Returns a copy because the watcher may publish another path concurrently.
[[nodiscard]] SM_SDK_API std::wstring last_changed_path();
SM_SDK_API void configure_native_source(const NativeSourceProvider &provider) noexcept;
SM_SDK_API void notify_native_source_changed(std::wstring physical_path) noexcept;
SM_SDK_API void clear_native_source() noexcept;
[[nodiscard]] SM_SDK_API bool install_hooks(HMODULE game_module, bool install_client_update = false);
SM_SDK_API void remove_hooks() noexcept;

} // namespace scrap::sdk::lua
