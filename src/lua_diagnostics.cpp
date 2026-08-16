#include "lua_diagnostics.hpp"

#include <windows.h>

#include <cstdarg>
#include <fstream>
#include <mutex>
#include <string>

namespace scrap::sdk::lua::internal {
namespace {

    std::mutex g_diagnostic_mutex;

} // namespace

void diagnostic_log(const char *format, ...) noexcept {
    char message[1024]{};
    va_list arguments;
    va_start(arguments, format);
    _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
    va_end(arguments);

    OutputDebugStringA((std::string("[HOTRELOAD] ") + message + "\n").c_str());

    try {
        std::lock_guard lock(g_diagnostic_mutex);
        char temp_path[MAX_PATH]{};
        const auto length = GetTempPathA(static_cast<DWORD>(sizeof(temp_path)), temp_path);
        if (length == 0 || length >= sizeof(temp_path))
            return;

        std::ofstream file(std::string(temp_path) + "scrap_lua_hot_reload.log", std::ios::app);
        if (file)
            file << "[HOTRELOAD] " << message << '\n';
    } catch (...) {
        // Diagnostics must never escape a native hook or violate noexcept.
        OutputDebugStringA("[HOTRELOAD] diagnostic file logging failed\n");
    }
}

} // namespace scrap::sdk::lua::internal
