#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_canvas.h"

typedef enum {
  UiCmd_RectMove,
  UiCmd_RectResize,
  UiCmd_RectResizeTo,
  UiCmd_Style,
  UiCmd_DrawText,
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiVector pos;
  UiOrigin origin;
  UiUnits  unit;
} UiRectMove;

typedef struct {
  UiVector size;
  UiUnits  unit;
} UiRectResize;

typedef struct {
  UiVector pos;
  UiOrigin origin;
  UiUnits  unit;
} UiRectResizeTo;

typedef struct {
  UiColor color;
  u8      outline;
} UiStyle;

typedef struct {
  UiId        id;
  String      text;
  u16         fontSize;
  UiTextAlign align;
  UiFlags     flags;
} UiDrawText;

typedef struct {
  UiId    id;
  Unicode cp;
  u16     maxCorner;
  UiFlags flags;
} UiDrawGlyph;

typedef struct {
  UiCmdType type;
  union {
    UiRectMove     rectMove;
    UiRectResize   rectResize;
    UiRectResizeTo rectResizeTo;
    UiStyle        style;
    UiDrawText     drawText;
    UiDrawGlyph    drawGlyph;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);

void ui_cmd_push_rect_move(UiCmdBuffer*, UiVector pos, UiOrigin, UiUnits);
void ui_cmd_push_rect_resize(UiCmdBuffer*, UiVector size, UiUnits);
void ui_cmd_push_rect_resize_to(UiCmdBuffer*, UiVector pos, UiOrigin, UiUnits);
void ui_cmd_push_style(UiCmdBuffer*, UiColor, u8 outline);
void ui_cmd_push_draw_text(UiCmdBuffer*, UiId, String text, u16 fontSize, UiTextAlign, UiFlags);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiId, Unicode cp, u16 maxCorner, UiFlags);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
