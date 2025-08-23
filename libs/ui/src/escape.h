#pragma once
#include "ui/color.h"
#include "ui/units.h"

typedef enum {
  UiEscape_Invalid,
  UiEscape_Reset,
  UiEscape_PadUntil,
  UiEscape_Color,
  UiEscape_Background,
  UiEscape_Outline,
  UiEscape_Weight,
  UiEscape_Cursor,
} UiEscapeType;

typedef struct {
  u8 stop;
} UiEscapePadUntil;

typedef struct {
  UiColor value;
} UiEscapeColor;

typedef struct {
  UiColor value;
} UiEscapeBackground;

typedef struct {
  u8 value;
} UiEscapeOutline;

typedef struct {
  UiWeight value;
} UiEscapeWeight;

typedef struct {
  u8 alpha;
} UiEscapeCursor;

typedef struct {
  UiEscapeType type;
  union {
    UiEscapePadUntil   escPadUntil;
    UiEscapeColor      escColor;
    UiEscapeBackground escBackground;
    UiEscapeOutline    escOutline;
    UiEscapeWeight     escWeight;
    UiEscapeCursor     escCursor;
  };
} UiEscape;

/**
 * Parse an escape sequence, pass 'null' to ignore the output.
 * NOTE: Does not read the leading escape character.
 */
String ui_escape_read(String input, UiEscape* out);
