#pragma once
#include "asset/fonttex.h"
#include "ui/canvas.h"
#include "ui/color.h"
#include "ui/rect.h"

typedef struct {
  const AssetFontTexComp* font;
  const AssetFontTexChar* ch;
  UiVector                pos;
  UiColor                 color;
  UiLayer                 layer : 8;
  UiWeight                weight : 8;
  u8                      outline;
  f32                     size;
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
    const AssetFontTexComp*,
    UiFlags  flags,
    UiRect   totalRect,
    UiVector inputPos,
    String   text,
    f32      fontSize,
    UiColor  fontColor,
    u8       fontOutline,
    UiLayer  fontLayer,
    UiMode   fontMode,
    u8       fontVariation,
    UiWeight fontWeight,
    UiAlign  align,
    void*    userCtx,
    UiTextBuildCharFunc,
    UiTextBuildBackgroundFunc);
