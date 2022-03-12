#pragma once
#include "asset_ftx.h"
#include "ui_canvas.h"

// Internal forward declarations:
typedef struct sGapWindowComp GapWindowComp;
typedef struct sUiCmdBuffer   UiCmdBuffer;

typedef struct {
  ALIGNAS(16)
  f32 glyphsPerDim;
  f32 invGlyphsPerDim;
  f32 padding[2];
} UiDrawData;

ASSERT(sizeof(UiDrawData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  UiRect  rect;
  UiColor color;
  u32     atlasIndex;
  u16     borderFrac; // 'border size' / rect.width * u16_max
  u16     cornerFrac; // 'border size' / rect.width * u16_max
  u32     outlineWidth;
} UiGlyphData;

ASSERT(sizeof(UiGlyphData) == 32, "Size needs to match the size defined in glsl");

typedef void (*UiOutputDrawFunc)(void* userCtx, UiDrawData);
typedef void (*UiOutputGlyphFunc)(void* userCtx, UiGlyphData, UiLayer);
typedef void (*UiOutputRect)(void* userCtx, UiId, UiRect);

typedef struct {
  const GapWindowComp* window;
  const AssetFtxComp*  font;
  void*                userCtx;
  UiOutputDrawFunc     outputDraw;
  UiOutputGlyphFunc    outputGlyph;
  UiOutputRect         outputRect;
} UiBuildCtx;

typedef struct {
  UiId hoveredId;
} UiBuildResult;

UiBuildResult ui_build(const UiCmdBuffer*, const UiBuildCtx*);
