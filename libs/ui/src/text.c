#include "core_diag.h"
#include "core_math.h"
#include "core_unicode.h"
#include "core_utf8.h"
#include "log_logger.h"

#include "escape_internal.h"
#include "text_internal.h"

#define ui_text_tab_size 4
#define ui_text_max_lines 100

typedef struct {
  String   text;
  UiVector size;
  f32      posY;
} UiTextLine;

typedef struct {
  const AssetFtxComp* font;
  UiRect              rect;
  f32                 fontSize;
  UiColor             fontColor, fontColorDefault;
  u8                  fontOutline, fontOutlineDefault;
  UiTextAlign         align;
  void*               userCtx;
  UiTextBuildCharFunc buildChar;
  f32                 cursor;
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
      break;
    case Unicode_ZeroWidthSpace:
      break;
    case Unicode_Escape:
    case Unicode_Bell:
      remainingText            = ui_escape_read(remainingText, null);
      cursorConsumed.charIndex = text.size - remainingText.size;
      break;
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

static UiRect ui_text_inner_rect(const UiRect rect, const UiVector size, const UiTextAlign align) {
  const f32 centerX = rect.x + (rect.width - size.width) * 0.5f;
  const f32 centerY = rect.y + (rect.height - size.height) * 0.5f;
  const f32 maxX    = rect.x + rect.width - size.width;
  const f32 maxY    = rect.y + rect.height - size.height;
  switch (align) {
  case UiTextAlign_TopLeft:
    return ui_rect(ui_vector(rect.x, maxY), size);
  case UiTextAlign_TopCenter:
    return ui_rect(ui_vector(centerX, maxY), size);
  case UiTextAlign_TopRight:
    return ui_rect(ui_vector(maxX, maxY), size);
  case UiTextAlign_MiddleLeft:
    return ui_rect(ui_vector(rect.x, centerY), size);
  case UiTextAlign_MiddleCenter:
    return ui_rect(ui_vector(centerX, centerY), size);
  case UiTextAlign_MiddleRight:
    return ui_rect(ui_vector(maxX, centerY), size);
  case UiTextAlign_BottomLeft:
    return ui_rect(rect.pos, size);
  case UiTextAlign_BottomCenter:
    return ui_rect(ui_vector(centerX, rect.y), size);
  case UiTextAlign_BottomRight:
    return ui_rect(ui_vector(maxX, rect.y), size);
  }
  diag_crash();
}

static UiVector ui_text_char_pos(UiTextBuildState* state, const UiTextLine* line) {
  const UiRect rect = state->rect;
  const f32    posY = rect.y + rect.height - line->posY;
  switch (state->align) {
  case UiTextAlign_TopLeft:
  case UiTextAlign_MiddleLeft:
  case UiTextAlign_BottomLeft:
    return ui_vector(rect.x + state->cursor, posY);
  case UiTextAlign_TopCenter:
  case UiTextAlign_MiddleCenter:
  case UiTextAlign_BottomCenter:
    return ui_vector(rect.x + (rect.width - line->size.x) * 0.5f + state->cursor, posY);
    break;
  case UiTextAlign_TopRight:
  case UiTextAlign_MiddleRight:
  case UiTextAlign_BottomRight:
    return ui_vector(rect.x + rect.width - line->size.x + state->cursor, posY);
  }
  diag_crash();
}

static void ui_text_build_char(UiTextBuildState* state, const UiTextLine* line, const Unicode cp) {
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cp);
  if (!sentinel_check(ch->glyphIndex)) {
    state->buildChar(
        state->userCtx,
        &(UiTextCharInfo){
            .ch      = ch,
            .pos     = ui_text_char_pos(state, line),
            .size    = state->fontSize,
            .color   = state->fontColor,
            .outline = state->fontOutline,
        });
  }
  state->cursor += ch->advance * state->fontSize;
}

static void ui_text_build_escape(UiTextBuildState* state, const UiEscape* esc) {
  switch (esc->type) {
  case UiEscape_Invalid:
    break;
  case UiEscape_Reset:
    state->fontColor   = state->fontColorDefault;
    state->fontOutline = state->fontOutlineDefault;
    break;
  case UiEscape_Color:
    state->fontColor = esc->escColor.value;
    break;
  case UiEscape_Outline:
    state->fontOutline = esc->escOutline.value;
    break;
  }
}

static void ui_text_build_line(UiTextBuildState* state, const UiTextLine* line) {
  state->cursor        = 0;
  String remainingText = line->text;
  while (!string_is_empty(remainingText)) {
    Unicode cp;
    remainingText = utf8_cp_read(remainingText, &cp);

    UiEscape esc;
    switch ((u32)cp) {
    case Unicode_CarriageReturn:
      state->cursor = 0;
      continue;
    case Unicode_HorizontalTab:
      state->cursor = ui_text_next_tabstop(state->font, state->cursor, state->fontSize);
      continue;
    case Unicode_ZeroWidthSpace:
      continue;
    case Unicode_Escape:
    case Unicode_Bell:
      remainingText = ui_escape_read(remainingText, &esc);
      ui_text_build_escape(state, &esc);
      continue;
    }
    ui_text_build_char(state, line, cp);
  }
}

UiTextBuildResult ui_text_build(
    const AssetFtxComp*       font,
    const UiRect              totalRect,
    const String              text,
    const f32                 fontSize,
    const UiColor             fontColor,
    const u8                  fontOutline,
    const UiTextAlign         align,
    void*                     userCtx,
    const UiTextBuildCharFunc buildChar) {

  /**
   * Compute all lines.
   */
  UiTextLine lines[ui_text_max_lines];
  u32        lineCount  = 0;
  f32        lineY      = 0;
  f32        totalWidth = 0;
  String     remText    = text;
  while (!string_is_empty(remText)) {
    const f32 lineHeight = lineCount ? (1 + font->lineSpacing) * fontSize : fontSize;
    if (lineY + lineHeight >= totalRect.height - font->lineSpacing * fontSize) {
      break; // Not enough space remaining for this line.
    }
    lineY += lineHeight;

    if (lineCount + 1 >= ui_text_max_lines) {
      log_w("Ui text line count exceeds maximum", log_param("limit", fmt_int(ui_text_max_lines)));
      break;
    }
    const usize lineIndex = lineCount++;
    remText = ui_text_line(font, remText, totalRect.width, fontSize, &lines[lineIndex]);

    lines[lineIndex].posY = lineY;
    totalWidth            = math_max(totalWidth, lines[lineIndex].size.width);
  }
  const UiVector size = ui_vector(totalWidth, lineY + (font->lineSpacing * 2) * fontSize);
  const UiRect   rect = ui_text_inner_rect(totalRect, size, align);

  /**
   * Draw all lines.
   */
  UiTextBuildState state = {
      .font               = font,
      .rect               = rect,
      .fontSize           = fontSize,
      .fontColor          = fontColor,
      .fontColorDefault   = fontColor,
      .fontOutline        = fontOutline,
      .fontOutlineDefault = fontOutline,
      .align              = align,
      .userCtx            = userCtx,
      .buildChar          = buildChar,
  };
  for (usize i = 0; i != lineCount; ++i) {
    ui_text_build_line(&state, &lines[i]);
  }

  return (UiTextBuildResult){
      .rect      = rect,
      .lineCount = lineCount,
  };
}
