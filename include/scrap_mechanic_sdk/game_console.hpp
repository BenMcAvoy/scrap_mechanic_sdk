#pragma once

#include "scrap_mechanic_sdk/runtime.hpp"

#include <string>
#include <string_view>

namespace scrap::sdk::game::console {

// Stable facade over the game's resolved UTILS::Console entry point. The SDK
// owns resolution and diagnostics; consumers only provide message text.
using State = scrap::sdk::runtime::ConsoleState;
using WriteFn = scrap::sdk::runtime::ConsoleWriteFn;

inline bool resolve(HMODULE module) {
    return scrap::sdk::runtime::resolve_console(module);
}

inline const State &state() noexcept {
    return scrap::sdk::runtime::console_state();
}

inline WriteFn writer() noexcept {
    return scrap::sdk::runtime::console_writer();
}

inline void record_write() noexcept {
    scrap::sdk::runtime::record_console_write();
}

// Write a normal SDK diagnostic through the game's native UTILS::Console
// logger. Consumers should use this facade instead of calling the native
// function pointer directly.
inline bool write(std::string_view message) {
    const auto console_writer = writer();
    if (!console_writer)
        return false;

    const std::string owned_message(message);
    static constexpr char source_path[] = "scrap_mechanic_sdk";
    static constexpr unsigned int source_line = 1;
    const char *source_name = source_path;

    console_writer(10, 1, source_path, source_line, &source_name, 0, &source_line, 0, owned_message.c_str());
    record_write();
    return true;
}
} // namespace scrap::sdk::game::console
