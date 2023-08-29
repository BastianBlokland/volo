#pragma once
#include "core_types.h"

#define uni_zws "\u200B"
#define uni_esc "\33"

/**
 * A single unicode codepoint.
 * https://en.wikipedia.org/wiki/Unicode#Architecture_and_terminology
 */
typedef enum eUnicode {
  Unicode_Invalid        = 0x0,
  Unicode_EndOfText      = 0x3,
  Unicode_Bell           = 0x7,
  Unicode_Backspace      = 0x8,
  Unicode_HorizontalTab  = 0x9,
  Unicode_Newline        = 0xA,
  Unicode_VerticalTab    = 0xB,
  Unicode_FormFeed       = 0xC,
  Unicode_CarriageReturn = 0xD,
  Unicode_Escape         = 0x1B,
  Unicode_Space          = 0x20,
  Unicode_Delete         = 0x7F,
  Unicode_ZeroWidthSpace = 0x200B,
} Unicode;

/**
 * Test if the given unicode codepoint is an ascii character (<= 127).
 */
bool unicode_is_ascii(Unicode);
