#pragma once
#include "asset_ftx.h"
#include "ui_color.h"
#include "ui_rect.h"
#include "ui_vector.h"

typedef struct {
  const AssetFtxChar* ch;
  UiVector            pos;
  f32                 size;
  UiColor             color;
} UiTextCharInfo;

typedef void (*UiTextBuildCharFunc)(void* userCtx, const UiTextCharInfo*);

void ui_text_build(
    const AssetFtxComp*,
    UiRect,
    String  text,
    f32     fontSize,
    UiColor fontColor,
    void*   userCtx,
    UiTextBuildCharFunc);
