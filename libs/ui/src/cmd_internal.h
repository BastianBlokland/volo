#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_canvas.h"
#include "ui_color.h"

typedef enum {
  UiCmd_RectPush,
  UiCmd_RectPop,
  UiCmd_RectPos,
  UiCmd_RectSize,
  UiCmd_RectSizeTo,
  UiCmd_RectSizeGrow,
  UiCmd_ContainerPush,
  UiCmd_ContainerPop,
  UiCmd_StylePush,
  UiCmd_StylePop,
  UiCmd_StyleColor,
  UiCmd_StyleColorMult,
  UiCmd_StyleOutline,
  UiCmd_StyleLayer,
  UiCmd_StyleVariation,
  UiCmd_StyleWeight,
  UiCmd_DrawText,
  UiCmd_DrawGlyph,
  UiCmd_DrawImage,
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
  UiBase   origin;
  UiVector offset;
  UiBase   units;
  UiAxis   axis;
} UiRectSizeTo;

typedef struct {
  UiVector delta;
  UiBase   units;
  UiAxis   axis;
} UiRectSizeGrow;

typedef struct {
  UiClip clip;
} UiContainerPush;

typedef struct {
  UiColor value;
} UiStyleColor;

typedef struct {
  f32 value;
} UiStyleColorMult;

typedef struct {
  u8 value;
} UiStyleOutline;

typedef struct {
  UiLayer value;
} UiStyleLayer;

typedef struct {
  u8 value;
} UiStyleVariation;

typedef struct {
  UiWeight value;
} UiStyleWeight;

typedef struct {
  UiId    id;
  void*   textPtr;
  u16     textSize;
  u16     fontSize;
  UiFlags flags : 16;
  UiAlign align : 8;
} UiDrawText;

typedef struct {
  UiId    id;
  Unicode cp;
  f32     angleRad;
  u16     maxCorner;
  UiFlags flags : 16;
} UiDrawGlyph;

typedef struct {
  UiId       id;
  StringHash img;
  f32        angleRad;
  u16        maxCorner;
  UiFlags    flags : 16;
} UiDrawImage;

typedef struct {
  UiCmdType type;
  union {
    UiRectPos        rectPos;
    UiRectSize       rectSize;
    UiRectSizeTo     rectSizeTo;
    UiRectSizeGrow   rectSizeGrow;
    UiContainerPush  containerPush;
    UiStyleColor     styleColor;
    UiStyleColorMult styleColorMult;
    UiStyleOutline   styleOutline;
    UiStyleLayer     styleLayer;
    UiStyleVariation styleVariation;
    UiStyleWeight    styleWeight;
    UiDrawText       drawText;
    UiDrawGlyph      drawGlyph;
    UiDrawImage      drawImage;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);
u32          ui_cmdbuffer_count(const UiCmdBuffer*);

void ui_cmd_push_rect_push(UiCmdBuffer*);
void ui_cmd_push_rect_pop(UiCmdBuffer*);
void ui_cmd_push_rect_pos(UiCmdBuffer*, UiBase origin, UiVector offset, UiBase units, UiAxis);
void ui_cmd_push_rect_size(UiCmdBuffer*, UiVector size, UiBase units, UiAxis);
void ui_cmd_push_rect_size_to(UiCmdBuffer*, UiBase origin, UiVector offset, UiBase units, UiAxis);
void ui_cmd_push_rect_size_grow(UiCmdBuffer*, UiVector delta, UiBase units, UiAxis);
void ui_cmd_push_container_push(UiCmdBuffer*, UiClip);
void ui_cmd_push_container_pop(UiCmdBuffer*);
void ui_cmd_push_style_push(UiCmdBuffer*);
void ui_cmd_push_style_pop(UiCmdBuffer*);
void ui_cmd_push_style_color(UiCmdBuffer*, UiColor);
void ui_cmd_push_style_color_mult(UiCmdBuffer*, f32 value);
void ui_cmd_push_style_outline(UiCmdBuffer*, u8 outline);
void ui_cmd_push_style_layer(UiCmdBuffer*, UiLayer);
void ui_cmd_push_style_variation(UiCmdBuffer*, u8 variation);
void ui_cmd_push_style_weight(UiCmdBuffer*, UiWeight);
void ui_cmd_push_draw_text(UiCmdBuffer*, UiId, String text, u16 fontSize, UiAlign, UiFlags);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiId, Unicode cp, u16 maxCorner, f32 angleRad, UiFlags);
void ui_cmd_push_draw_image(UiCmdBuffer*, UiId, StringHash, u16 maxCorner, f32 angleRad, UiFlags);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
