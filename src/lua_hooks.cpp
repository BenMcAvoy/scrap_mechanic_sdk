#include "scrap_mechanic_sdk/lua_hooks.hpp"
#include "scrap_mechanic_sdk/lua_events.hpp"

#include "lua_diagnostics.hpp"
#include "lua_reload_coordinator.hpp"
#include "memorylib/memorylib.hpp"
#include "scrap_mechanic_sdk/hook_registry.hpp"
#include "scrap_mechanic_sdk/lua_inspector.hpp"
#include "scrap_mechanic_sdk/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>

namespace scrap::sdk::lua {
namespace {

    using FixedUpdateFn = char *(__fastcall *)(void *);
    using VmRefreshFn = std::intptr_t(__fastcall *)(void *, std::intptr_t);
    using ReloadChangedFn = void(__fastcall *)(LuaVM *);
    using RawCacheReadFn = std::int64_t(__fastcall *)(void *, void *, std::int64_t, std::int64_t);
    using CacheMembershipFn = bool(__fastcall *)(void *, const std::string *);
    using LuaScriptLoadFn = std::int64_t(__fastcall *)(void *, std::intptr_t, void *, char);
    using LifecycleFn = char(__fastcall *)(void *, std::intptr_t, std::uint8_t, int);

    HookState make_initial_hook_state() {
        HookState state{};
        state.initialize.label = "LuaManager initialize";
        state.initialize.anchor = "Initializing LuaManager as client";
        state.script_load.label = "GameScript select/load";
        state.script_load.anchor = "Z:\\Build\\sm_steam\\ContraptionCommon\\GameScript.cpp";
        state.client_update.label = "client_onUpdate dispatcher";
        state.client_update.anchor = "client_onUpdate callback - callback during ongoing callback '";
        state.fixed_update.label = "fixed-update dispatcher";
        state.fixed_update.anchor = "server_onFixedUpdate callback - callback during ongoing callback '";
        state.vm_refresh.label = "VM refresh";
        state.vm_refresh.anchor = "onRefreshVMCallback - create class during ongoing callback  '";
        state.client_data.label = "client data update";
        state.client_data.anchor = "client_onClientDataUpdate - callback during ongoing callback '";
        state.lifecycle.label = "lifecycle dispatcher";
        state.lifecycle.anchor = "!bInstant || ((tls_threadContext == ThreadContext::Synchronized) || "
                                 "(tls_threadContext == ThreadContext::PreRend) || (tls_threadContext == "
                                 "ThreadContext::Logic || tls_threadContext == ThreadContext::LogicSync))";
        return state;
    }

    HookState g_state = make_initial_hook_state();
    HMODULE g_game_module{};
    const volatile std::uintptr_t *g_raw_cache_manager_global{};
    mem::hook::Function<FixedUpdateFn> g_fixed_update;
    mem::hook::Function<VmRefreshFn> g_vm_refresh;
    mem::hook::Function<ReloadChangedFn> g_reload_changed;
    mem::hook::Function<LifecycleFn> g_lifecycle;
    mem::hook::Function<RawCacheReadFn> g_raw_cache_read;
    mem::hook::Function<CacheMembershipFn> g_cache_membership;
    mem::hook::Function<LuaScriptLoadFn> g_lua_script_load;
    HookRegistry g_registry;
    std::atomic_bool g_reload_active{};
    std::atomic_bool g_native_change_pending{};
    std::atomic_uint64_t g_reload_serial{};

    // Manager/VM pairing and deferred class refreshes are coordinated together.
    // The reload hook only queues work; the update hooks drain it at a safe point.
    std::mutex g_changed_path_mutex;
    std::wstring g_last_changed_path;
    std::mutex g_source_provider_mutex;
    NativeSourceProvider g_source_provider{};
    std::mutex g_native_script_path_mutex;

    struct NativeScriptPath {
        LuaVM *vm{};
        std::string path;
    };

    std::array<NativeScriptPath, 32> g_native_script_paths{};
    std::size_t g_native_script_path_count{};
    // The following state is split by ownership: changed paths and source
    // providers belong to the reload bridge, while manager/VM state belongs to the
    // native hook coordinator. Do not merge these locks into the game callbacks.
    HookStatus g_raw_cache_status{"Lua raw-cache reader", "Raw cache miss! Path: '", nullptr, false, {}};
    HookStatus g_cache_membership_status{"Lua reload cache membership",
        "Lua reload cache-membership predicate",
        nullptr,
        false,
        {}};
    HookStatus g_lua_script_load_status{"Lua reload source loader", "Lua reload source loader", nullptr, false, {}};
    HookStatus g_reload_changed_status{"Lua native changed-script reload",
        "LuaVM reload-changed-scripts function",
        nullptr,
        false,
        {}};


    using internal::diagnostic_log;
    NativeSourceProvider source_provider() noexcept;

    NativeSourceProvider source_provider() noexcept {
        std::lock_guard lock(g_source_provider_mutex);
        return g_source_provider;
    }

    std::optional<std::string> logical_source_path(const std::wstring &physical) {
        const auto provider = source_provider();
        std::string logical;
        return provider.logical_path && provider.logical_path(provider.context, physical, logical)
                   ? std::optional<std::string>(std::move(logical))
                   : std::nullopt;
    }

    bool is_lua_source(const std::string *path) noexcept {
        const auto provider = source_provider();
        return provider.is_lua_source && provider.is_lua_source(provider.context, path);
    }

    bool read_source(SourceBuffer *buffer, const std::string *path) noexcept {
        const auto provider = source_provider();
        return provider.read_source && provider.read_source(provider.context, buffer, path);
    }

    bool resolve_source_path(const std::string *path, std::wstring &physical) noexcept {
        const auto provider = source_provider();
        return provider.resolve_path && provider.resolve_path(provider.context, path, physical);
    }

    void remember_manager_vm(LuaManager *manager, LuaVM *vm) {
        internal::reload_coordinator().remember_manager_vm(manager, vm);
    }

    LuaManager *manager_for_vm(LuaVM *vm) {
        if (!vm)
            return nullptr;
        return internal::reload_coordinator().manager_for_vm(vm);
    }

    std::string native_script_path_for_vm(LuaVM *vm) {
        std::lock_guard lock(g_native_script_path_mutex);
        for (std::size_t i = 0; i < g_native_script_path_count; ++i)
            if (g_native_script_paths[i].vm == vm)
                return g_native_script_paths[i].path;
        return {};
    }

    void clear_native_script_path() {
        std::lock_guard lock(g_native_script_path_mutex);
        g_native_script_path_count = 0;
    }

    void capture_native_script_path(LuaVM *vm, const std::string &path) {
        if (path.empty() || path.size() >= 4096)
            return;
        std::lock_guard lock(g_native_script_path_mutex);
        for (std::size_t i = 0; i < g_native_script_path_count; ++i) {
            if (g_native_script_paths[i].vm == vm) {
                g_native_script_paths[i].path = path;
                diagnostic_log("captured native Lua path vm=%p path=%s", vm, path.c_str());
                return;
            }
        }
        if (g_native_script_path_count < g_native_script_paths.size()) {
            g_native_script_paths[g_native_script_path_count++] = {vm, path};
            diagnostic_log("captured native Lua path vm=%p path=%s", vm, path.c_str());
        }
    }

    std::int64_t __fastcall hooked_raw_cache_read(void *output,
        void *path_object,
        std::int64_t cache_manager,
        std::int64_t extra) {
        // Native loader thread, synchronous. Only the currently changed and
        // resolvable Lua file is served from loose source. Every other request
        // must reach the game's reader, including workshop $CONTENT paths that
        // this mod cannot map to a physical file.
        auto original = g_raw_cache_read.original();
        const auto *path = reinterpret_cast<const std::string *>(path_object);
        bool is_changed_source = false;
        if (is_lua_source(path) && g_reload_active.load(std::memory_order_acquire)) {
            const auto changed = last_changed_path();
            std::wstring physical;
            is_changed_source = !changed.empty() && resolve_source_path(path, physical) &&
                                _wcsicmp(physical.c_str(), changed.c_str()) == 0;
            if (is_changed_source && read_source(reinterpret_cast<SourceBuffer *>(output), path))
                return 0;
        }

        if (is_changed_source) {
            diagnostic_log("lua reload loose read unavailable; preserving native cache path");
        }
        return original ? original(output, path_object, cache_manager, extra) : 13;
    }

    bool __fastcall hooked_cache_membership(void *cache, const std::string *path) {
        // Native cache thread, synchronous. This is deliberately a narrow bypass:
        // unrelated scripts remain on the game's normal cache-membership path.
        if (is_lua_source(path) && g_reload_active.load(std::memory_order_acquire)) {
            const auto changed = last_changed_path();
            std::wstring physical;
            if (!changed.empty() && resolve_source_path(path, physical) &&
                _wcsicmp(physical.c_str(), changed.c_str()) == 0) {
                diagnostic_log("lua reload cache membership bypassed changed path=%.*s",
                    path && path->size() < 4096 ? static_cast<int>(path->size()) : 0,
                    path && path->size() < 4096 ? path->data() : "");
                // Only the changed script must bypass the raw-cache timestamp.
                // Every other script stays on the game's native cache path.
                return false;
            }
        }
        auto original = g_cache_membership.original();
        return original ? original(cache, path) : false;
    }

    std::int64_t __fastcall hooked_lua_script_load(void *vm,
        std::intptr_t context,
        void *script_record,
        char use_cached_function) {
        // Native Lua loader thread, synchronous. Capture the logical path that the
        // game actually requested before forwarding to the original loader.
        auto original = g_lua_script_load.original();
        if (g_reload_active.load(std::memory_order_acquire)) {
            diagnostic_log("lua reload source loader call vm=%p record=%p cached=%d",
                vm,
                script_record,
                static_cast<int>(use_cached_function));
        }
        if (g_reload_active.load(std::memory_order_acquire) && script_record) {
            const auto *path = reinterpret_cast<const std::string *>(reinterpret_cast<std::byte *>(script_record) + 40);
            const auto changed = last_changed_path();
            std::wstring physical;
            if (is_lua_source(path) && !changed.empty() && resolve_source_path(path, physical) &&
                _wcsicmp(physical.c_str(), changed.c_str()) == 0) {
                capture_native_script_path(reinterpret_cast<LuaVM *>(vm), *path);
                if (use_cached_function) {
                    diagnostic_log("lua reload source path forced path=%.*s",
                        path->size() < 4096 ? static_cast<int>(path->size()) : 0,
                        path->size() < 4096 ? path->data() : "");
                    use_cached_function = 0;
                }
            }
        }
        return original ? original(vm, context, script_record, use_cached_function) : 0;
    }

    template <typename Fn>
    bool resolve_and_install(mem::Scan &scan, HookStatus &status, Fn detour, mem::hook::Function<Fn> &hook) {
        auto anchor = scan.string_xref(status.anchor, status.label);
        if (!anchor) {
            status.error = anchor.error.message;
            return false;
        }
        auto function = scan.containing_function(anchor.get(), status.label);
        if (!function) {
            status.error = function.error.message;
            return false;
        }
        status.address = const_cast<std::uint8_t *>(function.get());
        auto result = hook.install(reinterpret_cast<Fn>(status.address), detour);
        status.resolved = static_cast<bool>(result);
        if (!result)
            status.error = result.error.message;
        return static_cast<bool>(result);
    }

    // -----------------------------------------------------------------------------
    // Reload coordination and safe-point refresh
    // -----------------------------------------------------------------------------

    void activity(HookEventType type,
        void *self,
        std::intptr_t argument = 0,
        std::uint8_t kind = 0,
        int flags = 0,
        std::uint64_t identity_hash = 0,
        std::string detail = {}) {
        HookStatus *status = nullptr;
        switch (type) {
        case HookEventType::initialize:
            status = &g_state.initialize;
            break;
        case HookEventType::script_load:
            status = &g_state.script_load;
            break;
        case HookEventType::client_update:
            status = &g_state.client_update;
            break;
        case HookEventType::fixed_update:
            status = &g_state.fixed_update;
            break;
        case HookEventType::vm_refresh:
            status = &g_state.vm_refresh;
            break;
        case HookEventType::client_data:
            status = &g_state.client_data;
            break;
        case HookEventType::lifecycle:
            status = &g_state.lifecycle;
            break;
        }
        if (status) {
            ++status->calls;
            status->last_this = reinterpret_cast<std::uintptr_t>(self);
            status->last_thread = GetCurrentThreadId();
            status->last_tick = GetTickCount64();
        }
        interceptors().hook_activity.publish(
            {type, reinterpret_cast<LuaManager *>(self), argument, identity_hash, kind, flags, std::move(detail)});
    }

    void queue_class_refresh(LuaVM *vm) {
        // Called after native source reload returns. Queueing is intentionally
        // separate from refreshVM because reload_changed may run during a callback.
        const auto physical = last_changed_path();
        const auto captured = native_script_path_for_vm(vm);
        const auto logical = captured.empty() ? logical_source_path(physical) : std::optional<std::string>(captured);
        if (!logical || logical->empty()) {
            diagnostic_log("refresh skipped vm=%p reason=logical-path-missing physical=%ls", vm, physical.c_str());
            return;
        }

        auto *manager = manager_for_vm(vm);
        internal::RefreshRequest existing;
        auto &coordinator = internal::reload_coordinator();
        if (coordinator.contains_pending(vm, *logical, &existing)) {
            diagnostic_log("refresh coalesced vm=%p manager=%p seq=%llu logical=%s",
                vm,
                existing.manager,
                static_cast<unsigned long long>(existing.sequence),
                logical->c_str());
            return;
        }
        const auto request = coordinator.enqueue(vm, manager, physical, *logical);
        diagnostic_log("refresh queued vm=%p manager=%p seq=%llu logical=%s physical=%ls source=%s",
            vm,
            manager,
            static_cast<unsigned long long>(request.sequence),
            logical->c_str(),
            physical.c_str(),
            captured.empty() ? "filesystem" : "native-loader");
    }

    bool callback_state_clear(LuaManager *manager) {
        if (!manager)
            return false;
        return manager->callback_depth == 0 && manager->callback_guard == 0;
    }

    void drain_class_refreshes(LuaManager *manager) {
        // Safe-point consumer. This runs after an update dispatcher returns and
        // invokes native Lua only after callback_depth/callback_guard are clear.
        if (!manager)
            return;
        auto *vm = manager->lua_vm_shared_ptr.get();
        if (!vm) {
            diagnostic_log("refresh skipped manager=%p reason=vm-missing", manager);
            return;
        }

        internal::RefreshRequest request;
        auto &coordinator = internal::reload_coordinator();
        if (!coordinator.take_for_manager(manager, vm, request))
            return;

        if (!callback_state_clear(manager)) {
            const auto sequence = request.sequence;
            coordinator.defer(std::move(request));
            diagnostic_log("refresh deferred: callback active manager=%p vm=%p seq=%llu depth=%d guard=%u",
                manager,
                vm,
                static_cast<unsigned long long>(sequence),
                manager->callback_depth,
                static_cast<unsigned>(manager->callback_guard));
            return;
        }

        auto refresh = g_vm_refresh.original();
        if (!refresh) {
            diagnostic_log("refresh skipped manager=%p vm=%p seq=%llu reason=refresh-trampoline-missing",
                manager,
                vm,
                static_cast<unsigned long long>(request.sequence));
        } else if (!callback_state_clear(manager)) {
            const auto sequence = request.sequence;
            coordinator.defer(std::move(request));
            diagnostic_log("refresh deferred: callback active manager=%p vm=%p seq=%llu depth=%d guard=%u",
                manager,
                vm,
                static_cast<unsigned long long>(sequence),
                manager->callback_depth,
                static_cast<unsigned>(manager->callback_guard));
            return;
        } else {
            diagnostic_log("refresh begin manager=%p vm=%p seq=%llu logical=%s thread=%lu",
                manager,
                vm,
                static_cast<unsigned long long>(request.sequence),
                request.logical_path.c_str(),
                GetCurrentThreadId());
            refresh(manager, reinterpret_cast<std::intptr_t>(&request.logical_path));
            diagnostic_log("refresh returned manager=%p vm=%p seq=%llu logical=%s thread=%lu",
                manager,
                vm,
                static_cast<unsigned long long>(request.sequence),
                request.logical_path.c_str(),
                GetCurrentThreadId());
        }

        coordinator.finish_refresh();
        diagnostic_log("refresh completed manager=%p vm=%p seq=%llu",
            manager,
            vm,
            static_cast<unsigned long long>(request.sequence));
    }

    void __fastcall hooked_reload_changed(LuaVM *vm) {
        g_reload_serial.fetch_add(1, std::memory_order_relaxed);
        // Synchronous native reload hook. The original reload remains synchronous;
        // class-instance refresh is queued for a later safe point.
        diagnostic_log("native reload begin vm=%p", vm);
        g_reload_active.store(true, std::memory_order_release);
        auto original = g_reload_changed.original();
        bool success = false;
        if (original) {
            original(vm);
            success = true;
            queue_class_refresh(vm);
        } else {
            diagnostic_log("native reload skipped: reload trampoline unavailable");
        }
        g_reload_active.store(false, std::memory_order_release);
        const auto physical_path = last_changed_path();
        interceptors().source_reloaded.publish({vm, physical_path, success});
        events().publish(SourceReloadedEvent{reinterpret_cast<std::uintptr_t>(vm), physical_path, success});
        diagnostic_log("native reload return vm=%p success=%s", vm, success ? "true" : "false");
    }

    bool resolve_raw_cache_manager_global(mem::Scan &scan) noexcept {
        // IDA evidence: LuaVM_reloadChangedScripts reads g_raw_cache_manager with
        // a RIP-relative mov immediately followed by a null check. Resolve that
        // reference from the current image instead of assuming an RVA.
        auto reference = scan.pattern("48 8B 3D ?? ?? ?? ?? 48 85 FF 0F 84 ?? ?? ?? ?? "
                                      "49 8D 54 24 28 48 8B CF E8 ?? ?? ?? ??",
            "RawCache manager global reference");
        if (!reference) {
            g_raw_cache_manager_global = nullptr;
            diagnostic_log("raw-cache manager global unavailable: %s", reference.error.message.c_str());
            return false;
        }
        const auto target = mem::resolve_rip_target(reference.get());
        if (!target || !scan.sections().image.contains(target, sizeof(std::uintptr_t)) ||
            !mem::ProcessMemory::readable(target, sizeof(std::uintptr_t))) {
            g_raw_cache_manager_global = nullptr;
            diagnostic_log("raw-cache manager global rejected reference=%p target=%p", reference.get(), target);
            return false;
        }
        g_raw_cache_manager_global = reinterpret_cast<const volatile std::uintptr_t *>(target);
        diagnostic_log("raw-cache manager global resolved reference=%p global=%p", reference.get(), target);
        return true;
    }

    bool bump_native_cache_generation(HMODULE) noexcept {
        if (!g_native_change_pending.exchange(false, std::memory_order_acq_rel))
            return false;
        const auto manager_global = g_raw_cache_manager_global;
        if (!manager_global) {
            diagnostic_log("generation bump skipped: raw cache manager global is unresolved");
            return false;
        }
        const auto manager = *manager_global;
        if (!manager) {
            diagnostic_log("generation bump skipped: raw cache manager null global=%p", manager_global);
            return false;
        }
        InterlockedIncrement64(reinterpret_cast<volatile LONG64 *>(manager + 0x20));
        return true;
    }

    // -----------------------------------------------------------------------------
    // Native hook implementations
    // -----------------------------------------------------------------------------

    char *__fastcall hooked_fixed_update(void *self) {
        // Game fixed-update thread. This is both the generation-bump boundary and
        // a second safe point for deferred class refreshes.
        activity(HookEventType::fixed_update, self);
        auto *manager = reinterpret_cast<LuaManager *>(self);
        auto *vm = manager ? manager->lua_vm_shared_ptr.get() : nullptr;
        remember_manager_vm(manager, vm);
        if (g_native_change_pending.load(std::memory_order_acquire))
            diagnostic_log("fixed-update observed pending native change manager=%p", self);
        const bool native_change = bump_native_cache_generation(g_game_module);
        const auto reload_serial_before_update = g_reload_serial.load(std::memory_order_relaxed);
        auto original = g_fixed_update.original();
        auto result = original ? original(self) : nullptr;
        if (native_change && vm &&
            g_reload_serial.load(std::memory_order_relaxed) == reload_serial_before_update) {
            // The game's normal gate is dev-mode guarded and is not reached by
            // every LuaManager update path. The original update has completed,
            // so this is the safe point for the same native operation when the
            // game did not already invoke it.
            diagnostic_log("native generation bumped; invoking reload at fixed-update safe point");
            hooked_reload_changed(vm);
        }
        drain_class_refreshes(manager);
        interceptors().fixed_update.publish({reinterpret_cast<LuaManager *>(self), 0.0F});
        return result;
    }

    std::intptr_t __fastcall hooked_vm_refresh(void *self, std::intptr_t argument) {
        auto *manager = reinterpret_cast<LuaManager *>(self);
        auto *vm = manager ? manager->lua_vm_shared_ptr.get() : nullptr;
        remember_manager_vm(manager, vm);
        activity(HookEventType::vm_refresh, self, argument);
        diagnostic_log("native LuaManager_refreshVM entered manager=%p argument=%p",
            self,
            reinterpret_cast<void *>(argument));
        auto original = g_vm_refresh.original();
        const auto result = original ? original(self, argument) : 0;
        diagnostic_log("native LuaManager_refreshVM returned manager=%p result=%p",
            self,
            reinterpret_cast<void *>(result));
        return result;
    }

    char __fastcall hooked_lifecycle(void *self, std::intptr_t argument, std::uint8_t kind, int flags) {
        LifecycleEventDetails details;
        (void)decode_lifecycle_event(g_game_module, reinterpret_cast<LuaManager *>(self), argument, details);
        std::string text;
        if (!details.callback_name.empty())
            text = "script type=" + std::string(script_type_name(details.script_type)) +
                   " callback=" + details.callback_name;
        else if (details.identity_hash)
            text = "script type=" + std::string(script_type_name(details.script_type)) + " callback=unresolved";
        const auto event_text = text;
        activity(HookEventType::lifecycle, self, argument, kind, flags, details.identity_hash, std::move(text));
        if (details.callback_name.find("Refresh") != std::string::npos)
            diagnostic_log("lifecycle refresh dispatch manager=%p callback=%s kind=%u flags=%d",
                self,
                details.callback_name.c_str(),
                static_cast<unsigned>(kind),
                flags);
        interceptors().callback_dispatching.publish(
            {reinterpret_cast<LuaManager *>(self), details.identity_hash, details.script_type, kind != 0});
        events().publish(LifecycleEvent{reinterpret_cast<std::uintptr_t>(self),
            details.identity_hash,
            details.script_type,
            kind,
            flags,
            event_text});
        auto original = g_lifecycle.original();
        return original ? original(self, argument, kind, flags) : 0;
    }

} // namespace

const HookState &hook_state() noexcept {
    return g_state;
}

std::wstring last_changed_path() {
    std::lock_guard lock(g_changed_path_mutex);
    return g_last_changed_path;
}

void configure_native_source(const NativeSourceProvider &provider) noexcept {
    std::lock_guard lock(g_source_provider_mutex);
    g_source_provider = provider;
}

void notify_native_source_changed(std::wstring physical_path) noexcept {
    {
        std::lock_guard lock(g_changed_path_mutex);
        g_last_changed_path = std::move(physical_path);
    }
    clear_native_script_path();
    g_native_change_pending.store(true, std::memory_order_release);
}

void clear_native_source() noexcept {
    {
        std::lock_guard lock(g_source_provider_mutex);
        g_source_provider = {};
    }
    {
        std::lock_guard lock(g_changed_path_mutex);
        g_last_changed_path.clear();
    }
    g_native_change_pending.store(false, std::memory_order_release);
}

bool install_hooks(HMODULE game_module, bool install_client_update) {
    // Installation is performed once by the SDK owner. Every target is
    // resolved against the current module; failed targets stay disabled.
    (void)install_client_update;
    auto scan_result = mem::Scan::open(L"ScrapMechanic.exe", [](const mem::Diagnostic &) {});
    if (!scan_result) {
        diagnostic_log("install failed: scan open error");
        return false;
    }
    g_game_module = game_module;
    g_raw_cache_manager_global = nullptr;
    (void)mem::hook::initialize();
    auto &scan = scan_result.get();
    (void)resolve_raw_cache_manager_global(scan);
    auto reload_pattern = scan.pattern("48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 "
                                       "48 8D 6C 24 E1 48 81 EC E8 00 00 00 4C 8B F9 4C 8B 71 30 "
                                       "49 8B 36 49 3B F6 0F 84 ?? ?? ?? ?? 45 33 ED "
                                       "48 B8 25 23 22 84 E4 9C F2 CB",
        "LuaVM reload-changed-scripts function");
    // These helpers are validated against the current ScrapMechanic.exe IDB.
    // Keep the bindings in the SDK; the mod only consumes the reload event.
    g_registry.clear();
    g_registry.add(
        "Lua native changed-script reload",
        [&] {
            if (!reload_pattern) {
                g_reload_changed_status.error = reload_pattern.error.message;
                diagnostic_log("native reload resolve failed error=%s", g_reload_changed_status.error.c_str());
                return false;
            }
            g_reload_changed_status.address = const_cast<std::uint8_t *>(reload_pattern.get());
            auto result = g_reload_changed.install(reinterpret_cast<ReloadChangedFn>(g_reload_changed_status.address),
                hooked_reload_changed);
            g_reload_changed_status.resolved = static_cast<bool>(result);
            if (!result)
                g_reload_changed_status.error = result.error.message;
            diagnostic_log("native reload hook install=%s address=%p error=%s",
                result ? "ok" : "failed",
                g_reload_changed_status.address,
                g_reload_changed_status.error.c_str());
            return static_cast<bool>(result);
        },
        [] { (void)g_reload_changed.remove(); });
    g_registry.add(
        "fixed update",
        [&] {
            const bool ok = resolve_and_install(scan, g_state.fixed_update, hooked_fixed_update, g_fixed_update);
            diagnostic_log("fixed update hook install=%s address=%p error=%s",
                ok ? "ok" : "failed",
                g_state.fixed_update.address,
                g_state.fixed_update.error.c_str());
            return ok;
        },
        [] { (void)g_fixed_update.remove(); });
    g_registry.add(
        "Lua lifecycle dispatcher diagnostic",
        [&] {
            const bool ok = resolve_and_install(scan, g_state.lifecycle, hooked_lifecycle, g_lifecycle);
            diagnostic_log("lifecycle dispatcher hook install=%s address=%p error=%s",
                ok ? "ok" : "failed",
                g_state.lifecycle.address,
                g_state.lifecycle.error.c_str());
            return ok;
        },
        [] { (void)g_lifecycle.remove(); });
    g_registry.add(
        "Lua reload cache membership",
        [&] {
            auto pattern = scan.pattern("48 89 5C 24 08 48 89 6C 24 18 56 57 41 55 41 56 41 57 "
                                        "48 83 EC 50 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 48 "
                                        "4C 8B F2 4C 8B E9 33 ED 89 6C 24 20",
                g_cache_membership_status.label);
            if (!pattern) {
                g_cache_membership_status.error = pattern.error.message;
                diagnostic_log("cache membership resolve failed error=%s", g_cache_membership_status.error.c_str());
                return false;
            }
            g_cache_membership_status.address = const_cast<std::uint8_t *>(pattern.get());
            auto result = g_cache_membership.install(reinterpret_cast<CacheMembershipFn>(
                                                         g_cache_membership_status.address),
                hooked_cache_membership);
            g_cache_membership_status.resolved = static_cast<bool>(result);
            if (!result)
                g_cache_membership_status.error = result.error.message;
            diagnostic_log("cache membership install=%s address=%p error=%s",
                result ? "ok" : "failed",
                g_cache_membership_status.address,
                g_cache_membership_status.error.c_str());
            return static_cast<bool>(result);
        },
        [] { (void)g_cache_membership.remove(); });
    g_registry.add(
        "Lua reload source loader",
        [&] {
            auto pattern = scan.pattern("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 "
                                        "48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00 "
                                        "48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 A8 00 00 00",
                g_lua_script_load_status.label);
            if (!pattern) {
                g_lua_script_load_status.error = pattern.error.message;
                diagnostic_log("reload source loader resolve failed error=%s", g_lua_script_load_status.error.c_str());
                return false;
            }
            g_lua_script_load_status.address = const_cast<std::uint8_t *>(pattern.get());
            auto result = g_lua_script_load.install(reinterpret_cast<LuaScriptLoadFn>(g_lua_script_load_status.address),
                hooked_lua_script_load);
            g_lua_script_load_status.resolved = static_cast<bool>(result);
            if (!result)
                g_lua_script_load_status.error = result.error.message;
            diagnostic_log("reload source loader install=%s address=%p error=%s",
                result ? "ok" : "failed",
                g_lua_script_load_status.address,
                g_lua_script_load_status.error.c_str());
            return static_cast<bool>(result);
        },
        [] { (void)g_lua_script_load.remove(); });
    g_registry.add(
        "Lua VM class refresh diagnostic",
        [&] {
            // LuaManager_refreshVM at the current build: resolved by its stable
            // prologue, not by a fixed RVA. This is diagnostic only; the original
            // function remains responsible for rebuilding class definitions.
            auto pattern = scan.pattern("48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 "
                                        "48 8D AC 24 70 FF FF FF 48 81 EC 90 01 00 00 "
                                        "48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 80 00 00 00 "
                                        "48 8B DA 48 89 55 F0 4C 8B E9 48 8B 81 58 03 00 00",
                g_state.vm_refresh.label);
            if (!pattern) {
                g_state.vm_refresh.error = pattern.error.message;
                diagnostic_log("VM refresh resolve failed error=%s", g_state.vm_refresh.error.c_str());
                return false;
            }
            g_state.vm_refresh.address = const_cast<std::uint8_t *>(pattern.get());
            auto result = g_vm_refresh.install(reinterpret_cast<VmRefreshFn>(g_state.vm_refresh.address),
                hooked_vm_refresh);
            g_state.vm_refresh.resolved = static_cast<bool>(result);
            if (!result)
                g_state.vm_refresh.error = result.error.message;
            diagnostic_log("VM refresh hook install=%s address=%p error=%s",
                result ? "ok" : "failed",
                g_state.vm_refresh.address,
                g_state.vm_refresh.error.c_str());
            return static_cast<bool>(result);
        },
        [] { (void)g_vm_refresh.remove(); });
    g_registry.add(
        "Lua raw-cache bypass",
        [&] {
            const bool installed = resolve_and_install(scan,
                g_raw_cache_status,
                hooked_raw_cache_read,
                g_raw_cache_read);
            diagnostic_log("raw-cache bypass install=%s address=%p error=%s",
                installed ? "ok" : "failed",
                g_raw_cache_status.address,
                g_raw_cache_status.error.c_str());
            return installed;
        },
        [] { (void)g_raw_cache_read.remove(); });
    const bool installed = g_registry.install_all();
    return installed;
}

void remove_hooks() noexcept {
    // Called only after consumers have disconnected. Native hooks are removed
    // before the SDK releases memorylib state or the shared DLL is unloaded.
    g_reload_active.store(false, std::memory_order_release);
    g_reload_serial.store(0, std::memory_order_relaxed);
    internal::reload_coordinator().reset();
    clear_native_script_path();
    (void)g_reload_changed.remove();
    g_registry.clear();
    (void)mem::hook::uninitialize();
    g_game_module = nullptr;
}

} // namespace scrap::sdk::lua
