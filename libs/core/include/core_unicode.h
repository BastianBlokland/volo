#pragma once
#include "core_types.h"

/**
 * A single unicode codepoint.
 * https://en.wikipedia.org/wiki/Unicode#Architecture_and_terminology
 */
typedef enum {
  Unicode_Invalid        = 0,
  Unicode_Bell           = 7,
  Unicode_Backspace      = 8,
  Unicode_HorizontalTab  = 9,
  Unicode_Newline        = 10,
  Unicode_VerticalTab    = 11,
  Unicode_FormFeed       = 12,
  Unicode_CarriageReturn = 13,
  Unicode_Escape         = 27,
  Unicode_Space          = 32,
  Unicode_Delete         = 127,
} Unicode;
