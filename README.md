# Scrap Mechanic SDK

The Scrap Mechanic SDK is a shared native library for DLL mods. It provides
common hooks, Lua inspection helpers, game memory utilities, and event APIs so
mods can share the same runtime instead of each carrying its own copy.

This project is intended for C++ mod developers who are building native DLLs
for Scrap Mechanic and loading them with Rivet.

## Installation

The SDK is distributed as a Thunderstore package named
`BenMcAvoy-ScrapMechanicSDK`.

Install the package in the Mods directory used by Rivet. The package contains:

```text
manifest.json
icon.png
README.md
scrap_mechanic_sdk.dll
```

A mod that uses the SDK should declare this dependency in its Thunderstore
manifest:

```json
"dependencies": [
    "BenMcAvoy-ScrapMechanicSDK-0.1.0"
]
```

Rivet loads the SDK before mods that depend on it.

## What it provides

The public headers are grouped by purpose:

- `api.hpp` is the main include for SDK consumers.
- `lifecycle.hpp` controls SDK startup and shutdown.
- `interceptors.hpp` provides synchronous native hook subscriptions.
- `events.hpp` provides asynchronous events delivered on the SDK event thread.
- `lua_interceptors.hpp` exposes synchronous Lua hook channels.
- `lua_events.hpp` defines asynchronous Lua event payloads.
- `game_console.hpp` provides access to the game's console.
- `lua_hooks.hpp` exposes the Lua hook and reload interfaces.

Synchronous interceptors run on the game thread that called the hook. They may
inspect or change live game state, but handlers must be short and must not wait
on other threads.

Asynchronous events are copied into SDK-owned storage and delivered later on
the SDK event thread. Their payloads use copied strings, numbers, flags, and
stable identifiers. They do not provide pointers that remain valid after the
native hook returns.

## Building a mod

Include the SDK headers and link against `scrap_mechanic_sdk.lib`. Place
`scrap_mechanic_sdk.dll` beside the finished mod DLL when testing locally.

The SDK uses a `memorylib` Git submodule. To build the SDK itself:

```powershell
git clone https://github.com/BenMcAvoy/scrap_mechanic_sdk.git
cd scrap_mechanic_sdk
git submodule update --init --recursive
xmake f -y -p windows -a x64 -m release
xmake b -r -y scrap_mechanic_sdk
```

The release DLL is written to:

```text
build/windows/x64/release/scrap_mechanic_sdk.dll
```

The GitHub Actions workflow builds the SDK and creates an upload-ready
Thunderstore zip. It runs on Windows and includes the required submodule.

## Compatibility

The SDK targets the current Scrap Mechanic executable layout. Native layouts
and function addresses can change when the game updates. The resolver checks
the executable before enabling version-specific features and disables a
feature when its required evidence is not available.

Native overlay types describe game objects that are only valid while the game
owns them. Do not retain pointers across world reloads or manager replacement.
Use the provided snapshot types when data needs to outlive a hook callback.

## Related projects

- [Rivet](https://github.com/ReDoIngMods/Rivet) loads native mod packages.
- [Lua hot reload](https://github.com/BenMcAvoy/lua_hot_reload) uses this SDK
  to reload Lua files through the game's native reload path.
