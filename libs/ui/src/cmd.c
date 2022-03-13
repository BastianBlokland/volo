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

void ui_cmd_push_rect_push(UiCmdBuffer* buffer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_RectPush};
}

void ui_cmd_push_rect_pop(UiCmdBuffer* buffer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_RectPop};
}

void ui_cmd_push_rect_pos(
    UiCmdBuffer*   buffer,
    const UiBase   origin,
    const UiVector offset,
    const UiBase   units,
    const UiAxis   axis) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type    = UiCmd_RectPos,
      .rectPos = {
          .origin = origin,
          .offset = offset,
          .units  = units,
          .axis   = axis,
      }};
}

void ui_cmd_push_rect_size(
    UiCmdBuffer* buffer, const UiVector size, const UiBase units, const UiAxis axis) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type     = UiCmd_RectSize,
      .rectSize = {
          .size  = size,
          .units = units,
          .axis  = axis,
      }};
}

void ui_cmd_push_rect_size_grow(
    UiCmdBuffer* buffer, const UiVector delta, const UiBase units, const UiAxis axis) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type         = UiCmd_RectSizeGrow,
      .rectSizeGrow = {
          .delta = delta,
          .units = units,
          .axis  = axis,
      }};
}

void ui_cmd_push_container_push(UiCmdBuffer* buffer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_ContainerPush};
}

void ui_cmd_push_container_pop(UiCmdBuffer* buffer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_ContainerPop};
}

void ui_cmd_push_style_push(UiCmdBuffer* buffer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_StylePush};
}

void ui_cmd_push_style_pop(UiCmdBuffer* buffer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){.type = UiCmd_StylePop};
}

void ui_cmd_push_style_color(UiCmdBuffer* buffer, const UiColor color) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type       = UiCmd_StyleColor,
      .styleColor = {
          .value = color,
      }};
}

void ui_cmd_push_style_outline(UiCmdBuffer* buffer, const u8 outline) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type         = UiCmd_StyleOutline,
      .styleOutline = {
          .value = outline,
      }};
}

void ui_cmd_push_style_layer(UiCmdBuffer* buffer, const UiLayer layer) {
  *dynarray_push_t(&buffer->commands, UiCmd) = (UiCmd){
      .type       = UiCmd_StyleLayer,
      .styleLayer = {
          .value = layer,
      }};
}

void ui_cmd_push_draw_text(
    UiCmdBuffer*  buffer,
    const UiId    id,
    const String  text,
    const u16     fontSize,
    const UiAlign align,
    const UiFlags flags) {

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
    return buffer->commands.size ? dynarray_begin_t(&buffer->commands, UiCmd) : null;
  }
  UiCmd* next = ++prev;
  return next == dynarray_end_t(&buffer->commands, UiCmd) ? null : next;
}
