#pragma once
#include "data/registry.h"
#include "ecs/module.h"

ecs_comp_extern_public(AssetLocaleComp) {
  StringHash language; // ISO 639 language code, example: 'en'.
  StringHash country;  // ISO 3166 country code, example: 'us'.
  String     name;
};

extern DataMeta g_assetLocaleDefMeta;
