#include "core_diag.h"
#include "core_math.h"
#include "core_unicode.h"
#include "core_utf8.h"
#include "log_logger.h"
#include "ui_shape.h"

#include "escape_internal.h"
#include "text_internal.h"

#define ui_text_tab_size 8
#define ui_text_max_lines 100
#define ui_text_max_backgrounds 50

typedef struct {
  const AssetFtxComp*       font;
  const String              totalText;
  const UiRect              rect;
  const f32                 fontSize;
  UiColor                   fontColor, fontColorDefault;
  u8                        fontOutline, fontOutlineDefault;
  const UiLayer             fontLayer;
  const u8                  fontVariation;
  UiWeight                  fontWeight, fontWeightDefault;
  const UiAlign             align;
  void*                     userCtx;
  UiTextBuildCharFunc       buildChar;
  UiTextBuildBackgroundFunc buildBackground;
  f32                       cursor;
  const UiVector            inputPosition;
  usize                     hoveredCharIndex;
} UiTextBuildState;

typedef struct {
  String   text;
  UiVector size;
  f32      posY;
} UiTextLine;

typedef struct {
  const UiTextLine* line;
  UiColor           color;
  f32               start, end;
} UiTextBackground;

typedef struct {
  UiTextBackground values[ui_text_max_backgrounds];
  u32              count, active;
} UiTextBackgroundCollector;

static void ui_text_background_start(
    UiTextBackgroundCollector* collector,
    const UiTextLine*          line,
    const UiColor              color,
    const f32                  xPos) {
  diag_assert(sentinel_check(collector->active));
  if (UNLIKELY(collector->count == ui_text_max_backgrounds)) {
    log_w("Ui text background count exceeds maximum");
    return;
  }
  collector->active                    = collector->count++;
  collector->values[collector->active] = (UiTextBackground){
      .line  = line,
      .color = color,
      .start = xPos,
  };
}

static void ui_text_background_end(UiTextBackgroundCollector* collector, const f32 xPos) {
  if (!sentinel_check(collector->active)) {
    collector->values[collector->active].end = xPos;
    collector->active                        = sentinel_u32;
  }
}

static f32 ui_text_to_tabstop(
    const AssetFtxComp* font, const f32 cursor, const f32 fontSize, const u8 fontVariation) {
  const f32 spaceAdvance = asset_ftx_lookup(font, Unicode_Space, fontVariation)->advance * fontSize;
  const f32 tabSize      = spaceAdvance * ui_text_tab_size;
  return tabSize - math_mod_f32(cursor + spaceAdvance, tabSize);
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
    const AssetFtxComp*        font,
    const UiFlags              flags,
    const String               text,
    const f32                  maxWidth,
    const f32                  fontSize,
    const u8                   fontVariation,
    UiTextBackgroundCollector* bgCollector,
    UiTextLine*                out) {

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

    const bool isSeperator    = ui_text_is_seperator(cp);
    const bool allowWordBreak = firstWord || flags & UiFlags_AllowWordBreak;
    if ((isSeperator && !wasSeperator) || allowWordBreak) {
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
      cursorConsumed.pixel +=
          ui_text_to_tabstop(font, cursorConsumed.pixel, fontSize, fontVariation);
      break;
    case Unicode_ZeroWidthSpace:
      break;
    case Unicode_Escape:
    case Unicode_Bell: {
      UiEscape esc;
      remainingText            = ui_escape_read(remainingText, &esc);
      cursorConsumed.charIndex = text.size - remainingText.size;

      if (esc.type == UiEscape_Background || esc.type == UiEscape_Reset) {
        ui_text_background_end(bgCollector, cursorAccepted.pixel);
      }
      if (esc.type == UiEscape_Background) {
        ui_text_background_start(bgCollector, out, esc.escBackground.value, cursorAccepted.pixel);
      }
      break;
    }
    default:
      cursorConsumed.pixel += asset_ftx_lookup(font, cp, fontVariation)->advance * fontSize;
      break;
    }
    if (cursorConsumed.pixel > maxWidth) {
      goto End;
    }
  }

End:
  ui_text_background_end(bgCollector, cursorAccepted.pixel);
  *out = (UiTextLine){
      .text = string_slice(text, 0, cursorAccepted.charIndex),
      .size = ui_vector(cursorAccepted.pixel, fontSize),
  };
  return string_consume(text, cursorConsumed.charIndex);
}

static UiRect ui_text_inner_rect(const UiRect rect, const UiVector size, const UiAlign align) {
  const f32 centerX = rect.x + (rect.width - size.width) * 0.5f;
  const f32 centerY = rect.y + (rect.height - size.height) * 0.5f;
  const f32 maxX    = rect.x + rect.width - size.width;
  const f32 maxY    = rect.y + rect.height - size.height;
  switch (align) {
  case UiAlign_TopLeft:
    return ui_rect(ui_vector(rect.x, maxY), size);
  case UiAlign_TopCenter:
    return ui_rect(ui_vector(centerX, maxY), size);
  case UiAlign_TopRight:
    return ui_rect(ui_vector(maxX, maxY), size);
  case UiAlign_MiddleLeft:
    return ui_rect(ui_vector(rect.x, centerY), size);
  case UiAlign_MiddleCenter:
    return ui_rect(ui_vector(centerX, centerY), size);
  case UiAlign_MiddleRight:
    return ui_rect(ui_vector(maxX, centerY), size);
  case UiAlign_BottomLeft:
    return ui_rect(rect.pos, size);
  case UiAlign_BottomCenter:
    return ui_rect(ui_vector(centerX, rect.y), size);
  case UiAlign_BottomRight:
    return ui_rect(ui_vector(maxX, rect.y), size);
  }
  diag_crash();
}

static UiVector ui_text_char_pos(UiTextBuildState* state, const UiTextLine* line, const f32 posX) {
  const UiRect rect = state->rect;
  const f32    resY = rect.y + rect.height - line->posY;
  switch (state->align) {
  case UiAlign_TopLeft:
  case UiAlign_MiddleLeft:
  case UiAlign_BottomLeft:
    return ui_vector(rect.x + posX, resY);
  case UiAlign_TopCenter:
  case UiAlign_MiddleCenter:
  case UiAlign_BottomCenter:
    return ui_vector(rect.x + (rect.width - line->size.x) * 0.5f + posX, resY);
    break;
  case UiAlign_TopRight:
  case UiAlign_MiddleRight:
  case UiAlign_BottomRight:
    return ui_vector(rect.x + rect.width - line->size.x + posX, resY);
  }
  diag_crash();
}

/**
 * Get the byte-index into the total text.
 */
static usize ui_text_byte_index(UiTextBuildState* state, const String str) {
  return (u8*)str.ptr - (u8*)state->totalText.ptr;
}

static UiColor ui_text_color_alpha_mul(const UiColor color, const u8 alpha) {
  return ui_color(color.r, color.g, color.b, (u8)(color.a * (alpha / 255.0f)));
}

static void ui_text_update_hover(
    UiTextBuildState* state,
    const UiVector    pos,
    const f32         advance,
    const usize       charIndex,
    const usize       nextCharIndex) {

  if (UNLIKELY(sentinel_check(state->hoveredCharIndex))) {
    /**
     * This is the first (selectable) character, make it the initial hovered-character.
     */
    state->hoveredCharIndex = charIndex;
  }

  if (pos.x + advance * 0.5f < state->inputPosition.x) {
    /**
     * Input is beyond the middle of this character, move the hoveredCharIndex to the next char.
     * TODO: For multi-line support this would need to check if we're within the current line.
     */
    state->hoveredCharIndex = nextCharIndex;
  }
}

static void ui_text_build_char(
    UiTextBuildState* state,
    const UiVector    pos,
    const Unicode     cp,
    const usize       charIndex,
    const usize       nextCharIndex) {

  const AssetFtxChar* ch      = asset_ftx_lookup(state->font, cp, state->fontVariation);
  const f32           advance = ch->advance * state->fontSize;
  ui_text_update_hover(state, pos, advance, charIndex, nextCharIndex);

  if (!sentinel_check(ch->glyphIndex)) {
    state->buildChar(
        state->userCtx,
        &(UiTextCharInfo){
            .ch      = ch,
            .pos     = pos,
            .size    = state->fontSize,
            .color   = state->fontColor,
            .outline = state->fontOutline,
            .layer   = state->fontLayer,
            .weight  = state->fontWeight,
        });
  }
  state->cursor += advance;
}

static void ui_text_build_cursor(UiTextBuildState* state, const UiTextLine* line, const u8 alpha) {
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, UiShape_CursorVertialBar, 0);
  if (!sentinel_check(ch->glyphIndex)) {
    state->buildChar(
        state->userCtx,
        &(UiTextCharInfo){
            .ch      = ch,
            .pos     = ui_text_char_pos(state, line, state->cursor),
            .size    = state->fontSize,
            .color   = ui_text_color_alpha_mul(state->fontColor, alpha),
            .outline = state->fontOutline,
            .layer   = UiLayer_Overlay,
            .weight  = 1,
        });
  }
}

static void
ui_text_build_escape(UiTextBuildState* state, const UiTextLine* line, const UiEscape* esc) {
  switch (esc->type) {
  case UiEscape_Invalid:
    break;
  case UiEscape_Reset:
    state->fontColor   = state->fontColorDefault;
    state->fontOutline = state->fontOutlineDefault;
    state->fontWeight  = state->fontWeightDefault;
    break;
  case UiEscape_Color:
    state->fontColor = esc->escColor.value;
    break;
  case UiEscape_Background:
    // NOTE: Handled in the line collecting phase.
    break;
  case UiEscape_Outline:
    state->fontOutline = esc->escOutline.value;
    break;
  case UiEscape_Weight:
    state->fontWeight = esc->escWeight.value;
    break;
  case UiEscape_Cursor:
    ui_text_build_cursor(state, line, esc->escCursor.alpha);
    break;
  }
}

static void ui_text_build_line(UiTextBuildState* state, const UiTextLine* line) {
  state->cursor        = 0;
  String remainingText = line->text;
  usize  charIndex     = ui_text_byte_index(state, remainingText);
  while (!string_is_empty(remainingText)) {
    Unicode cp;
    remainingText = utf8_cp_read(remainingText, &cp);

    const UiVector pos           = ui_text_char_pos(state, line, state->cursor);
    usize          nextCharIndex = ui_text_byte_index(state, remainingText);

    UiEscape esc;
    switch (cp) {
    case Unicode_CarriageReturn:
      state->cursor = 0;
      break;
    case Unicode_HorizontalTab: {
      const f32 advance =
          ui_text_to_tabstop(state->font, state->cursor, state->fontSize, state->fontVariation);
      ui_text_update_hover(state, pos, advance, charIndex, nextCharIndex);
      state->cursor += advance;
      break;
    }
    case Unicode_ZeroWidthSpace:
      break;
    case Unicode_Escape:
    case Unicode_Bell:
      remainingText = ui_escape_read(remainingText, &esc);
      nextCharIndex = ui_text_byte_index(state, remainingText);
      ui_text_build_escape(state, line, &esc);
      break;
    default:
      ui_text_build_char(state, pos, cp, charIndex, nextCharIndex);
      break;
    }
    charIndex = nextCharIndex;
  }
}

static void ui_text_build_background(UiTextBuildState* state, const UiTextBackground* bg) {
  const UiVector startPos       = ui_text_char_pos(state, bg->line, bg->start);
  const UiVector endPos         = ui_text_char_pos(state, bg->line, bg->end);
  const f32      yBottomPadding = state->fontSize * state->font->baseline;
  const UiRect   rect           = {
                  .pos  = ui_vector(startPos.x, startPos.y - yBottomPadding),
                  .size = ui_vector(endPos.x - startPos.x, bg->line->size.y + yBottomPadding),
  };
  state->buildBackground(
      state->userCtx,
      &(UiTextBackgroundInfo){
          .rect  = rect,
          .color = bg->color,
          .layer = state->fontLayer,
      });
}

UiTextBuildResult ui_text_build(
    const AssetFtxComp*             font,
    const UiFlags                   flags,
    const UiRect                    totalRect,
    const UiVector                  inputPosition,
    const String                    text,
    const f32                       fontSize,
    const UiColor                   fontColor,
    const u8                        fontOutline,
    const UiLayer                   fontLayer,
    const u8                        fontVariation,
    const UiWeight                  fontWeight,
    const UiAlign                   align,
    void*                           userCtx,
    const UiTextBuildCharFunc       buildChar,
    const UiTextBuildBackgroundFunc buildBackground) {

  /**
   * Compute all lines and backgrounds.
   */
  UiTextBackgroundCollector bgCollector = {.active = sentinel_u32};
  UiTextLine                lines[ui_text_max_lines];
  u32                       lineCount        = 0;
  f32                       lineY            = 0;
  f32                       totalWidth       = 0;
  u32                       maxLineCharWidth = 0;
  String                    remText          = text;
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
    remText               = ui_text_line(
        font,
        flags,
        remText,
        totalRect.width,
        fontSize,
        fontVariation,
        &bgCollector,
        &lines[lineIndex]);

    lines[lineIndex].posY = lineY;
    totalWidth            = math_max(totalWidth, lines[lineIndex].size.width);
    maxLineCharWidth      = math_max(maxLineCharWidth, (u32)lines[lineIndex].text.size);

    if (flags & UiFlags_SingleLine) {
      break;
    }
  }
  const UiVector size = ui_vector(totalWidth, lineY + font->baseline * fontSize);
  const UiRect   rect = ui_text_inner_rect(totalRect, size, align);

  UiTextBuildState state = {
      .font               = font,
      .totalText          = text,
      .rect               = rect,
      .fontSize           = fontSize,
      .fontColor          = fontColor,
      .fontColorDefault   = fontColor,
      .fontOutline        = fontOutline,
      .fontOutlineDefault = fontOutline,
      .fontLayer          = fontLayer,
      .fontVariation      = fontVariation,
      .fontWeight         = fontWeight,
      .fontWeightDefault  = fontWeight,
      .align              = align,
      .userCtx            = userCtx,
      .buildChar          = buildChar,
      .buildBackground    = buildBackground,
      .inputPosition      = inputPosition,
      .hoveredCharIndex   = sentinel_usize,
  };

  /**
   * Draw all backgrounds.
   */
  for (usize i = 0; i != bgCollector.count; ++i) {
    ui_text_build_background(&state, &bgCollector.values[i]);
  }

  /**
   * Draw all lines.
   */
  for (usize i = 0; i != lineCount; ++i) {
    ui_text_build_line(&state, &lines[i]);
  }

  return (UiTextBuildResult){
      .rect             = rect,
      .lineCount        = lineCount,
      .maxLineCharWidth = maxLineCharWidth,
      .hoveredCharIndex = state.hoveredCharIndex,
  };
}
