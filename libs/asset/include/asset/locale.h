#pragma once
#include "core/array.h"
#include "data/registry.h"
#include "ecs/module.h"

typedef struct {
  StringHash key;   // Localization key, example: 'MENU_PLAY'.
  String     value; // Translation, example: 'Play'.
} AssetLocaleText;

ecs_comp_extern_public(AssetLocaleComp) {
  String name; // Display name in the native language, example: 'English'.
  bool   isDefault;
  HeapArray_t(AssetLocaleText) textEntries; // Sorted on 'key'.
};

extern DataMeta g_assetLocaleDefMeta;

i8 asset_locale_text_compare(const void* a, const void* b);
