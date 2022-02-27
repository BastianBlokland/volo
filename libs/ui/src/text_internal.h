#pragma once
#include "asset_ftx.h"
#include "ui_canvas.h"
#include "ui_rect.h"

typedef struct {
  const AssetFtxChar* ch;
  UiVector            pos;
  f32                 size;
  UiColor             color;
  u8                  outline;
} UiTextCharInfo;

typedef void (*UiTextBuildCharFunc)(void* userCtx, const UiTextCharInfo*);

void ui_text_build(
    const AssetFtxComp*,
    UiRect      rect,
    String      text,
    f32         fontSize,
    UiColor     fontColor,
    u8          fontOutline,
    UiTextAlign align,
    void*       userCtx,
    UiTextBuildCharFunc);
