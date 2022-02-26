#include "core_math.h"
#include "core_unicode.h"
#include "core_utf8.h"

#include "text_internal.h"

#define ui_text_tab_size 4

static f32 ui_text_next_tabstop(const AssetFtxComp* font, const f32 cursor, const f32 fontSize) {
  const f32 spaceAdvance = asset_ftx_lookup(font, Unicode_Space)->advance * fontSize;
  const f32 tabSize      = spaceAdvance * ui_text_tab_size;
  return cursor + math_mod_f32(cursor, tabSize);
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

String ui_text_line(
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
      lineEnd = consumedEnd = text.size - remainingText.size - 1;
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
