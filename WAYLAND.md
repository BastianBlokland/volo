# Wayland PAL Implementation — Progress Notes

## Current branch: `feature/wayland`

## What has been done

### 1. Code generation (`utilities/wlgen.c`)

A custom generator tool `wlgen` was written (analogous to `vkgen` for Vulkan) that reads
Wayland protocol XML files from disk and produces a single self-contained header and
implementation file — no dependency on system `wayland-client.h` or `wayland-scanner`.

Run via CMake:
```
cmake --build build --target run.wlgen
```

The generator is configured in the root `CMakeLists.txt` `run.wlgen` target, which passes:
- `$ENV{VOLO_WAYLAND_DATADIR}/wayland.xml` (core protocol)
- `$ENV{VOLO_WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml`

Both env vars are set by `flake.nix` from the `wayland-scanner` and `wayland-protocols` Nix packages.

#### What `wlgen` generates

**`libs/gap/src/wayland/wayland.h`** — self-contained header (no system includes beyond `core/forward.h`):
- Inline struct definitions: `wl_message`, `wl_interface`, `wl_array`
- `WL_MARSHAL_FLAG_DESTROY` constant
- `struct wl_proxy` forward declaration
- `WlFuncs` typedef: function pointer table for the 10 raw `libwayland-client.so` symbols (`display_connect/disconnect/dispatch/dispatch_pending/roundtrip/flush`, `proxy_add_listener/marshal_flags/get_version/destroy`). Note: `wl_display_get_registry` is NOT a libwayland export — it is generated as a typed wrapper.
- `wlLoad(DynLib*, WlFuncs*)` declaration: loads all symbols via dlsym
- `wlRegistryBind(...)` declaration: typed registry global bind helper
- Per-interface content (for all interfaces in all input XMLs):
  - Enum definitions
  - Request opcode `#define`s
  - Listener struct (event vtable)
  - Utility wrapper declarations: `<iface>_get_version`, `<iface>_destroy` (if no protocol destructor), `<iface>_add_listener` (if has events)
  - Request wrapper declarations: one typed function per request

**`libs/gap/src/wayland/wayland.c`** — implementation:
- `wlLoad` definition: calls `dynlib_symbol` for each of the 10 libwayland symbols
- `wlRegistryBind` definition: calls `proxy_marshal_flags(WL_REGISTRY_BIND, ...)` capping at `maxVersion`
- Per-message `static const struct wl_interface* <iface>_<msg>_types[]` arrays for any message whose signature contains `o` (object) or `n` (new_id) args — required by `wl_closure_lookup_objects` inside libwayland. Each entry points to `&<iface>_interface` for typed object/new_id args, or `null` for untyped ones. Untyped `new_id` occupies 3 slots (`sun`) each null.
- Per-interface `static const struct wl_message <iface>_requests[]` and `<iface>_events[]` arrays with proper argument signatures and types pointers (required by `proxy_marshal_flags` and `display_dispatch`/`display_roundtrip`)
- `const struct wl_interface` definitions for every protocol interface, referencing the message arrays
- All utility and request wrapper function definitions

The generated files are committed to the repository. Re-run `run.wlgen` after changing the XML inputs or the generator itself.

#### Known `wlgen` subtleties

- **`wl_display_get_registry` is not a libwayland export** — it is generated as a typed wrapper from the XML. It must NOT be in `WlFuncs` / loaded via dlsym.
- **`wl_seat_release` is version 5+** — do not call it on a seat with a lower version; use `wl_proxy_destroy` directly or version-guard the call.
- **Types arrays are required for `o`/`n` args** — `wl_closure_lookup_objects` inside libwayland dereferences `message->types[i]` for every object or new_id arg. If `message->types` is null, this crashes. `wlgen` generates `<iface>_<msg>_types[]` arrays for any such message.

### 2. PAL implementation (`libs/gap/src/pal_linux_wayland.c`)

Uses the same dlopen pattern as `pal_linux_xcb.c`. All protocol types, opcodes, and
function signatures come from the generated `wayland/wayland.h` — no manual definitions remain.

#### `Wayland` struct
```c
typedef struct {
  DynLib*             lib;
  WlFuncs             api;       // function pointer table, populated by wlLoad()
  struct wl_display*  display;
  struct wl_registry* registry;

  struct wl_compositor* compositor;
  u32                   compositorVersion;
  struct xdg_wm_base*   xdgWmBase;
  u32                   xdgWmBaseVersion;

  struct wl_seat*    seat;
  struct wl_pointer* pointer;

  struct wl_registry_listener  registryListener;
  struct xdg_wm_base_listener  xdgWmBaseListener;
  struct wl_seat_listener      seatListener;
  struct wl_pointer_listener   pointerListener;
} Wayland;
```

#### `GapPal` struct
```c
struct sGapPal {
  Allocator* alloc;
  DynArray   windows;       // GapPalWindow[]
  Wayland    wl;
  GapPalWindow* pointerFocus; // window currently under the pointer
};
```

#### `GapPalWindow` struct additions
```c
Wayland*             wl;           // back-pointer for use in listener callbacks
struct wl_surface*   wlSurface;
struct xdg_surface*  xdgSurface;
struct xdg_toplevel* xdgToplevel;
struct xdg_surface_listener  xdgSurfaceListener;   // stored here for lifetime
struct xdg_toplevel_listener xdgToplevelListener;  // stored here for lifetime
```

#### Initialization flow
`pal_init_wl(Wayland* out)`:
1. `dynlib_load` → `libwayland-client.so`
2. `wlLoad(&out->lib, &out->api)` → populates the `WlFuncs` table via dlsym
3. `out->api.display_connect(null)` → connect to default display
4. `wl_display_get_registry(display)` (generated wrapper, not a libwayland export)
5. `wl_registry_add_listener(...)` with `data = wl`, then `display_roundtrip` to enumerate globals
6. In `wl_registry_global`: bind `wl_compositor`, `xdg_wm_base`, and `wl_seat` via `wlRegistryBind`
7. `xdg_wm_base_add_listener(...)` for ping/pong keepalives

`pal_init_wl_seat(GapPal* pal)` — called after `GapPal*` is allocated so callbacks can look up windows:
1. `wl_seat_add_listener(...)` with `data = pal`
2. `display_roundtrip` → triggers `wl_seat_capabilities`
3. In `wl_seat_capabilities`: if pointer capability present, `wl_seat_get_pointer(...)`, then `wl_pointer_add_listener(...)` with `data = pal`

Note: registry callbacks use `data = Wayland*`; seat/pointer callbacks use `data = GapPal*` (needed to look up windows by surface pointer).

#### Window creation flow (`gap_pal_window_create`)
1. `wl_compositor_create_surface(...)` → `wl_surface*`
2. `xdg_wm_base_get_xdg_surface(..., wlSurface)` → `xdg_surface*`
3. `xdg_surface_get_toplevel(...)` → `xdg_toplevel*`
4. `xdg_surface_add_listener(...)` + `xdg_toplevel_add_listener(...)` for configure/close events
5. `xdg_toplevel_set_app_id(...)` → `"volo"` (used by window managers to float/tile the window)
6. `wl_surface_commit(...)` triggers compositor configure
7. `display_roundtrip` to process the configure event

#### `gap_pal_update` / `gap_pal_flush`
- `update`: clears volatile state, calls `api.display_dispatch_pending` (non-blocking)
- `flush`: calls `api.display_flush`

#### `gap_pal_window_title_set`
Calls `xdg_toplevel_set_title(...)` with a null-terminated scratch copy of the title string.

#### `gap_pal_native_app_handle`
Returns `(uptr)pal->wl.display` — needed by Vulkan for `VkWaylandSurfaceCreateInfoKHR`.

#### Mouse input
Pointer events are dispatched through the `wl_pointer_listener`:
- `enter`: sets `pal->pointerFocus` by matching `wl_surface*` to a window
- `leave`: clears `pal->pointerFocus`
- `motion`: converts `wl_fixed_t` → pixels (`>> 8`), flips Y axis (Wayland top-left vs. gap bottom-left), calls `pal_event_cursor`
- `button`: maps Linux `BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA` → `GapKey_MouseLeft/Right/Middle/X1/X2`, calls `pal_event_press` / `pal_event_release`
- `axis_discrete`: maps vertical/horizontal scroll to `pal_event_scroll`; raw `axis` events are ignored

Linux button defines (`BTN_LEFT = 0x110` etc.) are defined locally — no `<linux/input.h>` dependency.

### 3. `flake.nix`

- `pkgs.wayland` in `NIX_LDFLAGS` rpath so `libwayland-client.so` is found at runtime
- `pkgs.wayland-scanner` and `pkgs.wayland-protocols` in packages; their store paths are
  exported as `VOLO_WAYLAND_DATADIR` and `VOLO_WAYLAND_PROTOCOLS_DATADIR` for `run.wlgen`

Re-enter the dev shell (`nix develop`) after any `flake.nix` change.

## Current status

- Build: **passes** (`cmake --build build`)
- Unit tests: **all pass** (`cmake --build build --target test`)
- `ninja run.volo`: opens a window, Vulkan swapchain works, mouse cursor position and buttons work, keyboard input not yet implemented.

## What still needs to be done

The following functions currently silently do nothing:

| Function | Notes |
|---|---|
| `gap_pal_window_resize` | Call `xdg_toplevel_set_max_size` / handle configure events |
| `gap_pal_window_cursor_hide/capture/confine` | Requires `zwp_pointer_constraints_v1` (add XML to `run.wlgen`) |
| `gap_pal_window_cursor_set` | Requires `wl_cursor` / `wl_pointer_set_cursor` |
| `gap_pal_window_cursor_pos_set` | Not possible on Wayland (pointer warp unsupported) |
| `gap_pal_window_icon_set` | No standard Wayland protocol; compositor-specific |
| `gap_pal_icon_load` / `gap_pal_cursor_load` | Cursor theme loading |
| `gap_pal_key_label` | Needs `xkbcommon` integration (same as XCB path) |
| `gap_pal_window_clip_copy/paste` | Requires `wl_data_device_manager` protocol |
| Keyboard input | Bind `wl_keyboard` from seat capabilities; integrate `xkbcommon` for key → `GapKey` mapping |
| Display info (DPI, refresh rate, name) | Requires binding `wl_output` and its events |
| Fullscreen | Call `xdg_toplevel_set_fullscreen(...)` |

The next logical step is **keyboard input**:
1. In `wl_seat_capabilities`: if keyboard capability present, `wl_seat_get_keyboard(...)`, add `wl_keyboard_listener`
2. Handle `keymap` (load xkb keymap from fd), `key` (press/release), `modifiers` events
3. Integrate `xkbcommon` for scancode → `GapKey` mapping — the XCB PAL already does this, use the same pattern

Note: `wl_keyboard` is already in the core Wayland XML so its listener struct and request wrappers are already generated.
Adding `zwp_pointer_constraints_v1` (for cursor lock/confine) would require adding its
XML to the `run.wlgen` `--schema` list.

## File map

```
utilities/
  wlgen.c                            — code generator (reads XML, writes wayland.h + wayland.c)
  CMakeLists.txt                     — adds wlgen executable target

libs/gap/
  CMakeLists.txt                     — adds wayland.c when VOLO_WAYLAND=ON
  src/pal.c                          — selects pal_linux_wayland.c when VOLO_WAYLAND defined
  src/pal_linux_wayland.c            — main implementation file
  src/pal_linux_xcb.c                — reference implementation (dlopen pattern, struct layout)
  src/wayland/
    wayland.h                        — generated; included by the PAL
    wayland.c                        — generated; compiled into gap lib

CMakeLists.txt                       — run.wlgen target (calls wlgen with XML paths)
flake.nix                            — wayland runtime dep + wlgen env vars
```
