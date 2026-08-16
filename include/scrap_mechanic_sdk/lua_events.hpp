#pragma once

#include "scrap_mechanic_sdk/events.hpp"

#include <cstdint>
#include <string>

namespace scrap::sdk::lua {

// Asynchronous payloads deliberately contain copies and opaque identity
// values. They may outlive the native hook call that produced them.
struct SourceReloadedEvent {
    std::uintptr_t vm{};
    std::wstring physical_path;
    bool source_reload_succeeded{};
};

struct ManagerInitializedEvent {
    std::uintptr_t manager{};
    bool client{};
};

struct LifecycleEvent {
    std::uintptr_t manager{};
    std::uint64_t identity_hash{};
    std::uint8_t script_type{};
    std::uint8_t kind{};
    int flags{};
    std::string detail;
};

// Domain-facing alias for the process-wide SDK event bus. The generic
// sdk::events() accessor refers to the same singleton.
[[nodiscard]] SM_SDK_API AsyncEventBus &events() noexcept;

} // namespace scrap::sdk::lua
