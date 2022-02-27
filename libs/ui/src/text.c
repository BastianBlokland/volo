#include "core_math.h"
#include "core_unicode.h"
#include "core_utf8.h"

#include "text_internal.h"

#define ui_text_tab_size 4

typedef struct {
  String   text;
  UiVector size;
} UiTextLine;

typedef struct {
  const AssetFtxComp* font;
  UiRect              rect;
  f32                 fontSize;
  UiColor             fontColor;
  void*               userCtx;
  UiTextBuildCharFunc buildChar;
  UiVector            cursor;
} UiTextBuildState;

static f32 ui_text_next_tabstop(const AssetFtxComp* font, const f32 cursor, const f32 fontSize) {
  const f32 spaceAdvance = asset_ftx_lookup(font, Unicode_Space)->advance * fontSize;
  const f32 tabSize      = spaceAdvance * ui_text_tab_size;
  return cursor + tabSize - math_mod_f32(cursor, tabSize);
}

static bool ui_text_is_seperator(const Unicode cp) {
  switch (cp) {
  case Unicode_CarriageReturn:
  case Unicode_HorizontalTab:
  case Unicode_Newline:
  case Unicode_Space:
  case Unicode_ZeroWidthSpace:
    return true;
  default:
    return false;
  }
}

/**
 * Get the next line that fits in the given maximum width.
 * NOTE: Returns the remaining text that did not fit on the current line.
 */
static String ui_text_line(
    const AssetFtxComp* font,
    const String        text,
    const f32           maxWidth,
    const f32           fontSize,
    UiTextLine*         out) {

  if (UNLIKELY(maxWidth < fontSize)) {
    // Width is too small to fit even a single character.
    *out = (UiTextLine){.text = string_empty, .size = ui_vector(0, fontSize)};
    return string_empty;
  }

  String remainingText = text;
  usize  lineEnd       = 0; // Character index of the last processed non-seperator codepoint.
  usize  consumedEnd   = 0; // Character index of the last consumed codepoint (incl seperators).
  f32    cursor        = 0; // Current pixel position on the line.
  bool   wasSeperator  = false;
  bool   firstWord     = true;

  while (true) {
    if (string_is_empty(remainingText)) {
      lineEnd = consumedEnd = text.size;
      goto End;
    }

    Unicode cp;
    remainingText = utf8_cp_read(remainingText, &cp);

    const bool isSeperator = ui_text_is_seperator(cp);
    if ((isSeperator && !wasSeperator) || firstWord) {
      lineEnd = consumedEnd = text.size - remainingText.size - utf8_cp_bytes(cp);
    }
    if (isSeperator) {
      consumedEnd = text.size - remainingText.size;
      firstWord   = false;
    }
    wasSeperator = isSeperator;

    switch (cp) {
    case Unicode_Newline:
      goto End;
    case Unicode_HorizontalTab:
      cursor = ui_text_next_tabstop(font, cursor, fontSize);
    case Unicode_ZeroWidthSpace:
      break; // Occupies no space, so the cursor shouldn't be updated.
    default:
      cursor += asset_ftx_lookup(font, cp)->advance * fontSize;
      break;
    }
    if (cursor > maxWidth) {
      goto End;
    }
  }

End:
  *out = (UiTextLine){
      .text = string_slice(text, 0, lineEnd),
      .size = ui_vector(cursor, fontSize),
  };
  return string_consume(text, consumedEnd);
}

static void ui_text_build_char(UiTextBuildState* state, const Unicode cp) {
  switch (cp) {
  case Unicode_ZeroWidthSpace:
    return;
  case Unicode_HorizontalTab:
    state->cursor.x = ui_text_next_tabstop(state->font, state->cursor.x, state->fontSize);
    return;
  default:
    break;
  }
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cp);
  if (!sentinel_check(ch->glyphIndex)) {
    const f32      originX = state->rect.position.x;
    const f32      originY = state->rect.position.y + state->rect.size.height;
    const UiVector pos     = {
        originX + state->cursor.x,
        originY - state->cursor.y,
    };
    state->buildChar(
        state->userCtx,
        &(UiTextCharInfo){
            .ch    = ch,
            .pos   = pos,
            .size  = state->fontSize,
            .color = state->fontColor,
        });
  }
  state->cursor.x += ch->advance * state->fontSize;
}

static bool ui_text_build_line(UiTextBuildState* state, const UiTextLine* line) {
  state->cursor.x = 0;
  state->cursor.y += state->fontSize;

  const f32 maxY = state->rect.size.height - state->font->lineSpacing * state->fontSize;
  if (state->cursor.y >= maxY) {
    return false; // Not enough space remaining to draw a line.
  }

  String remainingText = line->text;
  while (!string_is_empty(remainingText)) {
    Unicode cp;
    remainingText = utf8_cp_read(remainingText, &cp);
    ui_text_build_char(state, cp);
  }

  state->cursor.y += state->font->lineSpacing * state->fontSize;
  return true;
}

void ui_text_build(
    const AssetFtxComp*       font,
    const UiRect              rect,
    const String              text,
    const f32                 fontSize,
    const UiColor             fontColor,
    void*                     userCtx,
    const UiTextBuildCharFunc buildChar) {

  UiTextBuildState state = {
      .font      = font,
      .rect      = rect,
      .fontSize  = fontSize,
      .fontColor = fontColor,
      .userCtx   = userCtx,
      .buildChar = buildChar,
      .cursor    = {0},
  };
  String     remainingText = text;
  UiTextLine line;
  while (!string_is_empty(remainingText)) {
    remainingText = ui_text_line(font, remainingText, rect.size.width, fontSize, &line);
    if (!ui_text_build_line(&state, &line)) {
      break;
    }
  }
}
