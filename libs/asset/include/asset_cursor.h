#pragma once
#include "data_registry.h"
#include "ecs_module.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef struct {
  u8 r, g, b, a; // Srgb encoded.
} AssetCursorPixel;

ecs_comp_extern_public(AssetCursorComp) {
  u32                     width, height;
  u32                     hotspotX, hotspotY;
  const AssetCursorPixel* pixels;
};

extern DataMeta g_assetCursorDataDef;

void asset_cursor_jsonschema_write(DynString*);
