#pragma once
#include "core_bits.h"
#include "ecs_entity.h"
#include "ecs_module.h"

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
