#pragma once
#include "core_alloc.h"
#include "core_unicode.h"

typedef u64 UiElementId;

typedef enum {
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiElementId id;
  Unicode     cp;
} UiDrawGlyph;

typedef struct {
  UiCmdType type;
  union {
    UiDrawGlyph drawGlyph;
  };
} UiCmd;

typedef struct sUiCmdBuffer UiCmdBuffer;

UiCmdBuffer* ui_cmdbuffer_create(Allocator*);
void         ui_cmdbuffer_destroy(UiCmdBuffer*);
void         ui_cmdbuffer_clear(UiCmdBuffer*);

void ui_cmd_push_draw_glyph(UiCmdBuffer*, UiDrawGlyph);

UiCmd* ui_cmd_next(const UiCmdBuffer*, UiCmd*);
