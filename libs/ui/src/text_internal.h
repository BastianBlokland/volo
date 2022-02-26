#pragma once
#include "asset_ftx.h"
#include "ui_vector.h"

typedef struct {
  String   text;
  UiVector size;
} UiTextLine;

/**
 * Get the next line that fits in the given maximum width.
 * NOTE: Returns the remaining text that did not fit on the current line.
 */
String ui_text_line(const AssetFtxComp*, String text, f32 maxWidth, f32 fontSize, UiTextLine* out);
