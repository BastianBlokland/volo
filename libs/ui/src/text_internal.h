#pragma once
#include "asset_ftx.h"
#include "ui_canvas.h"
#include "ui_rect.h"

typedef struct {
  const AssetFtxChar* ch;
  UiVector            pos;
  UiColor             color;
  UiLayer             layer;
  UiWeight            weight;
  f32                 size;
  u8                  outline;
} UiTextCharInfo;

typedef void (*UiTextBuildCharFunc)(void* userCtx, const UiTextCharInfo*);

typedef struct {
  UiRect  rect;
  UiColor color;
  UiLayer layer;
} UiTextBackgroundInfo;

typedef void (*UiTextBuildBackgroundFunc)(void* userCtx, const UiTextBackgroundInfo*);

typedef struct {
  UiRect rect;
  u32    lineCount;
  u32    maxLineCharWidth;
  /**
   * Byte index of the hovered character.
   * TODO: Does not support multi-line text atm (always returns a char on the last visible line).
   */
  usize hoveredCharIndex;
} UiTextBuildResult;

UiTextBuildResult ui_text_build(
    const AssetFtxComp*,
    UiFlags  flags,
    UiRect   totalRect,
    UiVector inputPos,
    String   text,
    f32      fontSize,
    UiColor  fontColor,
    u8       fontOutline,
    UiLayer  fontLayer,
    u8       fontVariation,
    UiWeight fontWeight,
    UiAlign  align,
    void*    userCtx,
    UiTextBuildCharFunc,
    UiTextBuildBackgroundFunc);
