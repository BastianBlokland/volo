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
- `$ENV{VOLO_WAYLAND_PROTOCOLS_DATADIR}/stable/viewporter/viewporter.xml`
- `$ENV{VOLO_WAYLAND_PROTOCOLS_DATADIR}/staging/fractional-scale/fractional-scale-v1.xml`

Both env vars are set by `flake.nix` from the `wayland-scanner` and `wayland-protocols` Nix packages.

#### What `wlgen` generates

**`libs/gap/src/wayland_api.h`** — self-contained header (no system includes beyond `core/forward.h`):
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

**`libs/gap/src/wayland_api.c`** — implementation:
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
function signatures come from the generated `wayland_api.h` — no manual definitions remain.

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

  struct wp_fractional_scale_manager_v1* fracScaleManager;
  struct wp_viewporter*                  viewporter;

  DynArray outputs; // WlOutput[]

  struct wl_registry_listener            registryListener;
  struct xdg_wm_base_listener            xdgWmBaseListener;
  struct wl_seat_listener                seatListener;
  struct wl_pointer_listener             pointerListener;
  struct wl_output_listener              outputListener;
  struct wl_surface_listener             surfaceListener;
  struct wp_fractional_scale_v1_listener fracScaleListener;
} Wayland;
```

#### `WlOutput` struct
```c
typedef struct {
  struct wl_output* wlOutput;
  String            name; // heap-allocated via g_allocHeap
  i32               physWidthMm, physHeightMm;
  i32               modeWidthPx, modeHeightPx;
  i32               refreshMHz;
  i32               scale; // integer buffer scale (from wl_output.scale event)
} WlOutput;
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

#### `GapPalWindow` struct
```c
GapPalWindowFlags flags;
// ...display info, key sets, input text, clipboard...
u32                            surfaceScale120; // fractional scale in 1/120ths units
i32                            logicalWidth, logicalHeight; // last configure logical size
Wayland*                       wl;
struct wl_surface*             wlSurface;
struct xdg_surface*            xdgSurface;
struct xdg_toplevel*           xdgToplevel;
struct wp_fractional_scale_v1* fracScale; // null if protocol unavailable
struct wp_viewport*            viewport;  // null if protocol unavailable
struct xdg_surface_listener    xdgSurfaceListener;
struct xdg_toplevel_listener   xdgToplevelListener;
```

#### Initialization flow

`pal_init_wl(Allocator* alloc, Wayland* out)`:
1. Initializes `outputListener` and `fracScaleListener` structs (must be done before the roundtrip so they are ready when `wl_registry_global` binds outputs)
2. `dynlib_load` → `libwayland-client.so`
3. `wlLoad(&out->lib, &out->api)` → populates the `WlFuncs` table via dlsym
4. `out->api.display_connect(null)` → connect to default display
5. `wl_display_get_registry(display)` (generated wrapper, not a libwayland export)
6. `wl_registry_add_listener(...)` with `data = wl`, then `display_roundtrip` to enumerate globals
7. In `wl_registry_global`: bind `wl_compositor`, `xdg_wm_base`, `wl_seat`, `wl_output` (v4), `wp_fractional_scale_manager_v1`, `wp_viewporter` via `wlRegistryBind`. Output listener is registered immediately at bind time so events (geometry/mode/name/done) are not dropped.
8. `xdg_wm_base_add_listener(...)` for ping/pong keepalives

`pal_init_wl_seat(GapPal* pal)` — called after `GapPal*` is allocated so callbacks can look up windows:
1. `wl_seat_add_listener(...)` with `data = pal`
2. `display_roundtrip` → triggers `wl_seat_capabilities`
3. In `wl_seat_capabilities`: if pointer capability present, `wl_seat_get_pointer(...)`, then `wl_pointer_add_listener(...)` with `data = pal`

`pal_init_wl_outputs(GapPal* pal)`:
- Initializes `surfaceListener` (enter/leave/preferred_buffer_scale/preferred_buffer_transform)

Note: registry callbacks use `data = Wayland*`; seat/pointer/surface callbacks use `data = GapPal*` (needed to look up windows by surface pointer). Output callbacks use `data = Wayland*` since they are registered at bind time during the registry roundtrip, before `GapPal*` is allocated.

#### Window creation flow (`gap_pal_window_create`)
1. `wl_compositor_create_surface(...)` → `wl_surface*`
2. `xdg_wm_base_get_xdg_surface(..., wlSurface)` → `xdg_surface*`
3. `xdg_surface_get_toplevel(...)` → `xdg_toplevel*`
4. `xdg_toplevel_set_app_id(...)` → `"volo"`
5. `xdg_surface_add_listener(...)` + `xdg_toplevel_add_listener(...)` for configure/close events
6. `wl_surface_add_listener(...)` for enter/leave/preferred_buffer_scale events
7. If `fracScaleManager` and `viewporter` are available: create per-surface `wp_fractional_scale_v1` and `wp_viewport`, register `fracScaleListener` with `data = window`
8. Otherwise (fallback): `wl_surface_set_buffer_scale(initIntScale)`
9. `wl_surface_commit(...)` triggers compositor configure
10. `display_roundtrip` to process the configure event (and the initial `preferred_scale` event)

#### `gap_pal_update` / `gap_pal_flush`
- `update`: clears volatile state, calls `api.display_dispatch_pending` (non-blocking)
- `flush`: calls `api.display_flush`

#### `gap_pal_window_title_set`
Calls `xdg_toplevel_set_title(...)` with a null-terminated scratch copy of the title string.

#### `gap_pal_native_app_handle`
Returns `(uptr)pal->wl.display` — needed by Vulkan for `VkWaylandSurfaceCreateInfoKHR`.

#### Focus events
`xdg_toplevel_configure` includes a `wl_array` of states. The states are scanned for:
- `XDG_TOPLEVEL_STATE_FULLSCREEN` → sets/clears `GapPalWindowFlags_Fullscreen`
- `XDG_TOPLEVEL_STATE_ACTIVATED` → calls `pal_event_focus_gained` / `pal_event_focus_lost`

On focus loss, `gap_keyset_clear(&window->keysDown)` is called to prevent stuck keys.

#### Display info (name, DPI, refresh rate)
`wl_output` is bound at version 4 (required for the `name` event). The output listener is
registered inside `wl_registry_global` at bind time — output events (`geometry`, `mode`,
`scale`, `name`, `done`) are sent immediately after bind, so the listener must be in place
before the registry roundtrip or they are silently dropped by libwayland.

`wl_surface_enter` fires when the surface enters an output. At that point the matching
`WlOutput` is looked up and the window's display name, refresh rate, and DPI are updated.

#### Fractional scaling

Wayland's `xdg_toplevel_configure` always delivers sizes in **logical (surface-local) coordinates**.
Converting to physical pixels requires knowing the scale factor:

- `wl_output.scale` — integer only; returns the ceiling (e.g. 2 for a 1.5× fractional setup). Incorrect for fractional scaling.
- `wl_surface.preferred_buffer_scale` — also an integer ceiling. Used as a fallback when `wp_fractional_scale_v1` is unavailable.
- `wp_fractional_scale_v1.preferred_scale` — exact fractional scale in units of 1/120th (e.g. 180 = 1.5×). This is the authoritative source.

Scale is stored as `surfaceScale120` (u32, in 1/120ths). Physical pixels are computed as:
```c
physical = ceil(logical * surfaceScale120 / 120)
```

The `wp_viewporter` protocol is used alongside `wp_fractional_scale_v1`. After receiving the
logical size from `xdg_toplevel_configure`, we call:
```c
wp_viewport_set_destination(&wl->api, window->viewport, logicalWidth, logicalHeight);
```
This tells the compositor that our physical buffer maps to exactly `logicalWidth × logicalHeight`
logical pixels. Combined with a buffer rendered at the exact physical size, the compositor can
blit the buffer to the screen 1:1 without any scaling — enabling direct scanout.

`wl_surface_preferred_buffer_scale` is ignored when `wp_fractional_scale_v1` is active.

#### Mouse input
Pointer events are dispatched through the `wl_pointer_listener`:
- `enter`: sets `pal->pointerFocus` by matching `wl_surface*` to a window
- `leave`: clears `pal->pointerFocus`
- `motion`: converts `wl_fixed_t` → logical pixels (`>> 8`), converts logical to physical via `surfaceScale120`, flips Y axis (Wayland top-left vs. gap bottom-left), calls `pal_event_cursor`
- `button`: maps Linux `BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA` → `GapKey_MouseLeft/Right/Middle/X1/X2`, calls `pal_event_press` / `pal_event_release`
- `axis`: maps vertical/horizontal scroll to `pal_event_scroll`

Linux button defines (`BTN_LEFT = 0x110` etc.) are defined locally — no `<linux/input.h>` dependency.

### 3. `flake.nix`

- `pkgs.wayland` in `NIX_LDFLAGS` rpath so `libwayland-client.so` is found at runtime
- `pkgs.wayland-scanner` and `pkgs.wayland-protocols` in packages; their store paths are
  exported as `VOLO_WAYLAND_DATADIR` and `VOLO_WAYLAND_PROTOCOLS_DATADIR` for `run.wlgen`

Re-enter the dev shell (`nix develop`) after any `flake.nix` change.

## Present timing investigation

### `present-timing` (`VK_EXT_present_timing`)

The runtime log shows `present-timing: false` and `present-at-relative: false`. Here is why.

**Blocker 1 — extension not yet in Mesa 26.0.x.**
`VK_EXT_present_timing` is listed in Mesa's `new_features.txt` as targeting **Mesa 26.1**. The RADV implementation (in `src/amd/vulkan/radv_physical_device.c`) gates exposure on `radv_calibrated_timestamps_enabled()`, which is true for Phoenix (not Raven/Raven2), and calibrated timestamps are available. But the feature missed the 26.0 branch cut — nixpkgs unstable (Mesa 26.0.2) is one release behind. Expect it with Mesa 26.1.

**Blocker 2 — present-stage mismatch (will need a code fix).**
Mesa's Wayland WSI (`wsi_common_wayland.c`) reports:
```c
wait->presentStageQueries = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
```
But `swapchain_timing_present_stage` in `swapchain.c` is `VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT`, chosen to work around XWayland not supporting `PIXEL_OUT`. The comment even notes `PIXEL_OUT` is the ideal. On native Wayland the stage must be switched to `VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT` — otherwise the `presentTiming` surface-capability check will fail even with a Mesa that exposes the extension.

**Compositor side is ready.** Hyprland advertises `wp_presentation` (with `CLOCK_MONOTONIC`), `wp_fifo_manager_v1`, and `wp_commit_timing_manager_v1` (when `render:commit_timing_enabled = 1`), satisfying all Mesa WSI requirements.

### `present-at-relative`

`presentAtRelativeTimeSupported` in `VkPresentTimingSurfaceCapabilitiesEXT` is initialized to `VK_FALSE` in Mesa's Wayland WSI and **never set to true** — not even in the current Mesa HEAD. The RADV device feature `presentAtRelativeTime = true` is set, but the WSI surface capability (which our code checks) is not. Mesa hasn't implemented this on the Wayland compositor path yet.

### Action items (deferred until Mesa 26.1 ships)

1. Change `swapchain_timing_present_stage` to `VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT` when running on native Wayland.
2. `present-at-relative` requires a Mesa-side fix; no action needed on our end.

---

## Current status

- Build: **passes** (`cmake --build build`)
- Unit tests: **all pass** (`cmake --build build --target test`)
- `ninja run.volo`: opens a window, Vulkan swapchain works, mouse cursor position and buttons work, window focus/unfocus events work, display name/DPI/refresh rate reported correctly, fractional scaling correct (2880×1800 physical on eDP-1 at 1.5× scale). Keyboard input not yet implemented.

## Known issues / deferred

- **Scroll speed**: `wl_pointer.axis` values are passed as raw integer pixels (`value >> 8`). On this setup Hyprland sends smooth-scroll events of ~6–8 pixels each (rather than one clean 15px-per-click event), so scroll feels faster than XCB/Win32's ±1 per click. Fixing this properly requires accumulating `wl_fixed_t` values and emitting steps at a 15px threshold — deferred until further into the Wayland implementation.
- **Saved prefs**: if the game was previously run with a broken scale implementation, `build/game/volo.prefs` will contain a wrong window size. Delete it to reset to defaults.

## What still needs to be done

The following functions currently silently do nothing:

| Function | Notes |
|---|---|
| `gap_pal_window_cursor_hide/capture/confine` | Requires `zwp_pointer_constraints_v1` (add XML to `run.wlgen`) |
| WM-initiated fullscreen not reflected in `win->mode` | `GapPalWindowFlags_Fullscreen` is correctly set from the compositor's `xdg_toplevel_configure` states, but `window.c` never reads it back. Doing so naively causes a feedback loop: the app-requested fullscreen state and the compositor-confirmed state are out of sync during the roundtrip, so reading the flag before confirmation fights the WM. Fix requires tracking pending fullscreen state in `GapPalWindow`. |
| `gap_pal_window_cursor_set` | Requires `wl_cursor` / `wl_pointer_set_cursor` |
| `gap_pal_window_cursor_pos_set` | Not possible on Wayland (pointer warp unsupported) |
| `gap_pal_window_icon_set` | No standard Wayland protocol; compositor-specific |
| `gap_pal_icon_load` / `gap_pal_cursor_load` | Cursor theme loading |
| `gap_pal_key_label` | Needs `xkbcommon` integration (same as XCB path) |
| `gap_pal_window_clip_copy/paste` | Requires `wl_data_device_manager` protocol |
| Keyboard input | Bind `wl_keyboard` from seat capabilities; integrate `xkbcommon` for key → `GapKey` mapping |

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
  src/wayland_api.h                  — generated; included by the PAL
  src/wayland_api.c                  — generated; compiled into gap lib

CMakeLists.txt                       — run.wlgen target (calls wlgen with XML paths)
flake.nix                            — wayland runtime dep + wlgen env vars
```
