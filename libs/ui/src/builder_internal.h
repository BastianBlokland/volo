#pragma once
#include "asset_atlas.h"
#include "asset_fonttex.h"
#include "ui_canvas.h"
#include "ui_settings.h"

#include "forward_internal.h"

typedef enum {
  UiAtomType_Glyph,
  UiAtomType_Image,

  UiAtomType_Count,
} UiAtomType;

typedef struct {
  ALIGNAS(16)
  UiRect  rect;
  UiColor color;
  u16     atlasIndex;
  u16     angleFrac;       // 'angle radians' / math_pi_f32 / 2 * u16_max.
  u16     glyphBorderFrac; // 'border size' / rect.width * u16_max (glyph only).
  u16     cornerFrac;      // 'corner size' / rect.width * u16_max.
  u8      atomType;
  u8      clipId;
  u8      glyphOutlineWidth; // (glyph only).
  u8      glyphWeight;       // (glyph only).
} UiAtomData;

ASSERT(sizeof(UiAtomData) == 32, "Size needs to match the size defined in glsl");
ASSERT(alignof(UiAtomData) == 16, "Alignment needs to match the glsl alignment");

typedef struct {
  u32 lineCount;
  u32 maxLineCharWidth;

  /**
   * Index of the character that was hovered in the text.
   * NOTE: Is 'sentinel_usize' when no character was hovered.
   * TODO: Does not support multi-line text atm (always returns a char on the last visible line).
   */
  usize hoveredCharIndex;
} UiBuildTextInfo;

typedef u8 (*UiOutputClipRectFunc)(void* userCtx, UiRect);
typedef void (*UiOutputAtomFunc)(void* userCtx, UiAtomData, UiLayer);
typedef void (*UiOutputRect)(void* userCtx, UiId, UiRect);
typedef void (*UiOutputTextInfo)(void* userCtx, UiId, UiBuildTextInfo);

typedef struct {
  const UiSettingsGlobalComp* settings;
  const AssetFontTexComp*     atlasFont;
  const AssetAtlasComp*       atlasImage;
  UiId                        debugElem;
  UiVector                    canvasRes, inputPos;
  void*                       userCtx;
  UiOutputClipRectFunc        outputClipRect;
  UiOutputAtomFunc            outputAtom;
  UiOutputRect                outputRect;
  UiOutputTextInfo            outputTextInfo;
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
