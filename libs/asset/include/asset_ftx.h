#pragma once
#include "core_unicode.h"
#include "ecs_module.h"

typedef struct {
  UnicodeCp cp;
  u32       glyphIndex; // sentinel_u32 when character has no glyph (for example a space).
  f32       size;
  f32       offsetX, offsetY;
  f32       advance;
} AssetFtxChar;

ecs_comp_extern_public(AssetFtxComp) {
  AssetFtxChar* characters;
  usize         characterCount;
};
