#pragma once
#include "asset_ftx.h"
#include "ui_canvas.h"
#include "ui_settings.h"

// Internal forward declarations:
typedef struct sUiCmdBuffer UiCmdBuffer;

typedef struct {
  ALIGNAS(16)
  UiRect   rect;
  UiColor  color;
  u32      atlasIndex;
  u16      borderFrac; // 'border size' / rect.width * u16_max
  u16      cornerFrac; // 'corner size' / rect.width * u16_max
  u8       clipId;
  u8       outlineWidth;
  UiWeight weight : 8;
} UiGlyphData;

ASSERT(sizeof(UiGlyphData) == 32, "Size needs to match the size defined in glsl");

typedef u8 (*UiOutputClipRectFunc)(void* userCtx, UiRect);
typedef void (*UiOutputGlyphFunc)(void* userCtx, UiGlyphData, UiLayer);
typedef void (*UiOutputRect)(void* userCtx, UiId, UiRect);

typedef struct {
  const UiSettingsComp* settings;
  const AssetFtxComp*   font;
  UiId                  debugElem;
  UiVector              canvasRes, inputPos;
  void*                 userCtx;
  UiOutputClipRectFunc  outputClipRect;
  UiOutputGlyphFunc     outputGlyph;
  UiOutputRect          outputRect;
} UiBuildCtx;

typedef struct {
  UiId    hoveredId;
  UiLayer hoveredLayer;
  UiFlags hoveredFlags;
} UiBuildResult;

UiBuildResult ui_build(const UiCmdBuffer*, const UiBuildCtx*);
