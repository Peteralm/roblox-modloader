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

## Global-init mods

A mod whose manifest declares `load_phase = "global_init"` is entered before Studio builds its
class registry, through two extra exports:

```cpp
#include <RobloxModLoader/mod/global_init_mod.hpp>

RML_EXPORT_GLOBAL_INIT_ABI_VERSION()

extern "C" RML_GLOBAL_INIT_EXPORT int rml_global_init(const RmlGlobalInitContext* context) noexcept
{
    // Never let an exception cross this boundary; return nonzero to fail.
}
```

`context->descriptors` is the only way to add a class the engine did not ship. It is valid for the
duration of the call and nowhere else:

- `find_class(name)` returns a live class descriptor, or `nullptr`.
- `begin_batch(count)` opens a reservation; `reserve_class` validates and stages one class;
  `abort_batch` discards the whole reservation; `commit_batch` publishes it and cannot fail.

Reserve every class first and commit once. `reserve_class` returns nonzero on refusal: `1` for a
malformed payload, `2` for a name collision or a descriptor whose own name does not match, `3` for
a duplicate within the batch, and `4` for a missing base class.

A reserved class is visible to reflection, but `Instance.new` on it is not supported yet: on
Studio 0.739 a descriptor carries no creation function of its own, so a descriptor copied from an
existing class cannot become buildable by editing its fields. Treat this API as class *metadata*
registration until the engine's creation path is mapped.

## Notes

- Keep hook bodies small and fast — they run on engine threads.
- Match the target's calling convention and signature exactly; a mismatch will corrupt the stack.
- Prefer the .NET API for anything that does not specifically need native interception; it is safer
  and easier to maintain.
