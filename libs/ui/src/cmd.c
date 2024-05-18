#include "core_dynarray.h"
#include "log_logger.h"

#include "cmd_internal.h"

#define ui_cmdbuffer_transient_chunk_size (32 * usize_kibibyte)
#define ui_cmdbuffer_max_text_size (8 * usize_kibibyte)

ASSERT(ui_cmdbuffer_max_text_size < u16_max, "Text size needs to be storable in a u16");

struct sUiCmdBuffer {
  DynArray   commands; // UiCmd[]
  Allocator* alloc;
  Allocator* allocTransient;
};

INLINE_HINT static UiCmd* ui_cmdbuffer_push(UiCmdBuffer* buffer) {
  return (UiCmd*)dynarray_push(&buffer->commands, 1).ptr;
}

UiCmdBuffer* ui_cmdbuffer_create(Allocator* alloc) {
  UiCmdBuffer* buffer = alloc_alloc_t(alloc, UiCmdBuffer);

  Allocator* allocTransient =
      alloc_chunked_create(g_allocHeap, alloc_bump_create, ui_cmdbuffer_transient_chunk_size);

  *buffer = (UiCmdBuffer){
      .commands       = dynarray_create_t(alloc, UiCmd, 128),
      .alloc          = alloc,
      .allocTransient = allocTransient,
  };
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

u32 ui_cmdbuffer_count(const UiCmdBuffer* buffer) { return (u32)buffer->commands.size; }

void ui_cmd_push_rect_push(UiCmdBuffer* buffer) {
  UiCmd* cmd = ui_cmdbuffer_push(buffer);
  cmd->type  = UiCmd_RectPush;
}

void ui_cmd_push_rect_pop(UiCmdBuffer* buffer) {
  UiCmd* cmd = ui_cmdbuffer_push(buffer);
  cmd->type  = UiCmd_RectPop;
}

void ui_cmd_push_rect_pos(
    UiCmdBuffer*   buffer,
    const UiBase   origin,
    const UiVector offset,
    const UiBase   units,
    const UiAxis   axis) {
  UiCmd* cmd   = ui_cmdbuffer_push(buffer);
  cmd->type    = UiCmd_RectPos;
  cmd->rectPos = (UiRectPos){
      .origin = origin,
      .offset = offset,
      .units  = units,
      .axis   = axis,
  };
}

void ui_cmd_push_rect_size(
    UiCmdBuffer* buffer, const UiVector size, const UiBase units, const UiAxis axis) {
  UiCmd* cmd    = ui_cmdbuffer_push(buffer);
  cmd->type     = UiCmd_RectSize;
  cmd->rectSize = (UiRectSize){
      .size  = size,
      .units = units,
      .axis  = axis,
  };
}

void ui_cmd_push_rect_size_to(
    UiCmdBuffer*   buffer,
    const UiBase   origin,
    const UiVector offset,
    const UiBase   units,
    const UiAxis   axis) {
  UiCmd* cmd      = ui_cmdbuffer_push(buffer);
  cmd->type       = UiCmd_RectSizeTo;
  cmd->rectSizeTo = (UiRectSizeTo){
      .origin = origin,
      .offset = offset,
      .units  = units,
      .axis   = axis,
  };
}

void ui_cmd_push_rect_size_grow(
    UiCmdBuffer* buffer, const UiVector delta, const UiBase units, const UiAxis axis) {
  UiCmd* cmd        = ui_cmdbuffer_push(buffer);
  cmd->type         = UiCmd_RectSizeGrow;
  cmd->rectSizeGrow = (UiRectSizeGrow){
      .delta = delta,
      .units = units,
      .axis  = axis,
  };
}

void ui_cmd_push_container_push(UiCmdBuffer* buffer, const UiClip clip) {
  UiCmd* cmd         = ui_cmdbuffer_push(buffer);
  cmd->type          = UiCmd_ContainerPush;
  cmd->containerPush = (UiContainerPush){
      .clip = clip,
  };
}

void ui_cmd_push_container_pop(UiCmdBuffer* buffer) {
  UiCmd* cmd = ui_cmdbuffer_push(buffer);
  cmd->type  = UiCmd_ContainerPop;
}

void ui_cmd_push_style_push(UiCmdBuffer* buffer) {
  UiCmd* cmd = ui_cmdbuffer_push(buffer);
  cmd->type  = UiCmd_StylePush;
}

void ui_cmd_push_style_pop(UiCmdBuffer* buffer) {
  UiCmd* cmd = ui_cmdbuffer_push(buffer);
  cmd->type  = UiCmd_StylePop;
}

void ui_cmd_push_style_color(UiCmdBuffer* buffer, const UiColor color) {
  UiCmd* cmd      = ui_cmdbuffer_push(buffer);
  cmd->type       = UiCmd_StyleColor;
  cmd->styleColor = (UiStyleColor){
      .value = color,
  };
}

void ui_cmd_push_style_color_mult(UiCmdBuffer* buffer, const f32 value) {
  UiCmd* cmd          = ui_cmdbuffer_push(buffer);
  cmd->type           = UiCmd_StyleColorMult;
  cmd->styleColorMult = (UiStyleColorMult){
      .value = value,
  };
}

void ui_cmd_push_style_outline(UiCmdBuffer* buffer, const u8 outline) {
  UiCmd* cmd        = ui_cmdbuffer_push(buffer);
  cmd->type         = UiCmd_StyleOutline;
  cmd->styleOutline = (UiStyleOutline){
      .value = outline,
  };
}

void ui_cmd_push_style_layer(UiCmdBuffer* buffer, const UiLayer layer) {
  UiCmd* cmd      = ui_cmdbuffer_push(buffer);
  cmd->type       = UiCmd_StyleLayer;
  cmd->styleLayer = (UiStyleLayer){
      .value = layer,
  };
}

void ui_cmd_push_style_variation(UiCmdBuffer* buffer, const u8 variation) {
  UiCmd* cmd          = ui_cmdbuffer_push(buffer);
  cmd->type           = UiCmd_StyleVariation;
  cmd->styleVariation = (UiStyleVariation){
      .value = variation,
  };
}

void ui_cmd_push_style_weight(UiCmdBuffer* buffer, const UiWeight weight) {
  UiCmd* cmd       = ui_cmdbuffer_push(buffer);
  cmd->type        = UiCmd_StyleWeight;
  cmd->styleWeight = (UiStyleWeight){
      .value = weight,
  };
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
  const String textCopy = string_maybe_dup(buffer->allocTransient, text);
  if (UNLIKELY(text.size && !mem_valid(textCopy))) {
    // TODO: Report error.
    return; // Transient allocator ran out of space.
  }
  UiCmd* cmd    = ui_cmdbuffer_push(buffer);
  cmd->type     = UiCmd_DrawText;
  cmd->drawText = (UiDrawText){
      .id       = id,
      .textPtr  = textCopy.ptr,
      .textSize = (u16)textCopy.size,
      .fontSize = fontSize,
      .align    = align,
      .flags    = flags,
  };
}

void ui_cmd_push_draw_glyph(
    UiCmdBuffer*  buffer,
    const UiId    id,
    const Unicode cp,
    const u16     maxCorner,
    const f32     angleRad,
    const UiFlags flags) {
  UiCmd* cmd     = ui_cmdbuffer_push(buffer);
  cmd->type      = UiCmd_DrawGlyph;
  cmd->drawGlyph = (UiDrawGlyph){
      .id        = id,
      .cp        = cp,
      .angleRad  = angleRad,
      .maxCorner = maxCorner,
      .flags     = flags,
  };
}

void ui_cmd_push_draw_image(
    UiCmdBuffer*     buffer,
    const UiId       id,
    const StringHash img,
    const u16        maxCorner,
    const f32        angleRad,
    const UiFlags    flags) {
  UiCmd* cmd     = ui_cmdbuffer_push(buffer);
  cmd->type      = UiCmd_DrawImage;
  cmd->drawImage = (UiDrawImage){
      .id        = id,
      .img       = img,
      .angleRad  = angleRad,
      .maxCorner = maxCorner,
      .flags     = flags,
  };
}

UiCmd* ui_cmd_next(const UiCmdBuffer* buffer, UiCmd* prev) {
  if (!prev) {
    return buffer->commands.size ? dynarray_begin_t(&buffer->commands, UiCmd) : null;
  }
  UiCmd* next = ++prev;
  return next == dynarray_end_t(&buffer->commands, UiCmd) ? null : next;
}
