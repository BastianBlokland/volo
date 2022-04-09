#pragma once
#include "core_bits.h"
#include "ecs_entity.h"
#include "ecs_module.h"

/**
 * Controls the cursor behaviour:
 * - Normal:  Cursor is visible and can be moved freely.
 * - Locked:  Cursor is hidden and kept centered, NOTE: Delta position values are still produced.
 */
typedef enum {
  InputCursorMode_Normal,
  InputCursorMode_Locked,
} InputCursorMode;

/**
 * Controls which inputs are currently allowed:
 * - Normal:    All triggered mappings are active.
 * - TextInput: All triggered mappings are disabled.
 */
typedef enum {
  InputLayer_Normal,
  InputLayer_TextInput,
} InputLayer;

/**
 * Global input manager component.
 */
ecs_comp_extern(InputManagerComp);

/**
 * Retrieve the entity of the active (focussed) window.
 * NOTE: Returns 0 when there is no active window.
 */
EcsEntityId input_active_window(const InputManagerComp*);

InputLayer input_layer(const InputManagerComp*);
void       input_layer_set(InputManagerComp*, InputLayer);

InputCursorMode input_cursor_mode(const InputManagerComp*);
void            input_cursor_mode_set(InputManagerComp*, InputCursorMode);
f32             input_cursor_delta_x(const InputManagerComp*);
f32             input_cursor_delta_y(const InputManagerComp*);

/**
 * Check if an input action was triggered this tick.
 */
#define input_triggered_lit(_MANAGER_, _ACTION_LIT_)                                               \
  input_triggered_hash((_MANAGER_), bits_hash_32(string_lit(_ACTION_LIT_)))

bool input_triggered_hash(const InputManagerComp*, u32 actionHash);
