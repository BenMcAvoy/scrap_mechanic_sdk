#pragma once

#include "scrap_mechanic_sdk/game_console.hpp"
#include "scrap_mechanic_sdk/game_mode.hpp"
#include "scrap_mechanic_sdk/runtime.hpp"
#include "scrap_mechanic_sdk/sdk.hpp"

namespace scrap::sdk::game {
inline scrap::sdk::LuaManager *lua_manager(HMODULE module = GetModuleHandleW(nullptr)) noexcept {
    return scrap::sdk::lua_manager(module);
}

inline scrap::sdk::runtime::GameModeState current_mode() noexcept {
    return scrap::sdk::runtime::refresh_game_mode();
}

inline const scrap::sdk::runtime::ConsoleState &console_state() noexcept {
    return scrap::sdk::runtime::console_state();
}
} // namespace scrap::sdk::game
