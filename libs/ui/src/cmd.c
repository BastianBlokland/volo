#include "core_dynarray.h"
#include "log_logger.h"

#include "cmd_internal.h"

#define ui_cmdbuffer_transient_chunk_size (8 * usize_kibibyte)
#define ui_cmdbuffer_max_text_size (8 * usize_kibibyte)

struct sUiCmdBuffer {
  DynArray   commands; // UiCmd[]
  Allocator* alloc;
  Allocator* allocTransient;
};

UiCmdBuffer* ui_cmdbuffer_create(Allocator* alloc) {
  UiCmdBuffer* buffer = alloc_alloc_t(alloc, UiCmdBuffer);
  *buffer             = (UiCmdBuffer){
      .commands = dynarray_create_t(alloc, UiCmd, 128),
      .alloc    = alloc,
      .allocTransient =
          alloc_chunked_create(g_alloc_page, alloc_bump_create, ui_cmdbuffer_transient_chunk_size)};
  return buffer;
}

void ui_cmdbuffer_destroy(UiCmdBuffer* buffer) {
  dynarray_destroy(&buffer->commands);
  alloc_chunked_destroy(buffer->allocTransient);
  alloc_free_t(buffer->alloc, buffer);
}

void ui_cmdbuffer_clear(UiCmdBuffer* buffer) {
  dynarray_clear(&buffer->commands);
  alloc_reset(buffer->allocTransient);
}

void ui_cmd_push_rect_move(
    UiCmdBuffer* buffer, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type     = UiCmd_RectMove,
      .rectMove = {
          .pos    = pos,
          .origin = origin,
          .unit   = unit,
      }};
}

void ui_cmd_push_rect_resize(UiCmdBuffer* buffer, const UiVector size, const UiUnits unit) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type       = UiCmd_RectResize,
      .rectResize = {
          .size = size,
          .unit = unit,
      }};
}

void ui_cmd_push_rect_resize_to(
    UiCmdBuffer* buffer, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type         = UiCmd_RectResizeTo,
      .rectResizeTo = {
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

void ui_cmd_push_draw_text(
    UiCmdBuffer*      buffer,
    const UiId        id,
    const String      text,
    const u16         fontSize,
    const UiTextAlign align,
    const UiFlags     flags) {

  if (UNLIKELY(text.size > ui_cmdbuffer_max_text_size)) {
    log_e(
        "Ui text size exceeds maximum",
        log_param("size", fmt_size(text.size)),
        log_param("limit", fmt_size(ui_cmdbuffer_max_text_size)));
    return;
  }
  // TODO: Report error when the transient allocator runs out of space.
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type     = UiCmd_DrawText,
      .drawText = {
          .id       = id,
          .text     = string_dup(buffer->allocTransient, text),
          .fontSize = fontSize,
          .align    = align,
          .flags    = flags,
      }};
}

void ui_cmd_push_draw_glyph(
    UiCmdBuffer*  buffer,
    const UiId    id,
    const Unicode cp,
    const u16     maxCorner,
    const UiFlags flags) {

  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type      = UiCmd_DrawGlyph,
      .drawGlyph = {
          .id        = id,
          .cp        = cp,
          .maxCorner = maxCorner,
          .flags     = flags,
      }};
}

UiCmd* ui_cmd_next(const UiCmdBuffer* buffer, UiCmd* prev) {
  if (!prev) {
    return dynarray_begin_t(&buffer->commands, UiCmd);
  }
  UiCmd* next = ++prev;
  return next == dynarray_end_t(&buffer->commands, UiCmd) ? null : next;
}
