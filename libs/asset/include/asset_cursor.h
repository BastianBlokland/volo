#pragma once
#include "data_registry.h"
#include "ecs_module.h"

typedef struct {
  u8 r, g, b, a; // Srgb encoded.
} AssetCursorPixel;

ecs_comp_extern_public(AssetCursorComp) {
  u32     width, height;
  u32     hotspotX, hotspotY;
  DataMem pixelData; // AssetCursorPixel[width * height]
};

extern DataMeta g_assetCursorDefMeta;
extern DataMeta g_assetCursorMeta;
