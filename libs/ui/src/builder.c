#include "core_diag.h"
#include "core_math.h"
#include "gap_window.h"
#include "ui_canvas.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "text_internal.h"

static const UiRect  g_ui_defaultRect    = {0, 0, 100, 100};
static const UiColor g_ui_defaultColor   = {255, 255, 255, 255};
static const u8      g_ui_defaultOutline = 0;

typedef struct {
  const UiBuildCtx*    ctx;
  const GapWindowComp* window;
  const AssetFtxComp*  font;
  UiRect               rect;
  UiColor              color;
  u8                   outline;
  UiId                 hoveredId;
} UiBuildState;

static UiDrawData ui_build_drawdata(const UiBuildState* state) {
  return (UiDrawData){
      .glyphsPerDim    = state->font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)state->font->glyphsPerDim,
  };
}

static UiVector ui_resolve_vec(UiBuildState* state, const UiVector vec, const UiUnits units) {
  const GapVector winSize = gap_window_param(state->window, GapParam_WindowSize);
  switch (units) {
  case UiUnits_Current:
    return ui_vector(vec.x * state->rect.width, vec.y * state->rect.height);
  case UiUnits_Absolute:
    return vec;
  case UiUnits_Window:
    return ui_vector(vec.x * winSize.width, vec.y * winSize.height);
  }
  diag_crash();
}

static UiVector ui_resolve_pos(
    UiBuildState* state, const UiVector pos, const UiOrigin origin, const UiUnits units) {
  const GapVector winSize   = gap_window_param(state->window, GapParam_WindowSize);
  const GapVector cursorPos = gap_window_param(state->window, GapParam_CursorPos);
  const UiVector  vec       = ui_resolve_vec(state, pos, units);
  switch (origin) {
  case UiOrigin_Current:
    return ui_vector(state->rect.x + vec.x, state->rect.y + vec.y);
  case UiOrigin_Cursor:
    return ui_vector(cursorPos.x + vec.x, cursorPos.y + vec.y);
  case UiOrigin_WindowBottomLeft:
    return vec;
  case UiOrigin_WindowBottomRight:
    return ui_vector(winSize.width - vec.x, vec.y);
  case UiOrigin_WindowTopLeft:
    return ui_vector(vec.x, winSize.height - vec.y);
  case UiOrigin_WindowTopRight:
    return ui_vector(winSize.width - vec.x, winSize.height - vec.y);
  }
  diag_crash();
}

static UiVector ui_resolve_size_to(
    UiBuildState* state, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  const UiVector toPos = ui_resolve_pos(state, pos, origin, unit);
  return ui_vector(math_max(toPos.x - state->rect.x, 0), math_max(toPos.y - state->rect.y, 0));
}

static void ui_build_text_char(void* userCtx, const UiTextCharInfo* info) {
  UiBuildState* state = userCtx;

  // NOTE: Take the border into account as the glyph will need to be drawn bigger to compensate.
  const f32      border = info->ch->border * info->size;
  const f32      size   = (info->ch->size + info->ch->border * 2.0f) * info->size;
  const UiVector pos    = {
      info->pos.x + info->ch->offsetX * info->size - border,
      info->pos.y + info->ch->offsetY * info->size - border,
  };

  state->ctx->outputGlyph(
      state->ctx->userCtx,
      (UiGlyphData){
          .rect         = {pos, ui_vector(size, size)},
          .color        = info->color,
          .atlasIndex   = info->ch->glyphIndex,
          .borderFrac   = (u16)(border / size * u16_max),
          .cornerFrac   = (u16)(0.5f * u16_max),
          .outlineWidth = info->outline,
      });
}

static bool ui_build_is_hovered(UiBuildState* state) {
  const f32       minX = state->rect.x, minY = state->rect.y;
  const f32       maxX = minX + state->rect.width, maxY = minY + state->rect.height;
  const GapVector cursorPos = gap_window_param(state->window, GapParam_CursorPos);
  return cursorPos.x >= minX && cursorPos.x <= maxX && cursorPos.y >= minY && cursorPos.y <= maxY;
}

static void ui_build_draw_text(UiBuildState* state, const UiDrawText* cmd) {
  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state)) {
    state->hoveredId = cmd->id;
  }
  ui_text_build(
      state->font,
      state->rect,
      cmd->text,
      cmd->fontSize,
      state->color,
      state->outline,
      cmd->align,
      state,
      &ui_build_text_char);
}

static void ui_build_draw_glyph(UiBuildState* state, const UiDrawGlyph* cmd) {
  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state)) {
    state->hoveredId = cmd->id;
  }
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cmd->cp);
  if (sentinel_check(ch->glyphIndex)) {
    return; // No glyph for the given codepoint.
  }
  const f32    halfMinDim = math_min(state->rect.width, state->rect.height) * 0.5f;
  const f32    corner     = cmd->maxCorner ? math_min(cmd->maxCorner, halfMinDim) : halfMinDim;
  const f32    border     = ch->border * corner * 2.0f;
  const UiRect rect       = {
      .pos  = {state->rect.x - border, state->rect.y - border},
      .size = {state->rect.width + border * 2, state->rect.height + border * 2},
  };
  state->ctx->outputGlyph(
      state->ctx->userCtx,
      (UiGlyphData){
          .rect         = rect,
          .color        = state->color,
          .atlasIndex   = ch->glyphIndex,
          .borderFrac   = (u16)(border / rect.size.width * u16_max),
          .cornerFrac   = (u16)((corner + border) / rect.size.width * u16_max),
          .outlineWidth = state->outline,
      });
}

static void ui_build_cmd(UiBuildState* state, const UiCmd* cmd) {
  switch (cmd->type) {
  case UiCmd_RectMove:
    state->rect.pos =
        ui_resolve_pos(state, cmd->rectMove.pos, cmd->rectMove.origin, cmd->rectMove.unit);
    break;
  case UiCmd_RectResize:
    state->rect.size = ui_resolve_vec(state, cmd->rectResize.size, cmd->rectResize.unit);
    break;
  case UiCmd_RectResizeTo:
    state->rect.size = ui_resolve_size_to(
        state, cmd->rectResizeTo.pos, cmd->rectResizeTo.origin, cmd->rectResizeTo.unit);
    break;
  case UiCmd_Style:
    state->color   = cmd->style.color;
    state->outline = cmd->style.outline;
    break;
  case UiCmd_DrawText:
    ui_build_draw_text(state, &cmd->drawText);
    break;
  case UiCmd_DrawGlyph:
    ui_build_draw_glyph(state, &cmd->drawGlyph);
    break;
  }
}

UiBuildResult ui_build(const UiCmdBuffer* cmdBuffer, const UiBuildCtx* ctx) {

  UiBuildState state = {
      .ctx       = ctx,
      .window    = ctx->window,
      .font      = ctx->font,
      .rect      = g_ui_defaultRect,
      .color     = g_ui_defaultColor,
      .outline   = g_ui_defaultOutline,
      .hoveredId = sentinel_u64,
  };
  ctx->outputDraw(ctx->userCtx, ui_build_drawdata(&state));

  UiCmd* cmd = null;
  while ((cmd = ui_cmd_next(cmdBuffer, cmd))) {
    ui_build_cmd(&state, cmd);
  }

  return (UiBuildResult){
      .hoveredId = state.hoveredId,
  };
}
