#include "scrap_mechanic_sdk/sdk.hpp"

#include "memorylib/memorylib.hpp"

#include <algorithm>
#include <mutex>

namespace scrap::sdk {

namespace {
    std::mutex g_manager_resolver_mutex;
    HMODULE g_manager_resolved_module{};
    const volatile std::uintptr_t *g_manager_global{};

    const volatile std::uintptr_t *resolve_manager_global(HMODULE module) noexcept {
        if (!module)
            return nullptr;
        std::lock_guard lock(g_manager_resolver_mutex);
        if (g_manager_resolved_module == module)
            return g_manager_global;

        g_manager_resolved_module = module;
        g_manager_global = nullptr;
        auto scan_result = mem::Scan::open(L"ScrapMechanic.exe", [](const mem::Diagnostic &) {});
        if (!scan_result || scan_result.get().module() != module)
            return nullptr;

        auto reference = scan_result.get().pattern("48 83 3D ?? ?? ?? ?? 00 74 ?? 41 B8 92 00 00 00 "
                                                   "48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? "
                                                   "4C 89 3D ?? ?? ?? ??",
            "LuaManager singleton global reference");
        if (!reference)
            return nullptr;

        const auto target = mem::resolve_rip_target(reference.get());
        if (!target || !scan_result.get().sections().image.contains(target, sizeof(std::uintptr_t)) ||
            !mem::ProcessMemory::readable(target, sizeof(std::uintptr_t)))
            return nullptr;

        g_manager_global = reinterpret_cast<const volatile std::uintptr_t *>(target);
        return g_manager_global;
    }
} // namespace

LuaManager *lua_manager(HMODULE module) noexcept {
    const auto slot = resolve_manager_global(module);
    if (!slot)
        return nullptr;
    LuaManager *manager{};
    (void)read_current_process(const_cast<const void *>(static_cast<const volatile void *>(slot)),
        static_cast<void *>(&manager),
        sizeof(LuaManager *));
    return manager;
}

bool read_current_process(const void *address, void *destination, std::size_t size) noexcept {
    if (!address || !destination || size == 0)
        return false;
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), address, destination, size, &copied) != FALSE && copied == size;
}

std::uintptr_t module_rva(HMODULE module, std::uintptr_t rva) noexcept {
    return reinterpret_cast<std::uintptr_t>(module) + rva;
}

const char *script_type_name(std::uint8_t type) noexcept {
    switch (type) {
    case 0:
        return "Game";
    case 1:
        return "Shape";
    case 2:
        return "Tool";
    case 3:
        return "World";
    case 4:
        return "Game";
    case 5:
        return "Character";
    case 6:
        return "Harvestable";
    case 7:
        return "Player";
    case 8:
        return "Unit";
    case 9:
        return "ScriptableObject";
    case 10:
        return "ClientScriptableObject";
    default:
        return "Custom script type";
    }
}

std::uint64_t fnv1a_64(const void *data, std::size_t size) noexcept {
    constexpr std::uint64_t offset = 0xCBF29CE484222325ULL;
    constexpr std::uint64_t prime = 0x100000001B3ULL;
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::uint64_t hash = offset;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

std::uint64_t fnv1a_64_u64(std::uint64_t value) noexcept {
    return fnv1a_64(&value, sizeof(value));
}

} // namespace scrap::sdk
