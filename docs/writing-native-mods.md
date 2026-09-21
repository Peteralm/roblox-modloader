# Writing native mods

A native mod is a C++ DLL that exports a `mod_base` instance. The loader discovers it, calls into
its lifecycle, and manages any hooks it registers. Native mods are the right choice when you need to
intercept engine functions or work at a lower level than the .NET API allows.

A minimal template is [`examples/basic-mod`](../examples/basic-mod).

## Project setup

Link against RML with CMake via `FetchContent`, then mark your target as a mod with
`roblox_add_mod`:

```cmake
include(FetchContent)

FetchContent_Declare(
    rml
    GIT_REPOSITORY https://github.com/revolutionxk/roblox-modloader.git
    GIT_TAG <release-tag>
)
FetchContent_MakeAvailable(rml)

add_library(my_mod SHARED mod.cpp)
roblox_add_mod(my_mod)
```

Build the project, then copy the resulting DLL into `RobloxModLoader/mods/<your-mod>/native/`.

## The mod class

Derive from `mod_base`, set your metadata in the constructor, and implement `on_load` / `on_unload`.
Export `start_mod` and `uninstall_mod` so the loader can create and destroy your instance.

```cpp
#include <RobloxModLoader/mod/mod_base.hpp>
#include <RobloxModLoader/logger/logger.hpp>

class my_mod final : public mod_base {
public:
    my_mod() {
        name = "My Mod";
        version = "1.0.0";
        author = "You";
        description = "What it does";
    }

    void on_load() override {
        logger::get_logger("MyMod")->info("Loaded.");
    }

    void on_unload() override {
        // Undo anything started in on_load.
    }
};

extern "C" {
    __declspec(dllexport) mod_base* start_mod() { return new my_mod(); }
    __declspec(dllexport) void uninstall_mod(const mod_base* mod) { delete mod; }
}
```

## Hooking

Native mods can intercept engine functions through the loader's hooking system. Define a hook with
the same signature as the target, call the original via `hooking::get_original`, and register it in
`on_load`:

```cpp
#include <RobloxModLoader/hooking/hooking.hpp>

namespace mod::hooks {
    static uint64_t* example_hook(uint64_t* instance, uint64_t param) {
        // pre-call logic
        auto result = hooking::get_original<&example_hook>()(instance, param);
        // post-call logic
        return result;
    }
}

// in on_load():
hooking::detour_hook_helper::add<&mod::hooks::example_hook>("EXAMPLE_HOOK", target_address);
```

The loader installs and removes hooks as part of the mod lifecycle, so you do not need to tear them
down manually in `on_unload`. Resolve `target_address` from the engine yourself (for example with a
pattern scan or a known offset); see [`examples/internal_developer`](../examples/internal_developer)
for a working hook.

## Finding the target

A byte signature pins the instructions the compiler happened to emit, so it dies on the update that
adds a local variable somewhere nearby. A string literal comes from the source and survives that
churn, which makes it the most durable anchor available. `rml::memory::range` — and therefore
`rml::memory::module` — can search for one and for the code that loads it:

```cpp
const rml::memory::module studio{rml::platform::studio_image_name()};

// Exactly one literal and exactly one instruction loading it, or the mod refuses to run.
const auto markers = studio.scan_strings("[Internal]", 2);
const auto references = studio.scan_rip_references(markers.front(), 2);
```

`scan_rip_references` decodes `lea r64, [rip + disp32]`, so it is x86-64 only and returns nothing
elsewhere. From the reference, walk back to the `call` that precedes it and check the callee looks
like what you expect before using it; see
[`examples/internal_developer`](../examples/internal_developer) for the whole chain.

Ask for one more match than you need (`limit` of 2 when you expect 1). A second match means the
anchor is ambiguous in this build, and acting on the first one would write to the wrong address.
Pattern scanning through `make_batch` is still there for targets no string reaches.

## Notes

- Keep hook bodies small and fast — they run on engine threads.
- Match the target's calling convention and signature exactly; a mismatch will corrupt the stack.
- Prefer the .NET API for anything that does not specifically need native interception; it is safer
  and easier to maintain.
