#include "scrap_mechanic_sdk/runtime.hpp"

#include "memorylib/memorylib.hpp"

#include <cstring>

namespace scrap::sdk::runtime {
namespace {

    ConsoleState g_console;
    ConsoleWriteFn g_console_writer{};
    const std::int32_t *g_mode_scalar{};

    bool readable(const void *address, std::size_t size) {
        return address && mem::ProcessMemory::readable(address, size);
    }

} // namespace

bool resolve_console(HMODULE) {
    auto scan_result = mem::Scan::open(L"ScrapMechanic.exe", [](const mem::Diagnostic &) {});
    if (!scan_result)
        return false;
    auto &scan = scan_result.get();
    auto first_anchor = scan.string_xref("Unable to allocate debug console", "UTILS::Console allocation xref");
    auto second_anchor = scan.string_xref("Unable to setup debug console", "UTILS::Console setup xref");
    auto first_message = scan.resolver().unique_string("Unable to allocate debug console",
        "UTILS::Console allocation diagnostic");
    auto second_message = scan.resolver().unique_string("Unable to setup debug console",
        "UTILS::Console setup diagnostic");
    if (!first_anchor || !second_anchor || !first_message || !second_message) {
        g_console.error = "UTILS::Console diagnostic strings could not be resolved";
        return false;
    }
    auto first_function = scan.containing_function(first_anchor.get(), "UTILS::Console allocation function");
    auto second_function = scan.containing_function(second_anchor.get(), "UTILS::Console setup function");
    if (!first_function || !second_function || first_function.get() != second_function.get()) {
        g_console.error = "UTILS::Console diagnostics could not be resolved";
        return false;
    }
    auto range = scan.function_range(first_function.get(), "UTILS::Console diagnostic function range");
    if (!range)
        return false;
    ConsoleWriteFn candidate{};
    for (const auto &site : mem::call_sites(range.get())) {
        bool references_message = false;
        for (const auto &instruction : site.preceding_instructions) {
            const auto referenced = mem::resolve_rip_target(instruction.address);
            if (referenced == first_message.get() || referenced == second_message.get()) {
                references_message = true;
                break;
            }
        }
        if (!references_message || !site.target())
            continue;
        auto target = reinterpret_cast<ConsoleWriteFn>(const_cast<std::uint8_t *>(site.target()));
        if (!candidate)
            candidate = target;
        else if (candidate != target) {
            g_console.error = "UTILS::Console logger resolution was ambiguous";
            return false;
        }
    }
    if (!candidate) {
        g_console.error = "UTILS::Console logger call was not found";
        return false;
    }
    g_console_writer = candidate;
    g_console.resolved = true;
    g_console.error.clear();
    return true;
}

const ConsoleState &console_state() noexcept {
    return g_console;
}

ConsoleWriteFn console_writer() noexcept {
    return g_console_writer;
}

void record_console_write() noexcept {
    ++g_console.writes;
}

bool resolve_game_mode(HMODULE, const void *script_function) {
    auto scan_result = mem::Scan::open(L"ScrapMechanic.exe", [](const mem::Diagnostic &) {});
    if (!scan_result || !script_function)
        return false;
    auto &scan = scan_result.get();
    auto range = scan.function_range(reinterpret_cast<const std::uint8_t *>(script_function),
        "GameScript mode function range");
    if (!range)
        return false;
    auto matches = scan.find(range.get(), "8B 05 ?? ?? ?? ?? 83 F8 0E");
    if (!matches || matches->size() != 1)
        return false;
    std::int32_t displacement{};
    std::memcpy(&displacement, matches->front() + 2, sizeof(displacement));
    const auto target = reinterpret_cast<const std::uint8_t *>(
        reinterpret_cast<std::uintptr_t>(matches->front() + 6) + displacement);
    if (!readable(target, sizeof(std::int32_t)))
        return false;
    g_mode_scalar = reinterpret_cast<const std::int32_t *>(target);
    return true;
}

GameModeState refresh_game_mode() noexcept {
    GameModeState state;
    state.scalar_resolved = g_mode_scalar != nullptr;
    state.scalar_address = reinterpret_cast<std::uintptr_t>(g_mode_scalar);
    if (!readable(g_mode_scalar, sizeof(*g_mode_scalar)))
        return state;
    state.raw_value = *g_mode_scalar;
    switch (state.raw_value) {
    case 14:
        state.mode = "Survival";
        state.class_name = "SurvivalGame";
        break;
    case 5:
        state.mode = "Challenge";
        state.class_name = "ChallengeGame";
        break;
    case 8:
        state.mode = "Menu";
        state.class_name = "MenuGame";
        break;
    case 0:
        state.mode = "Creative";
        state.class_name = "CreativeGame";
        break;
    case 1:
        state.mode = "Creative";
        state.class_name = "CreativeFlatGame";
        break;
    case 2:
        state.mode = "Creative";
        state.class_name = "ClassicCreativeGame";
        break;
    case 4:
    case 17:
        state.mode = "Creative";
        state.class_name = "CreativeCustomGame";
        break;
    case 7:
        state.mode = "Creative";
        state.class_name = "CreativeTerrainGame";
        break;
    default:
        state.mode = "unknown";
        state.class_name = "unknown";
        break;
    }
    return state;
}

ScriptIdentity identify_script_path(std::string_view path) {
    if (path.find("SurvivalGame.lua") != std::string_view::npos)
        return {"Survival", "SurvivalGame"};
    if (path.find("ChallengeGame.lua") != std::string_view::npos)
        return {"Challenge", "ChallengeGame"};
    if (path.find("MenuGame.lua") != std::string_view::npos)
        return {"Menu", "MenuGame"};
    if (path.find("CreativeGame.lua") != std::string_view::npos)
        return {"Creative", "CreativeGame family"};
    return {};
}

ScriptIdentity identify_lua_vm(const LuaVMState &vm) {
    for (const auto &entry : vm.script_entries) {
        if (!entry.address || entry.path.value.empty())
            continue;
        auto identity = identify_script_path(entry.path.value);
        if (!identity.mode.empty()) {
            identity.mode += " (observed in script cache)";
            return identity;
        }
    }
    return {};
}

const char *lifecycle_dispatch_mode_name(std::uint8_t kind) noexcept {
    return kind ? "instant" : "normal/deferred";
}

} // namespace scrap::sdk::runtime
