#pragma once

#include "scrap_mechanic_sdk/export.hpp"

#include <cstdint>

namespace scrap::sdk::lifecycle {

inline constexpr std::uint32_t abi_version_value = 1;

// A consumer acquires one shared-SDK reference before installing hooks and
// releases it only after all of its callbacks and hooks are disconnected.
SM_SDK_API std::uint32_t abi_version() noexcept;
SM_SDK_API bool acquire() noexcept;
SM_SDK_API void release() noexcept;
SM_SDK_API void shutdown() noexcept;
SM_SDK_API std::uint32_t references() noexcept;

} // namespace scrap::sdk::lifecycle

SM_SDK_C_API std::uint32_t scrap_mechanic_sdk_abi_version() noexcept;
SM_SDK_C_API bool scrap_mechanic_sdk_acquire() noexcept;
SM_SDK_C_API void scrap_mechanic_sdk_release() noexcept;
