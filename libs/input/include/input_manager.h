#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

// Forward declare from 'gap_input.h'.
typedef enum eGapKey GapKey;

/**
 * Controls the cursor behavior:
 * - Normal:  Cursor is visible and can be moved freely.
 * - Locked:  Cursor is hidden and kept centered, NOTE: Delta position values are still produced.
 */
typedef enum {
  InputCursorMode_Normal,
  InputCursorMode_Locked,
} InputCursorMode;

/**
 * Global input manager component.
 */
ecs_comp_extern(InputManagerComp);

/**
 * Retrieve the entity of the active (focussed) window.
 * NOTE: Returns 0 when there is no active window.
 */
EcsEntityId input_active_window(const InputManagerComp*);

typedef enum {
  InputBlocker_TextInput        = 1 << 0,
  InputBlocker_HoveringUi       = 1 << 1,
  InputBlocker_HoveringGizmo    = 1 << 2,
  InputBlocker_PrefabCreate     = 1 << 3,
  InputBlocker_CursorLocked     = 1 << 4, // Managed by the input library.
  InputBlocker_CursorConfined   = 1 << 5, // Managed by the input library.
  InputBlocker_WindowFullscreen = 1 << 6, // Managed by the input library.
} InputBlocker;

InputBlocker input_blockers(const InputManagerComp*);
void         input_blocker_update(InputManagerComp*, InputBlocker, bool value);

typedef enum {
  InputModifier_Shift   = 1 << 0,
  InputModifier_Control = 1 << 1,
  InputModifier_Alt     = 1 << 2,
} InputModifier;

InputModifier input_modifiers(const InputManagerComp*);

InputCursorMode input_cursor_mode(const InputManagerComp*);
void            input_cursor_mode_set(InputManagerComp*, InputCursorMode);
f32             input_cursor_x(const InputManagerComp*);       // Normalized.
f32             input_cursor_y(const InputManagerComp*);       // Normalized.
f32             input_cursor_delta_x(const InputManagerComp*); // Normalized.
f32             input_cursor_delta_y(const InputManagerComp*); // Normalized.
f32             input_cursor_aspect(const InputManagerComp*);  // Aspect ratio of cursor window.
f32             input_scroll_x(const InputManagerComp*);
f32             input_scroll_y(const InputManagerComp*);
TimeDuration    input_doubleclick_interval(const InputManagerComp*);

/**
 * Check if an input action was triggered this tick.
 */
#define input_triggered_lit(_MANAGER_, _ACTION_LIT_)                                               \
  input_triggered_hash((_MANAGER_), string_hash_lit(_ACTION_LIT_))

bool input_triggered_hash(const InputManagerComp*, StringHash actionHash);

GapKey input_primary_key(const InputManagerComp*, StringHash actionHash);

void input_layer_enable(InputManagerComp*, StringHash layerHash);
void input_layer_disable(InputManagerComp*, StringHash layerHash);
bool input_layer_active(const InputManagerComp*, StringHash layerHash);
