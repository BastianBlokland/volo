#pragma once
#include "core_array.h"
#include "data_registry.h"
#include "ecs_module.h"

/**
 * Input Map.
 * Maps actions (eg 'Jump") to a collection of bindings (eg press 'Space' or hold 'Up').
 */

typedef enum {
  AssetInputType_Pressed,  // Triggered the tick that the key was pressed.
  AssetInputType_Released, // Triggered the tick that the key was released.
  AssetInputType_Down,     // Triggered every tick while holding down the key.
} AssetInputType;

typedef struct {
  AssetInputType type;
  u32            key; // Key identifier, matches the 'GapKey' values in 'gap_input.h'.
  u32            requiredModifierBits; // Matches the 'InputModifier' enum in 'input_manager.h'.
  u32            illegalModifierBits;  // Matches the 'InputModifier' enum in 'input_manager.h'.
} AssetInputBinding;

typedef struct {
  StringHash nameHash;
  u32        blockerBits;                // Matches the 'InputBlocker' enum in 'input_manager.h'.
  u16        bindingIndex, bindingCount; // Stored in the bindings array.
} AssetInputAction;

ecs_comp_extern_public(AssetInputMapComp) {
  StringHash layer;
  HeapArray_t(AssetInputAction) actions; // Sorted on the nameHash.
  HeapArray_t(AssetInputBinding) bindings;
};

extern DataMeta g_assetInputDefMeta;

/**
 * Lookup an input action by the hash of its name.
 */
const AssetInputAction* asset_inputmap_get(const AssetInputMapComp*, StringHash nameHash);
