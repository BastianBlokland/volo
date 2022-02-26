#pragma once
#include "core_types.h"

/**
 * A single unicode codepoint.
 * https://en.wikipedia.org/wiki/Unicode#Architecture_and_terminology
 */
typedef enum {
  Unicode_Invalid        = 0x0,
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
