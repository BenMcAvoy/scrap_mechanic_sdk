#pragma once

#include <windows.h>

#include "scrap_mechanic_sdk/export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#define SM_SDK_PROP(type, name, offset)                                                                                \
    __declspec(property(get = get_##name)) type name;                                                                  \
    type &get_##name() {                                                                                               \
        return *reinterpret_cast<type *>(reinterpret_cast<std::uintptr_t>(this) + offset);                             \
    }                                                                                                                  \
    const type &get_##name() const {                                                                                   \
        return *reinterpret_cast<const type *>(reinterpret_cast<std::uintptr_t>(this) + offset);                       \
    }

namespace scrap::sdk {

[[nodiscard]] SM_SDK_API bool read_current_process(const void *address, void *destination, std::size_t size) noexcept;

template <typename T> [[nodiscard]] bool read(const void *base, std::size_t offset, T &value) noexcept {
    if (!base || offset > static_cast<std::size_t>(-1) - sizeof(T))
        return false;
    return read_current_process(static_cast<const std::uint8_t *>(base) + offset, &value, sizeof(T));
}

[[nodiscard]] SM_SDK_API std::uintptr_t module_rva(HMODULE module, std::uintptr_t rva) noexcept;

struct HashMapLayout {
    float max_load_factor;
    std::uint32_t reserved_04;
    std::uintptr_t sentinel;
    std::uint64_t size;
    std::uintptr_t buckets;
    std::byte reserved_20[0x10];
    std::uint64_t bucket_mask;
    std::uint64_t bucket_count;
};

static_assert(sizeof(HashMapLayout) == 0x40);

struct CallbackEntryState {
    bool readable{};
    std::uintptr_t address{};
    std::uint64_t field_0x10{};
    std::uint64_t field_0x18{};
    std::uint32_t field_0x20{};
    std::uint32_t field_0x24{};
    std::uint32_t field_0x28{};
    std::uint8_t field_0x30{};
    std::uint8_t field_0x38{};
};

struct CallbackVectorState {
    std::size_t count{};
    std::array<CallbackEntryState, 8> entries{};
};

struct HashNodeState {
    bool readable{};
    std::uintptr_t address{};
    std::uint64_t key{};
    std::array<std::uint64_t, 3> payload{};
    std::string text;
};

struct HashContainerState {
    bool readable{};
    std::size_t object_offset{};
    float max_load_factor{};
    std::uintptr_t head{};
    std::uint64_t size{};
    std::uintptr_t buckets{};
    std::uint64_t bucket_mask{};
    std::uint64_t bucket_count{};
    std::array<HashNodeState, 16> nodes{};
};

struct LuaManagerState {
    bool readable{};
    std::uintptr_t self{};
    std::int32_t callback_depth{};
    std::int64_t callback_index{};
    std::uint8_t callback_active{};
    std::uint8_t callback_guard{};
    std::uint8_t callback_kind{};
    std::uint8_t is_server{};
    std::uintptr_t callback_context_0x20{};
    std::uintptr_t callback_context_0x28{};
    std::uintptr_t callback_context_0x30{};
    std::uintptr_t callback_context_0x38{};
    std::uintptr_t callback_context_0x40{};
    std::uint32_t callback_context_id{};
    std::uint32_t callback_context_type{};
    std::uint32_t callback_context_flags{};
    std::uintptr_t container_0x0F8_begin{};
    std::uintptr_t container_0x100_end{};
    std::uintptr_t container_0x110_begin{};
    std::uintptr_t container_0x118_end{};
    std::uintptr_t container_0x150_begin{};
    std::uintptr_t container_0x158_end{};
    std::uintptr_t container_0x190_begin{};
    std::uintptr_t container_0x198_end{};
    std::uintptr_t container_0x1D0_begin{};
    std::uintptr_t container_0x1D8_end{};
    std::uintptr_t registry_storage{};
    std::uintptr_t registry_buckets{};
    std::uintptr_t registry_mask{};
    std::uintptr_t container_0x250{};
    std::uintptr_t container_0x258{};
    std::uintptr_t container_0x268{};
    std::uintptr_t container_mask_0x280{};
    std::uintptr_t container_0x308{};
    std::uintptr_t container_0x310{};
    std::uintptr_t container_0x320{};
    std::uintptr_t container_0x338{};
    std::uint32_t callback_count{};
    std::uint32_t fixed_cursor{};
    std::uintptr_t server_callbacks_begin{};
    std::uintptr_t server_callbacks_end{};
    std::uintptr_t server_callbacks_capacity{};
    std::uintptr_t fixed_callbacks_begin{};
    std::uintptr_t fixed_callbacks_end{};
    std::uintptr_t fixed_callbacks_capacity{};
    std::uintptr_t client_fixed_begin{};
    std::uintptr_t client_fixed_end{};
    std::uintptr_t client_fixed_capacity{};
    std::uintptr_t client_update_begin{};
    std::uintptr_t client_update_end{};
    std::uintptr_t client_update_capacity{};
    std::uintptr_t receive_update_begin{};
    std::uintptr_t receive_update_end{};
    std::uintptr_t receive_update_capacity{};
    std::uintptr_t lua_vm_shared_ptr{};
    std::uintptr_t lua_vm_control_block{};
    CallbackVectorState server_callback_entries{};
    CallbackVectorState fixed_callback_entries{};
    CallbackVectorState client_fixed_entries{};
    CallbackVectorState client_update_entries{};
    CallbackVectorState receive_update_entries{};
    HashContainerState registered_callback_hashes{};
    HashContainerState callback_type_registry{};
    HashContainerState callback_name_registry{};
};

struct LuaVMMapState {
    std::size_t object_offset{};
    bool readable{};
    float max_load_factor{};
    std::uintptr_t sentinel{};
    std::uint64_t size{};
    std::uintptr_t buckets{};
    std::uintptr_t bucket_end{};
    std::uintptr_t bucket_capacity{};
    std::uint64_t bucket_mask{};
    std::uint64_t bucket_count{};
};

// A standalone tool value, not a replacement for the game's std::string.
// ABI overlays expose std::string directly; this type exists only when a
// caller intentionally copies a value for UI/export after reading it live.
struct LuaStringSnapshot {
    bool readable{};
    std::string value;
};

using LuaVMStringState = LuaStringSnapshot;

struct LuaVMLoadedStateMap {
    std::size_t object_offset{0x68};
    bool readable{};
    std::uintptr_t sentinel{};
    std::uintptr_t buckets{};
    std::uint64_t bucket_count{};
    std::uint64_t cursor{};
    std::uint64_t size{};
};

struct LuaVMScriptEntryState {
    std::uintptr_t address{};
    bool readable{};
    std::uint64_t key{};
    LuaVMStringState path;
    std::uint64_t status{};
    LuaVMStringState resolved_path;
    std::array<std::uint8_t, 16> identity{};
    std::uint64_t loader_value{};
    std::int32_t registry_ref{-1};
};

struct LuaVMUserdataEntryState {
    std::uintptr_t address{};
    std::uint32_t type_id{};
    std::uintptr_t descriptor{};
};

struct LuaVMEnvironmentEntryState {
    std::uintptr_t address{};
    std::array<std::uint8_t, 16> key{};
    std::int32_t registry_ref{-1};
};

struct LuaVMLoadedScriptEntryState {
    std::uintptr_t address{};
    LuaVMStringState path;
    std::uint64_t state_value{};
};

struct LuaVMState {
    bool readable{};
    std::uintptr_t self{};
    std::uintptr_t lua_state{};
    LuaVMStringState environment_name;
    LuaVMMapState script_cache{.object_offset = 0x28};
    LuaVMLoadedStateMap loaded_script_states{};
    std::array<std::uint8_t, 16> current_script_identity{};
    std::int32_t weak_registry_ref{};
    std::int32_t active_function_ref{-1};
    std::uintptr_t method_stack_begin{};
    std::uintptr_t method_stack_end{};
    std::uintptr_t method_stack_capacity{};
    float profiler_threshold_or_scale{};
    std::uintptr_t script_execution_hook{};
    std::uintptr_t execution_guard{};
    std::uintptr_t execution_state_hook{};
    LuaVMMapState userdata_types{.object_offset = 0x190};
    LuaVMMapState environment_refs{.object_offset = 0x1D0};
    std::array<std::uint64_t, 8> inline_container_0{};
    std::array<std::uint64_t, 8> inline_container_1{};
    std::uintptr_t inline_container_boundary_0{};
    std::uintptr_t inline_container_boundary_1{};
    std::uintptr_t auxiliary_vector_begin{};
    std::uintptr_t auxiliary_vector_end{};
    std::uintptr_t auxiliary_vector_capacity{};
    std::uintptr_t auxiliary_tail{};
    std::array<LuaVMScriptEntryState, 16> script_entries{};
    std::array<LuaVMUserdataEntryState, 16> userdata_entries{};
    std::array<LuaVMEnvironmentEntryState, 16> environment_entries{};
    std::array<LuaVMLoadedScriptEntryState, 16> loaded_script_entries{};
};

enum class ScriptType : std::uint8_t {
    game = 0,
    shape = 1,
    tool = 2,
    world = 3,
    game_client = 4,
    character = 5,
    harvestable = 6,
    player = 7,
    unit = 8,
    scriptable_object = 9,
    client_scriptable_object = 10,
};

[[nodiscard]] SM_SDK_API const char *script_type_name(std::uint8_t type) noexcept;
[[nodiscard]] SM_SDK_API std::uint64_t fnv1a_64(const void *data, std::size_t size) noexcept;
[[nodiscard]] SM_SDK_API std::uint64_t fnv1a_64_u64(std::uint64_t value) noexcept;

// ABI overlays. These deliberately contain no ownership or C++ runtime
// state: they are views over objects owned by Mechanic.exe. Unknown regions
// remain named padding until IDA gives us evidence for their contents.
struct LuaVM;

struct LuaManager {
    SM_SDK_PROP(std::int32_t, callback_depth, 0x48);
    SM_SDK_PROP(std::int64_t, callback_index, 0x4C);
    SM_SDK_PROP(std::uint8_t, callback_active, 0x58);
    SM_SDK_PROP(std::uint8_t, callback_guard, 0x59);
    SM_SDK_PROP(std::uint8_t, callback_kind, 0x5A);
    SM_SDK_PROP(std::uint8_t, is_server, 0x354);

    SM_SDK_PROP(std::uintptr_t, callback_context_0x20, 0x20);
    SM_SDK_PROP(std::uintptr_t, callback_context_0x28, 0x28);
    SM_SDK_PROP(std::uintptr_t, callback_context_0x30, 0x30);
    SM_SDK_PROP(std::uintptr_t, callback_context_0x38, 0x38);
    SM_SDK_PROP(std::uintptr_t, callback_context_0x40, 0x40);
    SM_SDK_PROP(std::uint32_t, callback_context_type, 0x50);
    SM_SDK_PROP(std::uint32_t, callback_context_flags, 0x54);

    SM_SDK_PROP(std::uintptr_t, container_0x0F8_begin, 0x0F8);
    SM_SDK_PROP(std::uintptr_t, container_0x100_end, 0x100);
    SM_SDK_PROP(std::uintptr_t, container_0x110_begin, 0x110);
    SM_SDK_PROP(std::uintptr_t, container_0x118_end, 0x118);
    SM_SDK_PROP(std::uintptr_t, container_0x150_begin, 0x150);
    SM_SDK_PROP(std::uintptr_t, container_0x158_end, 0x158);
    SM_SDK_PROP(std::uintptr_t, container_0x190_begin, 0x190);
    SM_SDK_PROP(std::uintptr_t, container_0x198_end, 0x198);
    SM_SDK_PROP(HashMapLayout, callback_name_registry, 0x1D0);
    SM_SDK_PROP(HashMapLayout, callback_type_registry, 0x210);
    SM_SDK_PROP(std::uintptr_t, callback_name_registry_begin, 0x1D0);
    SM_SDK_PROP(std::uintptr_t, callback_name_registry_end, 0x1D8);
    SM_SDK_PROP(std::uintptr_t, registry_storage, 0x218);
    SM_SDK_PROP(std::uintptr_t, registry_buckets, 0x228);
    SM_SDK_PROP(std::uintptr_t, registry_mask, 0x240);
    SM_SDK_PROP(std::uintptr_t, container_0x250, 0x250);
    SM_SDK_PROP(std::uintptr_t, container_0x258, 0x258);
    SM_SDK_PROP(std::uintptr_t, container_0x268, 0x268);
    SM_SDK_PROP(std::uintptr_t, container_mask_0x280, 0x280);
    SM_SDK_PROP(std::uintptr_t, container_0x308, 0x308);
    SM_SDK_PROP(std::uintptr_t, container_0x310, 0x310);
    SM_SDK_PROP(std::uintptr_t, container_0x320, 0x320);
    SM_SDK_PROP(std::uintptr_t, container_0x338, 0x338);

    SM_SDK_PROP(std::uintptr_t, server_callbacks_begin, 0x290);
    SM_SDK_PROP(std::uintptr_t, server_callbacks_end, 0x298);
    SM_SDK_PROP(std::uintptr_t, server_callbacks_capacity, 0x2A0);
    SM_SDK_PROP(std::uintptr_t, fixed_callbacks_begin, 0x2A8);
    SM_SDK_PROP(std::uintptr_t, fixed_callbacks_end, 0x2B0);
    SM_SDK_PROP(std::uintptr_t, fixed_callbacks_capacity, 0x2B8);
    SM_SDK_PROP(std::uintptr_t, client_fixed_begin, 0x2C0);
    SM_SDK_PROP(std::uintptr_t, client_fixed_end, 0x2C8);
    SM_SDK_PROP(std::uintptr_t, client_fixed_capacity, 0x2D0);
    SM_SDK_PROP(std::uintptr_t, client_update_begin, 0x2D8);
    SM_SDK_PROP(std::uintptr_t, client_update_end, 0x2E0);
    SM_SDK_PROP(std::uintptr_t, client_update_capacity, 0x2E8);
    SM_SDK_PROP(std::uintptr_t, receive_update_begin, 0x2F0);
    SM_SDK_PROP(std::uintptr_t, receive_update_end, 0x2F8);
    SM_SDK_PROP(std::uintptr_t, receive_update_capacity, 0x300);
    SM_SDK_PROP(std::uint32_t, callback_count, 0x348);
    SM_SDK_PROP(std::uint32_t, fixed_cursor, 0x350);
    SM_SDK_PROP(std::shared_ptr<LuaVM>, lua_vm_shared_ptr, 0x358);
    SM_SDK_PROP(std::uintptr_t, lua_vm_control_block, 0x360);
};

struct LuaVM {
    SM_SDK_PROP(std::string, environment_name, 0x08);
    SM_SDK_PROP(HashMapLayout, script_cache, 0x28);
    SM_SDK_PROP(HashMapLayout, loaded_script_states, 0x68);
    SM_SDK_PROP(std::uintptr_t, lua_state, 0x00);
    SM_SDK_PROP(std::int32_t, weak_registry_ref, 0xA0);
    SM_SDK_PROP(std::int32_t, active_function_ref, 0xA4);
    SM_SDK_PROP(std::uintptr_t, method_stack_begin, 0xA8);
    SM_SDK_PROP(std::uintptr_t, method_stack_end, 0xB0);
    SM_SDK_PROP(std::uintptr_t, method_stack_capacity, 0xB8);
    SM_SDK_PROP(float, profiler_threshold_or_scale, 0xC8);
    SM_SDK_PROP(std::uintptr_t, script_execution_hook, 0x108);
    SM_SDK_PROP(std::uintptr_t, execution_guard, 0x148);
    SM_SDK_PROP(std::uintptr_t, execution_state_hook, 0x188);
    SM_SDK_PROP(std::uintptr_t, userdata_types_begin, 0x190);
    SM_SDK_PROP(std::uintptr_t, environment_refs_begin, 0x1D0);
    SM_SDK_PROP(HashMapLayout, userdata_types, 0x190);
    SM_SDK_PROP(HashMapLayout, environment_refs, 0x1D0);
    SM_SDK_PROP(std::uintptr_t, inline_container_boundary_0, 0x290);
    SM_SDK_PROP(std::uintptr_t, inline_container_boundary_1, 0x298);
    SM_SDK_PROP(std::uintptr_t, auxiliary_vector_begin, 0x2A0);
    SM_SDK_PROP(std::uintptr_t, auxiliary_vector_end, 0x2A8);
    SM_SDK_PROP(std::uintptr_t, auxiliary_vector_capacity, 0x2B0);
    SM_SDK_PROP(std::uintptr_t, auxiliary_tail, 0x2B8);
};

[[nodiscard]] SM_SDK_API LuaManager *lua_manager(HMODULE module = GetModuleHandleW(nullptr)) noexcept;

} // namespace scrap::sdk

#undef SM_SDK_PROP
