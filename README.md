# Scrap Mechanic SDK

Shared reverse-engineering SDK for Scrap Mechanic DLL projects.

The SDK is intentionally versioned around evidence from the current IDA database. Its public surface is typed ABI overlays, following the same property-based pattern as `scrapcheese`: a mod can use `reinterpret_cast<scrap::sdk::LuaManager *>(address)` and read named members such as `lua_vm_shared_ptr` or `callback_count`. The overlay keeps guarded copies at the process boundary because these objects disappear during world reloads.

Current scope:

- guarded native capability resolution for the current executable;
- guarded current-process memory reads;
- game `std::string`-compatible decoding;
- Lua script-type names and callback hash utilities;
- `scrap::sdk::LuaManager` and `scrap::sdk::LuaVM` named property overlays;
- layout-only game string and hash-map views used by those overlays;
- reusable callback, hash-container, Lua string/map, script-entry, and manager/VM inspection state types;
- manager lookup through the SDK resolver facade `scrap::sdk::lua_manager()`.
- synchronous Lua interceptors and a shared asynchronous event bus;
- `HookRegistry` for reverse-order detour ownership and unload cleanup.

The SDK is split into domain headers. `game.hpp` contains stable game access;
`lua.hpp` contains native Lua hooks, diagnostics, and the native-loader bridge;
`game_console.hpp`, `game_mode.hpp`, and `lua_identity.hpp` provide focused
domain entry points. `sdk.hpp` remains the low-level ABI/memory overlay header;
`lua_inspector.hpp` contains explicit inspection snapshots. The active SDK has
no compatibility namespace layer; in-repository consumers use the domain API
directly.

Game-facing overlays use the real MSVC types where the ABI is established:
`std::string` and `std::shared_ptr`. Snapshot types are only for copying values
into a debugger or exporter; they are not alternate representations of game
objects. Opaque container layouts remain layout views until their exact key and
value types are proven.

Do not add a new offset catalog when extending a reversed class. Add a named property to the appropriate overlay, document the evidence and confidence, and leave genuinely unknown regions as padding or explicitly named unknown members. Keep file watching, reload policy, notifications, and mod-specific UI/behavior in the consuming DLL or application.
