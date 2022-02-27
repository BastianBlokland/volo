#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_canvas.h"

typedef enum {
  UiCmd_Move,
  UiCmd_Size,
  UiCmd_SizeTo,
  UiCmd_Style,
  UiCmd_DrawText,
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiVector pos;
  UiOrigin origin;
  UiUnits  unit;
} UiMove;

typedef struct {
  UiVector size;
  UiUnits  unit;
} UiSize;

typedef struct {
  UiVector pos;
  UiOrigin origin;
  UiUnits  unit;
} UiSizeTo;

typedef struct {
  UiColor color;
  u8      outline;
} UiStyle;

typedef struct {
  UiElementId id;
  String      text;
  u16         fontSize;
  UiTextAlign align;
} UiDrawText;

typedef struct {
  UiElementId id;
  Unicode     cp;
  u16         maxCorner;
} UiDrawGlyph;

typedef struct {
  UiCmdType type;
  union {
    UiMove      move;
    UiSize      size;
    UiSizeTo    sizeTo;
    UiStyle     style;
    UiDrawText  drawText;
    UiDrawGlyph drawGlyph;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);

void ui_cmd_push_move(UiCmdBuffer*, UiVector pos, UiOrigin, UiUnits);
void ui_cmd_push_size(UiCmdBuffer*, UiVector size, UiUnits);
void ui_cmd_push_size_to(UiCmdBuffer*, UiVector pos, UiOrigin, UiUnits);
void ui_cmd_push_style(UiCmdBuffer*, UiColor, u8 outline);
void ui_cmd_push_draw_text(UiCmdBuffer*, UiElementId, String text, u16 fontSize, UiTextAlign);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiElementId, Unicode cp, u16 maxCorner);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
