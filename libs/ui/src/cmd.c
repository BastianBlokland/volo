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

void ui_cmdbuffer_clear(UiCmdBuffer* buffer) { dynarray_clear(&buffer->commands); }

void ui_cmd_push_set_pos(
    UiCmdBuffer* buffer, const UiVector pos, const UiOrigin origin, const UiUnits units) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type   = UiCmd_SetPos,
      .setPos = {
          .pos    = pos,
          .origin = origin,
          .units  = units,
      }};
}

void ui_cmd_push_set_size(UiCmdBuffer* buffer, const UiVector size, const UiUnits units) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type    = UiCmd_SetSize,
      .setSize = {
          .size  = size,
          .units = units,
      }};
}

void ui_cmd_push_set_flow(UiCmdBuffer* buffer, const UiFlow flow) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type    = UiCmd_SetFlow,
      .setFlow = {
          .flow = flow,
      }};
}

void ui_cmd_push_set_style(UiCmdBuffer* buffer, const UiColor color, const u8 outline) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type     = UiCmd_SetStyle,
      .setStyle = {
          .color   = color,
          .outline = outline,
      }};
}

void ui_cmd_push_draw_glyph(
    UiCmdBuffer* buffer, const UiElementId id, const Unicode cp, const u16 maxCorner) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type      = UiCmd_DrawGlyph,
      .drawGlyph = {
          .id        = id,
          .cp        = cp,
          .maxCorner = maxCorner,
      }};
}

UiCmd* ui_cmd_next(const UiCmdBuffer* buffer, UiCmd* prev) {
  if (!prev) {
    return dynarray_begin_t(&buffer->commands, UiCmd);
  }
  UiCmd* next = ++prev;
  return next == dynarray_end_t(&buffer->commands, UiCmd) ? null : next;
}
