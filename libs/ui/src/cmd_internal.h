#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_canvas.h"

typedef enum {
  UiCmd_RectPush,
  UiCmd_RectPop,
  UiCmd_RectMove,
  UiCmd_RectResize,
  UiCmd_RectResizeTo,
  UiCmd_StylePush,
  UiCmd_StylePop,
  UiCmd_StyleColor,
  UiCmd_StyleOutline,
  UiCmd_DrawText,
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiVector pos;
  UiOrigin origin;
  UiUnits  unit;
  UiAxis   axis;
} UiRectMove;

typedef struct {
  UiVector size;
  UiUnits  unit;
  UiAxis   axis;
} UiRectResize;

typedef struct {
  UiVector pos;
  UiOrigin origin;
  UiUnits  unit;
  UiAxis   axis;
} UiRectResizeTo;

typedef struct {
  UiColor value;
} UiStyleColor;

typedef struct {
  u8 value;
} UiStyleOutline;

typedef struct {
  UiId    id;
  String  text;
  u16     fontSize;
  UiAlign align;
  UiFlags flags;
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
    UiStyleColor   styleColor;
    UiStyleOutline styleOutline;
    UiDrawText     drawText;
    UiDrawGlyph    drawGlyph;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);

void ui_cmd_push_rect_push(UiCmdBuffer*);
void ui_cmd_push_rect_pop(UiCmdBuffer*);
void ui_cmd_push_rect_move(UiCmdBuffer*, UiVector pos, UiOrigin, UiUnits, UiAxis);
void ui_cmd_push_rect_resize(UiCmdBuffer*, UiVector size, UiUnits, UiAxis);
void ui_cmd_push_rect_resize_to(UiCmdBuffer*, UiVector pos, UiOrigin, UiUnits, UiAxis);
void ui_cmd_push_style_push(UiCmdBuffer*);
void ui_cmd_push_style_pop(UiCmdBuffer*);
void ui_cmd_push_style_color(UiCmdBuffer*, UiColor);
void ui_cmd_push_style_outline(UiCmdBuffer*, u8 outline);
void ui_cmd_push_draw_text(UiCmdBuffer*, UiId, String text, u16 fontSize, UiAlign, UiFlags);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiId, Unicode cp, u16 maxCorner, UiFlags);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
