#pragma once
#include "core/array.h"
#include "data/registry.h"
#include "ecs/module.h"

typedef struct {
  StringHash key;   // Localization key, example: 'MENU_PLAY'.
  String     value; // Translation, example: 'Play'.
} AssetLocaleText;

ecs_comp_extern_public(AssetLocaleComp) {
  StringHash language; // ISO 639 language code, example: 'en'.
  StringHash country;  // ISO 3166 country code, example: 'us'.
  String     name;     // Display name in the native language, example: 'English'.
  bool       isDefault;
  HeapArray_t(AssetLocaleText) textEntries; // Sorted on 'key'.
};

extern DataMeta g_assetLocaleDefMeta;
