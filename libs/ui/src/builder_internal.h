#pragma once
#include "asset_ftx.h"
#include "ui_types.h"

// Internal forward declarations:
typedef struct sUiCmdBuffer UiCmdBuffer;

typedef struct {
  ALIGNAS(16)
  f32 glyphsPerDim;
  f32 invGlyphsPerDim;
  f32 padding[2];
} UiDrawData;

ASSERT(sizeof(UiDrawData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  UiRect rect;
  u32    atlasIndex;
  u32    padding[3];
} UiGlyphData;

ASSERT(sizeof(UiGlyphData) == 32, "Size needs to match the size defined in glsl");

typedef void (*UiOutputDrawFunc)(void* userCtx, UiDrawData);
typedef void (*UiOutputGlyphFunc)(void* userCtx, UiGlyphData);

typedef struct {
  void*             userCtx;
  UiOutputDrawFunc  outputDraw;
  UiOutputGlyphFunc outputGlyph;
} UiBuildCtx;

void ui_build(const UiCmdBuffer*, const AssetFtxComp*, const UiBuildCtx*);
