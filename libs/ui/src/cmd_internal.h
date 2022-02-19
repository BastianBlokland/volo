#pragma once
#include "core_alloc.h"
#include "core_unicode.h"
#include "ui_color.h"
#include "ui_vector.h"

typedef u64 UiElementId;

typedef enum {
  UiCmd_SetSize,
  UiCmd_SetColor,
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiVector size;
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
    UiSetSize   setSize;
    UiSetColor  setColor;
    UiDrawGlyph drawGlyph;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);

void ui_cmd_push_set_size(UiCmdBuffer*, UiSetSize);
void ui_cmd_push_set_color(UiCmdBuffer*, UiSetColor);
void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiDrawGlyph);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
