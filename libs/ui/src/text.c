#include "core_diag.h"
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
  UiTextAlign         align;
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

  typedef struct {
    f32   pixel;
    usize charIndex;
  } CursorPos;

  CursorPos cursorAccepted = {0}, cursorConsumed = {0};
  String    remainingText = text;
  bool      wasSeperator  = false;
  bool      firstWord     = true;

  while (true) {
    if (string_is_empty(remainingText)) {
      cursorConsumed.charIndex = text.size;
      cursorAccepted           = cursorConsumed;
      goto End;
    }

    Unicode cp;
    remainingText = utf8_cp_read(remainingText, &cp);

    const bool isSeperator = ui_text_is_seperator(cp);
    if ((isSeperator && !wasSeperator) || firstWord) {
      cursorConsumed.charIndex = text.size - remainingText.size - utf8_cp_bytes(cp);
      cursorAccepted           = cursorConsumed;
    }
    if (isSeperator) {
      cursorConsumed.charIndex = text.size - remainingText.size;
      firstWord                = false;
    }
    wasSeperator = isSeperator;

    switch (cp) {
    case Unicode_Newline:
      goto End;
    case Unicode_CarriageReturn:
      cursorConsumed.pixel = 0;
      break;
    case Unicode_HorizontalTab:
      cursorConsumed.pixel = ui_text_next_tabstop(font, cursorConsumed.pixel, fontSize);
    case Unicode_ZeroWidthSpace:
      break; // Occupies no space, so the cursor shouldn't be updated.
    default:
      cursorConsumed.pixel += asset_ftx_lookup(font, cp)->advance * fontSize;
      break;
    }
    if (cursorConsumed.pixel > maxWidth) {
      goto End;
    }
  }

End:
  *out = (UiTextLine){
      .text = string_slice(text, 0, cursorAccepted.charIndex),
      .size = ui_vector(cursorAccepted.pixel, fontSize),
  };
  return string_consume(text, cursorConsumed.charIndex);
}

static UiVector ui_text_char_pos(UiTextBuildState* state, const UiTextLine* line) {
  const f32 minY = state->rect.pos.y;
  const f32 maxY = minY + state->rect.size.height;
  const f32 minX = state->rect.pos.x;
  const f32 maxX = minX + state->rect.size.width;

  switch (state->align) {
  case UiTextAlign_TopLeft:
    return ui_vector(minX + state->cursor.x, maxY - state->cursor.y);
  case UiTextAlign_TopRight:
    return ui_vector(maxX - line->size.x + state->cursor.x, maxY - state->cursor.y);
  }
  diag_crash();
}

static void ui_text_build_char(UiTextBuildState* state, const UiTextLine* line, const Unicode cp) {
  switch (cp) {
  case Unicode_CarriageReturn:
    state->cursor.x = 0;
    return;
  case Unicode_HorizontalTab:
    state->cursor.x = ui_text_next_tabstop(state->font, state->cursor.x, state->fontSize);
    return;
  case Unicode_ZeroWidthSpace:
    return;
  default:
    break;
  }
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cp);
  if (!sentinel_check(ch->glyphIndex)) {
    state->buildChar(
        state->userCtx,
        &(UiTextCharInfo){
            .ch    = ch,
            .pos   = ui_text_char_pos(state, line),
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
    ui_text_build_char(state, line, cp);
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
    const UiTextAlign         align,
    void*                     userCtx,
    const UiTextBuildCharFunc buildChar) {

  UiTextBuildState state = {
      .font      = font,
      .rect      = rect,
      .fontSize  = fontSize,
      .fontColor = fontColor,
      .align     = align,
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
