#include "core_dynarray.h"

#include "cmd_internal.h"

struct sUiCmdBuffer {
  DynArray   commands; // UiCmd[]
  Allocator* alloc;
};

UiCmdBuffer* ui_cmdbuffer_create(Allocator* alloc) {
  UiCmdBuffer* buffer = alloc_alloc_t(alloc, UiCmdBuffer);
  *buffer             = (UiCmdBuffer){
      .commands = dynarray_create_t(alloc, UiCmd, 128),
      .alloc    = alloc,
  };
  return buffer;
}

void ui_cmdbuffer_destroy(UiCmdBuffer* buffer) {
  dynarray_destroy(&buffer->commands);
  alloc_free_t(buffer->alloc, buffer);
}

void ui_cmdbuffer_clear(UiCmdBuffer* buffer) { (void)buffer; }

void ui_cmd_push_draw_glyph(UiCmdBuffer* buffer, const UiDrawGlyph cmd) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_DrawGlyph, .drawGlyph = cmd};
}

UiCmd* ui_cmd_next(const UiCmdBuffer* buffer, UiCmd* prev) {
  if (!prev) {
    return dynarray_begin_t(&buffer->commands, UiCmd);
  }
  UiCmd* next = ++prev;
  return next == dynarray_end_t(&buffer->commands, UiCmd) ? null : next;
}
