#pragma once
#include "asset_ftx.h"
#include "ui_canvas.h"
#include "ui_settings.h"

// Internal forward declarations:
typedef struct sUiCmdBuffer UiCmdBuffer;

typedef struct {
  ALIGNAS(16)
  UiRect  rect;
  UiColor color;
  u32     atlasIndex;
  u16     borderFrac; // 'border size' / rect.width * u16_max
  u16     cornerFrac; // 'corner size' / rect.width * u16_max
  u8      clipId;
  u8      outlineWidth;
  u8      weight;
} UiGlyphData;

ASSERT(sizeof(UiGlyphData) == 32, "Size needs to match the size defined in glsl");

typedef struct {
  /**
   * Index of the character that was hovered in the text.
   * NOTE: Is 'sentinel_usize' when no character was hovered.
   * TODO: Does not support multi-line text atm (always returns a char on the last visible line).
   */
  usize hoveredCharIndex;
} UiBuildTextInfo;

typedef u8 (*UiOutputClipRectFunc)(void* userCtx, UiRect);
typedef void (*UiOutputGlyphFunc)(void* userCtx, UiGlyphData, UiLayer);
typedef void (*UiOutputRect)(void* userCtx, UiId, UiRect);
typedef void (*UiOutputTextInfo)(void* userCtx, UiId, UiBuildTextInfo);

typedef struct {
  const UiSettingsComp* settings;
  const AssetFtxComp*   font;
  UiId                  debugElem;
  UiVector              canvasRes, inputPos;
  void*                 userCtx;
  UiOutputClipRectFunc  outputClipRect;
  UiOutputGlyphFunc     outputGlyph;
  UiOutputRect          outputRect;
  UiOutputTextInfo      outputTextInfo;
} UiBuildCtx;

typedef struct {
  UiId    id;
  UiLayer layer;
  UiFlags flags;
} UiBuildHover;

typedef struct {
  u32          commandCount;
  UiBuildHover hover;
} UiBuildResult;

UiBuildResult ui_build(const UiCmdBuffer*, const UiBuildCtx*);
