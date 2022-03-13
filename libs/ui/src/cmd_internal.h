#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_canvas.h"

typedef enum {
  UiCmd_RectPush,
  UiCmd_RectPop,
  UiCmd_RectPos,
  UiCmd_RectSize,
  UiCmd_ContainerPush,
  UiCmd_ContainerPop,
  UiCmd_StylePush,
  UiCmd_StylePop,
  UiCmd_StyleColor,
  UiCmd_StyleOutline,
  UiCmd_StyleLayer,
  UiCmd_DrawText,
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiBase   origin;
  UiVector offset;
  UiBase   units;
  UiAxis   axis;
} UiRectPos;

typedef struct {
  UiVector size;
  UiBase   units;
  UiAxis   axis;
} UiRectSize;

typedef struct {
  UiColor value;
} UiStyleColor;

typedef struct {
  u8 value;
} UiStyleOutline;

typedef struct {
  UiLayer value;
} UiStyleLayer;

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
    UiRectPos      rectPos;
    UiRectSize     rectSize;
    UiStyleColor   styleColor;
    UiStyleOutline styleOutline;
    UiStyleLayer   styleLayer;
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
void ui_cmd_push_rect_pos(UiCmdBuffer*, UiBase origin, UiVector offset, UiBase units, UiAxis);
void ui_cmd_push_rect_size(UiCmdBuffer*, UiVector size, UiBase units, UiAxis);
void ui_cmd_push_rect_size_to(UiCmdBuffer*, UiBase origin, UiVector offset, UiBase units, UiAxis);
void ui_cmd_push_container_push(UiCmdBuffer*);
void ui_cmd_push_container_pop(UiCmdBuffer*);
void ui_cmd_push_style_push(UiCmdBuffer*);
void ui_cmd_push_style_pop(UiCmdBuffer*);
void ui_cmd_push_style_color(UiCmdBuffer*, UiColor);
void ui_cmd_push_style_outline(UiCmdBuffer*, u8 outline);
void ui_cmd_push_style_layer(UiCmdBuffer*, UiLayer);
void ui_cmd_push_draw_text(UiCmdBuffer*, UiId, String text, u16 fontSize, UiAlign, UiFlags);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiId, Unicode cp, u16 maxCorner, UiFlags);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
