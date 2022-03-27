#pragma once
#include "ui_color.h"
#include "ui_escape.h"

typedef enum {
  UiEscape_Invalid,
  UiEscape_Reset,
  UiEscape_Color,
  UiEscape_Outline,
  UiEscape_Weight,
} UiEscapeType;

typedef struct {
  UiColor value;
} UiEscapeColor;

typedef struct {
  u8 value;
} UiEscapeOutline;

typedef struct {
  UiWeight value;
} UiEscapeWeight;

typedef struct {
  UiEscapeType type;
  union {
    UiEscapeColor   escColor;
    UiEscapeOutline escOutline;
    UiEscapeWeight  escWeight;
  };
} UiEscape;

/**
 * Parse an escape sequence, pass 'null' to ignore the output.
 * NOTE: Does not read the leading escape character.
 */
String ui_escape_read(String input, UiEscape* out);
