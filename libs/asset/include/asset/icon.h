#pragma once
#include "data/registry.h"
#include "ecs/module.h"

typedef struct {
  u8 r, g, b, a; // Srgb encoded.
} AssetIconPixel;

ecs_comp_extern_public(AssetIconComp) {
  u32     width, height;
  u32     hotspotX, hotspotY;
  DataMem pixelData; // AssetIconPixel[width * height]
};

extern DataMeta g_assetIconDefMeta;
extern DataMeta g_assetIconMeta;
