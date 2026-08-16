#pragma once

#include "scrap_mechanic_sdk/runtime.hpp"

namespace scrap::sdk::game::mode {
using State = scrap::sdk::runtime::GameModeState;

inline State current() noexcept {
    return scrap::sdk::runtime::refresh_game_mode();
}

inline bool resolve(HMODULE module, const void *script_function) {
    return scrap::sdk::runtime::resolve_game_mode(module, script_function);
}
} // namespace scrap::sdk::game::mode
