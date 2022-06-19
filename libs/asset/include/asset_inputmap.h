#pragma once
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
  u32            key; // Key identifier, matches the 'GapKey' values in 'GapInput.h'.
} AssetInputBinding;

typedef struct {
  StringHash nameHash;
  u16        bindingIndex, bindingCount; // Stored in the bindings array.
} AssetInputAction;

ecs_comp_extern_public(AssetInputMapComp) {
  AssetInputAction*  actions; // Sorted on the nameHash.
  usize              actionCount;
  AssetInputBinding* bindings;
  usize              bindingCount;
};

/**
 * Lookup an input action by the hash of its name.
 */
const AssetInputAction* asset_inputmap_get(const AssetInputMapComp*, u32 nameHash);
