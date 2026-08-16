#include "scrap_mechanic_sdk/lua_inspector.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

namespace scrap::sdk {
constexpr bool kCaptureInternalState = true;

bool safe_copy_from_game(const void *address, void *destination, std::size_t size) {
    return read_current_process(address, destination, size);
}

bool read_short_game_string(std::uintptr_t address, std::array<char, 96> &destination) {
    destination.fill('\0');
    if (!address)
        return false;
    for (std::size_t i = 0; i + 1 < destination.size(); ++i) {
        char ch{};
        if (!safe_copy_from_game(reinterpret_cast<const void *>(address + i), &ch, sizeof(ch)))
            return false;
        if (ch == '\0')
            return i != 0;
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20 || byte > 0x7E)
            return false;
        destination[i] = ch;
    }
    return false;
}

template <typename T> bool read_internal_field(const void *self, std::size_t offset, T &value) {
    if (!self || offset > static_cast<std::size_t>(-1) - sizeof(T))
        return false;
    return read_current_process(static_cast<const std::uint8_t *>(self) + offset, &value, sizeof(T));
}

template <typename T> bool read_typed_value(const T &game_value, T &value) {
    value = game_value;
    return true;
}

bool read_known_lifecycle_callback_name(HMODULE game_module,
    std::uintptr_t callback_hash,
    std::array<char, 96> &destination) {
    destination.fill('\0');
    if (!game_module || !callback_hash)
        return false;
    constexpr std::uintptr_t image_base = 0x140000000ULL;
    constexpr std::array<std::pair<std::uintptr_t, std::uintptr_t>, 11> callbacks{{{0x1419C74C8ULL, 0x1419C74D0ULL},
        {0x1419C74E8ULL, 0x1419C74F0ULL},
        {0x1419C7508ULL, 0x1419C7510ULL},
        {0x1419C7528ULL, 0x1419C7530ULL},
        {0x1419C7548ULL, 0x1419C7550ULL},
        {0x1419C7568ULL, 0x1419C7570ULL},
        {0x1419C7588ULL, 0x1419C7590ULL},
        {0x1419C75A8ULL, 0x1419C75B0ULL},
        {0x1419C75E8ULL, 0x1419C75F0ULL},
        {0x1419C7608ULL, 0x1419C7610ULL},
        {0x1419C7628ULL, 0x1419C7630ULL}}};
    const auto module = reinterpret_cast<const std::uint8_t *>(game_module);
    for (const auto &[key_rva_address, name_rva_address] : callbacks) {
        std::uintptr_t key{};
        std::uintptr_t name{};
        if (!safe_copy_from_game(module + (key_rva_address - image_base), &key, sizeof(key)) ||
            !safe_copy_from_game(module + (name_rva_address - image_base), &name, sizeof(name)))
            continue;
        if (key == callback_hash && read_short_game_string(name, destination))
            return true;
    }
    return false;
}

bool read_userdata_type_name(std::uintptr_t descriptor, std::string &name) {
    name.clear();
    std::uintptr_t text{};
    if (!descriptor || !read_internal_field(reinterpret_cast<const void *>(descriptor), 0, text))
        return false;
    std::array<char, 96> buffer{};
    if (!read_short_game_string(text, buffer))
        return false;
    name = buffer.data();
    return true;
}

bool read_lifecycle_callback_name(void *manager,
    std::intptr_t argument,
    std::uint8_t script_type,
    std::array<char, 96> &destination) {
    destination.fill('\0');
    std::uintptr_t callback_hash{};
    std::uintptr_t sentinel{};
    std::uintptr_t buckets{};
    std::uint64_t mask{};
    if (!manager || !argument || !read_internal_field(reinterpret_cast<const void *>(argument), 0, callback_hash) ||
        !read_internal_field(manager, 0x218, sentinel) || !sentinel || !read_internal_field(manager, 0x228, buckets) ||
        !buckets || !read_internal_field(manager, 0x240, mask))
        return false;

    std::uintptr_t callback_sentinel{};
    std::uintptr_t callback_buckets{};
    std::uint64_t callback_mask{};
    if (read_internal_field(manager, 0x1D8, callback_sentinel) && callback_sentinel &&
        read_internal_field(manager, 0x1E8, callback_buckets) && callback_buckets &&
        read_internal_field(manager, 0x200, callback_mask)) {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (callback_hash >> (byte * 8)) & 0xFF;
            hash *= 0x100000001B3ULL;
        }
        std::uintptr_t node{};
        if (read_internal_field(reinterpret_cast<const void *>(callback_buckets + (hash & callback_mask) * 16 + 8),
                0,
                node)) {
            for (std::size_t chain = 0; chain < 32 && node && node != callback_sentinel; ++chain) {
                std::uint64_t node_hash{};
                if (!read_internal_field(reinterpret_cast<const void *>(node), 0x10, node_hash))
                    break;
                if (node_hash == callback_hash) {
                    LuaVMStringState callback_string;
                    if (read_lua_string(reinterpret_cast<const void *>(node), 0x18, callback_string) &&
                        !callback_string.value.empty()) {
                        std::snprintf(destination.data(), destination.size(), "%s", callback_string.value.c_str());
                        return true;
                    }
                }
                if (!read_internal_field(reinterpret_cast<const void *>(node), 8, node))
                    break;
            }
        }
    }

    constexpr std::uint64_t fnv_offset = 0xCBF29CE484222325ULL;
    constexpr std::uint64_t fnv_prime = 0x100000001B3ULL;
    const auto bucket = (fnv_prime * (static_cast<std::uint64_t>(script_type) ^ fnv_offset)) & mask;
    std::uintptr_t node{};
    if (!read_internal_field(reinterpret_cast<const void *>(buckets + bucket * 16 + 8), 0, node))
        return false;
    for (std::size_t chain = 0; chain < 32 && node && node != sentinel; ++chain) {
        std::uint8_t node_type{};
        std::uintptr_t values{};
        if (!read_internal_field(reinterpret_cast<const void *>(node), 0x10, node_type) ||
            !read_internal_field(reinterpret_cast<const void *>(node), 0x30, values))
            break;
        if (node_type == script_type && values) {
            for (std::size_t entry = 0; entry < 64; ++entry) {
                const auto pair = values + 0x10 + entry * 0x20;
                std::uintptr_t key{};
                std::uintptr_t name{};
                if (!read_internal_field(reinterpret_cast<const void *>(pair), 0, key) ||
                    !read_internal_field(reinterpret_cast<const void *>(pair), 8, name))
                    break;
                if (!name)
                    break;
                if (key == callback_hash && read_short_game_string(name, destination))
                    return true;
            }
        }
        for (const std::size_t descriptor_offset : {std::size_t{0x28}, std::size_t{0x38}, std::size_t{0x40}}) {
            std::uintptr_t descriptor{};
            if (!read_internal_field(reinterpret_cast<const void *>(node), descriptor_offset, descriptor) ||
                !descriptor)
                continue;
            for (std::size_t entry = 0; entry < 32; ++entry) {
                const auto pair = descriptor + entry * 0x20;
                std::array<std::uintptr_t, 4> words{};
                if (!safe_copy_from_game(reinterpret_cast<const void *>(pair), words.data(), sizeof(words)))
                    break;
                std::size_t hash_word = words.size();
                for (std::size_t word = 0; word < words.size(); ++word)
                    if (words[word] == callback_hash) {
                        hash_word = word;
                        break;
                    }
                if (hash_word == words.size())
                    continue;
                for (std::size_t word = 0; word < words.size(); ++word)
                    if (word != hash_word && words[word] && read_short_game_string(words[word], destination))
                        return true;
            }
        }
        if (!read_internal_field(reinterpret_cast<const void *>(node), 8, node))
            break;
    }
    return false;
}

bool decode_lifecycle_event(HMODULE game_module,
    LuaManager *manager,
    std::intptr_t argument,
    LifecycleEventDetails &result) {
    result = {};
    if (!manager || !argument)
        return false;
    std::uint64_t identity_hash{};
    if (!read_internal_field(reinterpret_cast<const void *>(argument), 0x18, identity_hash))
        return false;
    result.identity_hash = identity_hash;
    result.script_type = static_cast<std::uint8_t>(identity_hash >> 32);
    std::uintptr_t callback_pointer{};
    if (!read_internal_field(reinterpret_cast<const void *>(argument), 0, callback_pointer))
        return true;
    std::array<char, 96> callback_name{};
    result.callback_resolved = read_short_game_string(callback_pointer, callback_name) ||
                               read_known_lifecycle_callback_name(game_module, callback_pointer, callback_name) ||
                               read_lifecycle_callback_name(manager, argument, result.script_type, callback_name);
    if (result.callback_resolved)
        result.callback_name = callback_name.data();
    return true;
}

void read_lua_manager(void *self, LuaManagerState &out) {
    if constexpr (!kCaptureInternalState)
        return;
    LuaManagerState snapshot{};
    snapshot.self = reinterpret_cast<std::uintptr_t>(self);
    if (!self)
        return;

    auto *typed = reinterpret_cast<const scrap::sdk::LuaManager *>(self);
    bool any = false;
    any = true;
#define READ_LUA_FIELD(member) any |= read_typed_value(typed->member, snapshot.member)
    READ_LUA_FIELD(callback_depth);
    READ_LUA_FIELD(callback_index);
    READ_LUA_FIELD(callback_active);
    READ_LUA_FIELD(callback_guard);
    READ_LUA_FIELD(callback_kind);
    READ_LUA_FIELD(is_server);
    READ_LUA_FIELD(callback_context_0x20);
    READ_LUA_FIELD(callback_context_0x28);
    READ_LUA_FIELD(callback_context_0x30);
    READ_LUA_FIELD(callback_context_0x38);
    READ_LUA_FIELD(callback_context_0x40);
    std::int64_t callback_context_id{};
    any |= read_typed_value(typed->callback_index, callback_context_id);
    snapshot.callback_context_id = static_cast<std::uint32_t>(callback_context_id);
    READ_LUA_FIELD(callback_context_type);
    READ_LUA_FIELD(callback_context_flags);
    READ_LUA_FIELD(container_0x0F8_begin);
    READ_LUA_FIELD(container_0x100_end);
    READ_LUA_FIELD(container_0x110_begin);
    READ_LUA_FIELD(container_0x118_end);
    READ_LUA_FIELD(container_0x150_begin);
    READ_LUA_FIELD(container_0x158_end);
    any |= read_typed_value(typed->callback_name_registry_begin, snapshot.container_0x1D0_begin);
    any |= read_typed_value(typed->callback_name_registry_end, snapshot.container_0x1D8_end);
    READ_LUA_FIELD(registry_storage);
    READ_LUA_FIELD(registry_buckets);
    READ_LUA_FIELD(registry_mask);
    READ_LUA_FIELD(container_0x250);
    READ_LUA_FIELD(container_0x258);
    READ_LUA_FIELD(container_0x268);
    READ_LUA_FIELD(container_mask_0x280);
    READ_LUA_FIELD(container_0x308);
    READ_LUA_FIELD(container_0x310);
    READ_LUA_FIELD(container_0x320);
    READ_LUA_FIELD(container_0x338);
    READ_LUA_FIELD(server_callbacks_begin);
    READ_LUA_FIELD(server_callbacks_end);
    READ_LUA_FIELD(server_callbacks_capacity);
    READ_LUA_FIELD(fixed_callbacks_begin);
    READ_LUA_FIELD(fixed_callbacks_end);
    READ_LUA_FIELD(fixed_callbacks_capacity);
    READ_LUA_FIELD(client_fixed_begin);
    READ_LUA_FIELD(client_fixed_end);
    READ_LUA_FIELD(client_fixed_capacity);
    READ_LUA_FIELD(client_update_begin);
    READ_LUA_FIELD(client_update_end);
    READ_LUA_FIELD(client_update_capacity);
    READ_LUA_FIELD(receive_update_begin);
    READ_LUA_FIELD(receive_update_end);
    READ_LUA_FIELD(receive_update_capacity);
    READ_LUA_FIELD(callback_count);
    READ_LUA_FIELD(fixed_cursor);
    READ_LUA_FIELD(lua_vm_control_block);
#undef READ_LUA_FIELD
    snapshot.readable = any;
    out = std::move(snapshot);
}

std::size_t callback_span(std::uintptr_t begin, std::uintptr_t end) {
    if (!begin || end < begin || end - begin > 0x100000)
        return 0;
    return static_cast<std::size_t>((end - begin) / sizeof(std::uintptr_t));
}

void snapshot_callback_vector(std::uintptr_t begin, std::uintptr_t end, CallbackVectorState &snapshot) {
    snapshot = {};
    snapshot.count = callback_span(begin, end);
    const auto shown = snapshot.count > snapshot.entries.size() ? snapshot.entries.size() : snapshot.count;
    for (std::size_t index = 0; index < shown; ++index) {
        std::uintptr_t entry_address{};
        if (!read_internal_field(reinterpret_cast<const void *>(begin),
                index * sizeof(std::uintptr_t),
                entry_address) ||
            !entry_address)
            continue;

        auto &entry = snapshot.entries[index];
        entry.address = entry_address;
        bool any = false;
#define READ_ENTRY_FIELD(member, offset)                                                                               \
    any |= read_internal_field(reinterpret_cast<const void *>(entry_address), offset, entry.member)
        READ_ENTRY_FIELD(field_0x10, 0x10);
        READ_ENTRY_FIELD(field_0x18, 0x18);
        READ_ENTRY_FIELD(field_0x20, 0x20);
        READ_ENTRY_FIELD(field_0x24, 0x24);
        READ_ENTRY_FIELD(field_0x28, 0x28);
        READ_ENTRY_FIELD(field_0x30, 0x30);
        READ_ENTRY_FIELD(field_0x38, 0x38);
#undef READ_ENTRY_FIELD
        entry.readable = any;
    }
}

void read_callback_entries(void *self, LuaManagerState &fields) {
    if constexpr (!kCaptureInternalState)
        return;
    if (!self)
        return;
    CallbackVectorState server;
    CallbackVectorState fixed;
    CallbackVectorState client_fixed;
    CallbackVectorState client_update;
    CallbackVectorState receive_update;
    snapshot_callback_vector(fields.server_callbacks_begin, fields.server_callbacks_end, server);
    snapshot_callback_vector(fields.fixed_callbacks_begin, fields.fixed_callbacks_end, fixed);
    snapshot_callback_vector(fields.client_fixed_begin, fields.client_fixed_end, client_fixed);
    snapshot_callback_vector(fields.client_update_begin, fields.client_update_end, client_update);
    snapshot_callback_vector(fields.receive_update_begin, fields.receive_update_end, receive_update);
    fields.server_callback_entries = server;
    fields.fixed_callback_entries = fixed;
    fields.client_fixed_entries = client_fixed;
    fields.client_update_entries = client_update;
    fields.receive_update_entries = receive_update;
}

void snapshot_hash_container(void *self, std::size_t offset, bool byte_key, HashContainerState &snapshot) {
    snapshot = {};
    snapshot.object_offset = offset;
    if (!self)
        return;
    bool any = false;
    any |= read_internal_field(self, offset + 0x00, snapshot.max_load_factor);
    any |= read_internal_field(self, offset + 0x08, snapshot.head);
    any |= read_internal_field(self, offset + 0x10, snapshot.size);
    any |= read_internal_field(self, offset + 0x18, snapshot.buckets);
    any |= read_internal_field(self, offset + 0x30, snapshot.bucket_mask);
    any |= read_internal_field(self, offset + 0x38, snapshot.bucket_count);
    snapshot.readable = any;
    if (!snapshot.head)
        return;

    std::uintptr_t node{};
    if (!read_internal_field(reinterpret_cast<const void *>(snapshot.head), 0x08, node))
        return;
    for (auto &entry : snapshot.nodes) {
        if (!node || node == snapshot.head)
            break;
        bool duplicate = false;
        for (const auto &previous : snapshot.nodes) {
            if (previous.address == node) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            break;
        entry.address = node;
        entry.readable = read_internal_field(reinterpret_cast<const void *>(node), 0x10, entry.key);
        if (byte_key) {
            std::uint8_t key{};
            entry.readable = read_internal_field(reinterpret_cast<const void *>(node), 0x10, key);
            entry.key = key;
        }
        read_internal_field(reinterpret_cast<const void *>(node), 0x18, entry.payload[0]);
        read_internal_field(reinterpret_cast<const void *>(node), 0x20, entry.payload[1]);
        read_internal_field(reinterpret_cast<const void *>(node), 0x28, entry.payload[2]);
        if (offset == 0x1D0) {
            LuaVMStringState name;
            if (read_lua_string(reinterpret_cast<const void *>(node), 0x18, name))
                entry.text = std::move(name.value);
        }
        std::uintptr_t next{};
        if (!read_internal_field(reinterpret_cast<const void *>(node), 0x08, next))
            break;
        node = next;
    }
}

void read_lua_vm(void *vm, LuaVMState &out);

bool refresh_lua_state(HMODULE game_module, LuaManagerState &manager, LuaVMState &vm) {
    manager = {};
    vm = {};
    if constexpr (!kCaptureInternalState)
        return false;
    if (!game_module)
        return false;
    auto *typed_manager = scrap::sdk::lua_manager(game_module);
    const auto manager_address = reinterpret_cast<std::uintptr_t>(typed_manager);
    if (!typed_manager || !manager_address)
        return false;
    read_lua_manager(typed_manager, manager);
    auto *typed_vm = typed_manager->lua_vm_shared_ptr.get();
    const auto lua_vm = reinterpret_cast<std::uintptr_t>(typed_vm);
    manager.lua_vm_shared_ptr = lua_vm;
    if (typed_vm)
        read_lua_vm(typed_vm, vm);
    read_callback_entries(reinterpret_cast<void *>(manager_address), manager);
    snapshot_hash_container(reinterpret_cast<void *>(manager_address),
        0x110,
        false,
        manager.registered_callback_hashes);
    snapshot_hash_container(reinterpret_cast<void *>(manager_address),
        reinterpret_cast<std::uintptr_t>(&typed_manager->callback_name_registry) - manager_address,
        false,
        manager.callback_name_registry);
    snapshot_hash_container(reinterpret_cast<void *>(manager_address),
        reinterpret_cast<std::uintptr_t>(&typed_manager->callback_type_registry) - manager_address,
        true,
        manager.callback_type_registry);
    return manager.readable;
}

bool read_lua_string(const void *vm, std::size_t offset, LuaVMStringState &result) {
    result = {};
    if (!vm)
        return false;
    // This is an injected internal view. The member is the game's actual
    // MSVC std::string, so read it as std::string rather than decoding its
    // implementation bytes into a second string abstraction.
    const auto *game_string = reinterpret_cast<const std::string *>(static_cast<const std::uint8_t *>(vm) + offset);
    result.value = *game_string;
    result.readable = true;
    return true;
}

bool read_vm_map(void *vm, std::size_t offset, LuaVMMapState &map) {
    map = {};
    map.object_offset = offset;
    bool any = false;
#define READ_VM_MAP(member, relative) any |= read_internal_field(vm, offset + (relative), map.member)
    READ_VM_MAP(max_load_factor, 0x00);
    READ_VM_MAP(sentinel, 0x08);
    READ_VM_MAP(size, 0x10);
    READ_VM_MAP(buckets, 0x18);
    READ_VM_MAP(bucket_end, 0x20);
    READ_VM_MAP(bucket_capacity, 0x28);
    READ_VM_MAP(bucket_mask, 0x30);
    READ_VM_MAP(bucket_count, 0x38);
#undef READ_VM_MAP
    map.readable = any;
    return any;
}

bool read_loaded_state_map(void *vm, LuaVMLoadedStateMap &map) {
    map = {};
    map.object_offset = 0x68;
    bool any = false;
    any |= read_internal_field(vm, 0x68, map.sentinel);
    any |= read_internal_field(vm, 0x70, map.buckets);
    any |= read_internal_field(vm, 0x78, map.bucket_count);
    any |= read_internal_field(vm, 0x80, map.cursor);
    any |= read_internal_field(vm, 0x88, map.size);
    map.readable = any;
    return any;
}

template <typename Entry> bool seen_vm_entry(const std::array<Entry, 16> &entries, std::uintptr_t address) {
    return std::any_of(entries.begin(), entries.end(), [address](const auto &entry) {
        return entry.address == address;
    });
}

void snapshot_lua_vm_entries(void *vm, LuaVMState &snapshot) {
    (void)vm;
    auto &scripts = snapshot.script_entries;
    auto &script_map = snapshot.script_cache;
    std::uintptr_t node{};
    if (script_map.sentinel && read_internal_field(reinterpret_cast<const void *>(script_map.sentinel), 0x08, node)) {
        for (auto &entry : scripts) {
            if (!node || node == script_map.sentinel || seen_vm_entry(scripts, node))
                break;
            entry = {};
            entry.address = node;
            bool any = false;
            any |= read_internal_field(reinterpret_cast<const void *>(node), 0x10, entry.key);
            any |= read_lua_string(reinterpret_cast<const void *>(node), 0x18, entry.path);
            any |= read_internal_field(reinterpret_cast<const void *>(node), 0x38, entry.status);
            any |= read_lua_string(reinterpret_cast<const void *>(node), 0x40, entry.resolved_path);
            any |= safe_copy_from_game(reinterpret_cast<const std::uint8_t *>(node) + 0x60,
                entry.identity.data(),
                entry.identity.size());
            any |= read_internal_field(reinterpret_cast<const void *>(node), 0x70, entry.loader_value);
            any |= read_internal_field(reinterpret_cast<const void *>(node), 0xC0, entry.registry_ref);
            entry.readable = any;
            if (!read_internal_field(reinterpret_cast<const void *>(node), 0x08, node))
                break;
        }
    }

    auto &userdata = snapshot.userdata_entries;
    node = 0;
    if (snapshot.userdata_types.sentinel &&
        read_internal_field(reinterpret_cast<const void *>(snapshot.userdata_types.sentinel), 0x08, node)) {
        for (auto &entry : userdata) {
            if (!node || node == snapshot.userdata_types.sentinel || seen_vm_entry(userdata, node))
                break;
            entry = {};
            entry.address = node;
            read_internal_field(reinterpret_cast<const void *>(node), 0x10, entry.type_id);
            read_internal_field(reinterpret_cast<const void *>(node), 0x18, entry.descriptor);
            if (!read_internal_field(reinterpret_cast<const void *>(node), 0x08, node))
                break;
        }
    }

    auto &environments = snapshot.environment_entries;
    node = 0;
    if (snapshot.environment_refs.sentinel &&
        read_internal_field(reinterpret_cast<const void *>(snapshot.environment_refs.sentinel), 0x08, node)) {
        for (auto &entry : environments) {
            if (!node || node == snapshot.environment_refs.sentinel || seen_vm_entry(environments, node))
                break;
            entry = {};
            entry.address = node;
            safe_copy_from_game(reinterpret_cast<const std::uint8_t *>(node) + 0x10,
                entry.key.data(),
                entry.key.size());
            read_internal_field(reinterpret_cast<const void *>(node), 0x20, entry.registry_ref);
            if (!read_internal_field(reinterpret_cast<const void *>(node), 0x08, node))
                break;
        }
    }

    auto &loaded = snapshot.loaded_script_entries;
    node = 0;
    if (snapshot.loaded_script_states.sentinel &&
        read_internal_field(reinterpret_cast<const void *>(snapshot.loaded_script_states.sentinel), 0x08, node)) {
        for (auto &entry : loaded) {
            if (!node || node == snapshot.loaded_script_states.sentinel || seen_vm_entry(loaded, node))
                break;
            entry = {};
            entry.address = node;
            (void)read_lua_string(reinterpret_cast<const void *>(node), 0x00, entry.path);
            read_internal_field(reinterpret_cast<const void *>(node), 0x20, entry.state_value);
            if (!read_internal_field(reinterpret_cast<const void *>(node), 0x08, node))
                break;
        }
    }
}

void read_lua_vm(void *vm, LuaVMState &out) {
    LuaVMState snapshot{};
    snapshot.self = reinterpret_cast<std::uintptr_t>(vm);
    if (!vm)
        return;
    auto *typed = reinterpret_cast<const scrap::sdk::LuaVM *>(vm);
    bool any = false;
#define READ_VM(member) any |= read_typed_value(typed->member, snapshot.member)
    READ_VM(lua_state);
    any |= safe_copy_from_game(reinterpret_cast<const std::uint8_t *>(vm) + 0x90,
        snapshot.current_script_identity.data(),
        snapshot.current_script_identity.size());
    READ_VM(weak_registry_ref);
    READ_VM(active_function_ref);
    READ_VM(method_stack_begin);
    READ_VM(method_stack_end);
    READ_VM(method_stack_capacity);
    READ_VM(profiler_threshold_or_scale);
    READ_VM(script_execution_hook);
    READ_VM(execution_guard);
    READ_VM(execution_state_hook);
    any |= safe_copy_from_game(reinterpret_cast<const std::uint8_t *>(vm) + 0x210,
        snapshot.inline_container_0.data(),
        sizeof(snapshot.inline_container_0));
    any |= safe_copy_from_game(reinterpret_cast<const std::uint8_t *>(vm) + 0x250,
        snapshot.inline_container_1.data(),
        sizeof(snapshot.inline_container_1));
    READ_VM(inline_container_boundary_0);
    READ_VM(inline_container_boundary_1);
    READ_VM(auxiliary_vector_begin);
    READ_VM(auxiliary_vector_end);
    READ_VM(auxiliary_vector_capacity);
    READ_VM(auxiliary_tail);
#undef READ_VM
    any |= read_lua_string(vm, 0x08, snapshot.environment_name);
    read_vm_map(vm, 0x28, snapshot.script_cache);
    read_loaded_state_map(vm, snapshot.loaded_script_states);
    auto *typed_vm = reinterpret_cast<const scrap::sdk::LuaVM *>(vm);
    const auto vm_base = reinterpret_cast<std::uintptr_t>(typed_vm);
    read_vm_map(vm,
        reinterpret_cast<std::uintptr_t>(&typed_vm->userdata_types_begin) - vm_base,
        snapshot.userdata_types);
    read_vm_map(vm,
        reinterpret_cast<std::uintptr_t>(&typed_vm->environment_refs_begin) - vm_base,
        snapshot.environment_refs);
    snapshot_lua_vm_entries(vm, snapshot);
    snapshot.readable = any;
    out = std::move(snapshot);
}


} // namespace scrap::sdk
