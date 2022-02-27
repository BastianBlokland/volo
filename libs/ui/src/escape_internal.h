#pragma once
#include "ui_color.h"
#include "ui_escape.h"

typedef enum {
  UiEscape_Invalid,
  UiEscape_Color,
} UiEscapeType;

typedef struct {
  UiColor value;
} UiEscapeColor;

typedef struct {
  UiEscapeType type;
  union {
    UiEscapeColor escColor;
  };
} UiEscape;

/**
 * Parse an escape sequence, pass 'null' to ignore the output.
 * NOTE: Does not read the leading escape character.
 */
String ui_escape_read(String input, UiEscape* out);
