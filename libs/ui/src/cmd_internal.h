#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_canvas.h"

typedef enum {
  UiCmd_SetPos,
  UiCmd_SetSize,
  UiCmd_SetColor,
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiVector pos;
  UiOrigin origin;
} UiSetPos;

typedef struct {
  UiVector size;
  UiUnits  units;
} UiSetSize;

typedef struct {
  UiColor color;
} UiSetColor;

typedef struct {
  UiElementId id;
  Unicode     cp;
} UiDrawGlyph;

typedef struct {
  UiCmdType type;
  union {
    UiSetPos    setPos;
    UiSetSize   setSize;
    UiSetColor  setColor;
    UiDrawGlyph drawGlyph;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);

void ui_cmd_push_set_pos(UiCmdBuffer*, UiVector pos, UiOrigin);
void ui_cmd_push_set_size(UiCmdBuffer*, UiVector size, UiUnits);
void ui_cmd_push_set_color(UiCmdBuffer*, UiColor);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiElementId, Unicode cp);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
