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

void ui_cmd_push_move(
    UiCmdBuffer* buffer, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type = UiCmd_Move,
      .move = {
          .pos    = pos,
          .origin = origin,
          .unit   = unit,
      }};
}

void ui_cmd_push_size(UiCmdBuffer* buffer, const UiVector size, const UiUnits unit) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type = UiCmd_Size,
      .size = {
          .size = size,
          .unit = unit,
      }};
}

void ui_cmd_push_size_to(
    UiCmdBuffer* buffer, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type   = UiCmd_SizeTo,
      .sizeTo = {
          .pos    = pos,
          .origin = origin,
          .unit   = unit,
      }};
}

void ui_cmd_push_style(UiCmdBuffer* buffer, const UiColor color, const u8 outline) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type  = UiCmd_Style,
      .style = {
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
