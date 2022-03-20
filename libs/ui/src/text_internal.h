#pragma once
#include "asset_ftx.h"
#include "ui_canvas.h"
#include "ui_rect.h"

typedef struct {
  const AssetFtxChar* ch;
  UiVector            pos;
  UiColor             color;
  UiLayer             layer;
  f32                 size;
  u8                  outline;
} UiTextCharInfo;

typedef void (*UiTextBuildCharFunc)(void* userCtx, const UiTextCharInfo*);

typedef struct {
  UiRect rect;
  u32    lineCount;
} UiTextBuildResult;

UiTextBuildResult ui_text_build(
    const AssetFtxComp*,
    UiRect  totalRect,
    String  text,
    f32     fontSize,
    UiColor fontColor,
    u8      fontOutline,
    UiLayer fontLayer,
    u8      fontVariation,
    UiAlign align,
    void*   userCtx,
    UiTextBuildCharFunc);
