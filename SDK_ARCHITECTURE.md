# Scrap Mechanic SDK architecture rules

This document defines the ownership and extension rules for the SDK. It is intentionally short: the goal is to keep future reverse-engineered features easy to place without recreating the current namespace drift.

## Public namespace layout

```text
scrap::sdk
├── core          cross-cutting primitives
├── interceptors  synchronous hook-time interception
├── events        asynchronous process-wide event delivery
├── game          stable game-facing concepts
├── lua           Lua concepts and native Lua integration
│   ├── hooks
│   ├── reload_bridge
│   └── diagnostics
├── memory        memorylib-facing utilities when they become public
└── rendering     rendering concepts when reverse engineered
```

Synchronous hook notifications live under `lua::interceptors`; asynchronous
copied notifications live under `lua::events`. New public APIs must use the
domain namespaces above.

## Ownership rules

1. `core` contains reusable infrastructure only. It must not know about Lua, mods, notifications, Steam paths, or a particular game subsystem.
2. `game` contains stable concepts exposed by the game: managers, achievements, networking, serialization, mode, and console.
3. `lua` contains Lua VM/manager concepts, native hook coordination, script inspection, and the native reload bridge.
4. The SDK owns native hooks, address resolution, ABI overlays, pointer validation, and safe-point coordination.
5. Feature policy belongs outside the SDK. File watching, debounce policy, UI notifications, VS Code actions, and user preferences belong to the consuming DLL or application.
6. A reverse-engineered feature must have its own narrow module. Do not add unrelated functions to `runtime`, `api`, or `lua_hooks` merely because they are convenient.

## Resolver rules

1. Keep signatures, strings, xrefs, and version-specific layouts private to the implementation of the feature they resolve.
2. Prefer string/xref evidence, then RIP-relative data-flow resolution, then byte patterns as a secondary discriminator.
3. Validate every resolved address against the expected module and section, and validate pointers before dereferencing them.
4. A failed resolver disables only its capability. It must not guess an old RVA or silently use an unsafe fallback.
5. Record the evidence, assumptions, binary identity, and failure reason in the implementation or the research notes.
6. IDA changes may add names, types, comments, and bookmarks, but must not patch executable bytes without explicit approval.

## Public API rules

1. Public functions describe concepts, not implementation details. Prefer `game::achievements::enabled()` over an API exposing a global address.
2. Use nouns for state objects and verbs for operations: `lua::hooks::install`, `lua::reload_bridge::submit_change`.
3. Use `interceptors` for synchronous hook-time callbacks and `events` for
asynchronous copied notifications. Never use an asynchronous event to mutate
live game objects.
4. Return capability/status information where resolution can fail. Do not report success merely because a DLL is loaded.
5. Do not expose raw native pointers or ABI overlay fields unless the caller explicitly needs a reverse-engineering/inspection API.
6. Keep compatibility wrappers thin and mark them as legacy. New code must not depend on them.

## Dependency direction

```text
consumer DLL/application
        ↓
public SDK facade (core/game/lua)
        ↓
native feature implementation and resolvers
        ↓
memorylib / Windows primitives
```

## Internal implementation boundaries

The shared SDK keeps native Lua concerns separated even though consumers use a
small public facade:

- `lua_hooks.cpp` contains the ABI detours and public lifecycle wrappers.
- `lua_reload_coordinator.*` owns manager/VM associations and deferred refresh
  requests. It never invokes game code while holding its mutex.
- `lua_inspector.cpp` contains guarded layout reads and snapshot decoding.
- `events.cpp` owns the process-wide asynchronous dispatcher and lifecycle.

The hot-reload DLL has a parallel policy boundary:

- `source_paths.*` owns root discovery and logical/physical path translation.
- `source_service.*` owns file reads and directory notification workers.
- `notification_text.*` owns pure text and URI conversion.
- `shell_notifications.cpp` owns the notification window and chooses toast or
  legacy fallback delivery.

This separation is deliberate: native detours and safety coordination belong
to the SDK, while filesystem and user-interface policy belongs to the DLL.

The SDK must not depend on the hot-reload DLL. The DLL may depend on the SDK and may implement policy around SDK capabilities.

## Adding a new reverse-engineered feature

1. Identify its domain (`game`, `lua`, `rendering`, `networking`, or another justified domain).
2. Create a focused implementation/resolver module.
3. Add a stable public capability API only after the native evidence is understood.
4. Keep version-specific addresses and structure details private.
5. Add independent status and fail-closed behavior.
6. Add IDA annotations and a short evidence note.
7. Add a focused test or runtime diagnostic before exposing the feature to consumers.
8. Add compatibility forwarding only if an existing consumer needs it.

## Current migration status

- `scrap::sdk::lua::hooks` is the preferred hook lifecycle API.
- `scrap::sdk::lua::reload_bridge` is the preferred source-provider/change-submission API.
- `scrap::sdk::lua::diagnostics` is the preferred diagnostic-state API.
- `scrap::sdk::game` is the preferred game facade.
- The old root-level compatibility namespaces have been removed from the active SDK. In-repository consumers are required to use the domain API.
- Filesystem watching and notifications remain in `projects/lua_hot_reload`, not the SDK.
- `LuaManager::GetInstance()` and the singleton macro are not part of the public ABI overlay. Manager access goes through the resolver facade; the current legacy profile fallback remains isolated in the implementation until a fully pattern-resolved manager singleton is added.
- Runtime helpers have domain entry points in `game_console.hpp`, `game_mode.hpp`, and `lua_identity.hpp`; `runtime.hpp` remains the compatibility/implementation aggregate.
