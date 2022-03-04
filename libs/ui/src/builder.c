#include "core_diag.h"
#include "core_math.h"
#include "gap_window.h"
#include "ui_canvas.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "text_internal.h"

#define ui_build_rect_stack_max 10
#define ui_build_style_stack_max 10

static const UiRect  g_ui_defaultRect    = {0, 0, 100, 100};
static const UiColor g_ui_defaultColor   = {255, 255, 255, 255};
static const u8      g_ui_defaultOutline = 2;

typedef struct {
  UiColor color;
  u8      outline;
} UiBuildStyle;

typedef struct {
  const UiBuildCtx*    ctx;
  const GapWindowComp* window;
  const AssetFtxComp*  font;
  UiRect               rectStack[ui_build_rect_stack_max];
  u32                  rectStackCount;
  UiBuildStyle         styleStack[ui_build_style_stack_max];
  u32                  styleStackCount;
  UiId                 hoveredId;
} UiBuildState;

static UiRect* ui_build_rect_currect(UiBuildState* state) {
  diag_assert(state->rectStackCount);
  return &state->rectStack[state->rectStackCount - 1];
}

static UiBuildStyle* ui_build_style_currect(UiBuildState* state) {
  diag_assert(state->rectStackCount);
  return &state->styleStack[state->styleStackCount - 1];
}

static UiDrawData ui_build_drawdata(const UiBuildState* state) {
  return (UiDrawData){
      .glyphsPerDim    = state->font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)state->font->glyphsPerDim,
  };
}

static UiVector ui_resolve_vec(UiBuildState* state, const UiVector vec, const UiUnits units) {
  const GapVector winSize     = gap_window_param(state->window, GapParam_WindowSize);
  const UiRect    currentRect = *ui_build_rect_currect(state);
  switch (units) {
  case UiUnits_Current:
    return ui_vector(vec.x * currentRect.width, vec.y * currentRect.height);
  case UiUnits_Absolute:
    return vec;
  case UiUnits_Window:
    return ui_vector(vec.x * winSize.width, vec.y * winSize.height);
  }
  diag_crash();
}

static UiVector ui_resolve_pos(
    UiBuildState* state, const UiVector pos, const UiOrigin origin, const UiUnits units) {
  const UiRect    currentRect = *ui_build_rect_currect(state);
  const GapVector winSize     = gap_window_param(state->window, GapParam_WindowSize);
  const GapVector cursorPos   = gap_window_param(state->window, GapParam_CursorPos);
  const UiVector  vec         = ui_resolve_vec(state, pos, units);
  switch (origin) {
  case UiOrigin_Current:
    return ui_vector(currentRect.x + vec.x, currentRect.y + vec.y);
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
  const UiRect   currentRect = *ui_build_rect_currect(state);
  const UiVector toPos       = ui_resolve_pos(state, pos, origin, unit);
  return ui_vector(math_max(toPos.x - currentRect.x, 0), math_max(toPos.y - currentRect.y, 0));
}

static void ui_build_set_pos(UiBuildState* state, const UiVector val, const UiAxis axis) {
  if (axis & Ui_X) {
    ui_build_rect_currect(state)->pos.x = val.x;
  }
  if (axis & Ui_Y) {
    ui_build_rect_currect(state)->pos.y = val.y;
  }
}

static void ui_build_set_size(UiBuildState* state, const UiVector val, const UiAxis axis) {
  if (axis & Ui_X) {
    ui_build_rect_currect(state)->size.x = val.x;
  }
  if (axis & Ui_Y) {
    ui_build_rect_currect(state)->size.y = val.y;
  }
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
  const UiRect    currentRect = *ui_build_rect_currect(state);
  const f32       minX = currentRect.x, minY = currentRect.y;
  const f32       maxX = minX + currentRect.width, maxY = minY + currentRect.height;
  const GapVector cursorPos = gap_window_param(state->window, GapParam_CursorPos);
  return cursorPos.x >= minX && cursorPos.x <= maxX && cursorPos.y >= minY && cursorPos.y <= maxY;
}

static void ui_build_draw_text(UiBuildState* state, const UiDrawText* cmd) {
  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state)) {
    state->hoveredId = cmd->id;
  }
  ui_text_build(
      state->font,
      *ui_build_rect_currect(state),
      cmd->text,
      cmd->fontSize,
      ui_build_style_currect(state)->color,
      ui_build_style_currect(state)->outline,
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
  const UiRect currentRect = *ui_build_rect_currect(state);
  const f32    halfMinDim  = math_min(currentRect.width, currentRect.height) * 0.5f;
  const f32    corner      = cmd->maxCorner ? math_min(cmd->maxCorner, halfMinDim) : halfMinDim;
  const f32    border      = ch->border * corner * 2.0f;
  const UiRect rect        = {
      .pos  = {currentRect.x - border, currentRect.y - border},
      .size = {currentRect.width + border * 2, currentRect.height + border * 2},
  };
  state->ctx->outputGlyph(
      state->ctx->userCtx,
      (UiGlyphData){
          .rect         = rect,
          .color        = ui_build_style_currect(state)->color,
          .atlasIndex   = ch->glyphIndex,
          .borderFrac   = (u16)(border / rect.size.width * u16_max),
          .cornerFrac   = (u16)((corner + border) / rect.size.width * u16_max),
          .outlineWidth = ui_build_style_currect(state)->outline,
      });
}

static void ui_build_cmd(UiBuildState* state, const UiCmd* cmd) {
  switch (cmd->type) {
  case UiCmd_RectPush:
    diag_assert(state->rectStackCount < ui_build_rect_stack_max);
    state->rectStack[state->rectStackCount] = state->rectStack[state->rectStackCount - 1];
    ++state->rectStackCount;
    break;
  case UiCmd_RectPop:
    diag_assert(state->rectStackCount);
    --state->rectStackCount;
    break;
  case UiCmd_RectMove:
    ui_build_set_pos(
        state,
        ui_resolve_pos(state, cmd->rectMove.pos, cmd->rectMove.origin, cmd->rectMove.unit),
        cmd->rectMove.axis);
    break;
  case UiCmd_RectResize:
    ui_build_set_size(
        state,
        ui_resolve_vec(state, cmd->rectResize.size, cmd->rectResize.unit),
        cmd->rectResize.axis);
    break;
  case UiCmd_RectResizeTo:
    ui_build_set_size(
        state,
        ui_resolve_size_to(
            state, cmd->rectResizeTo.pos, cmd->rectResizeTo.origin, cmd->rectResizeTo.unit),
        cmd->rectResizeTo.axis);
    break;
  case UiCmd_StylePush:
    diag_assert(state->styleStackCount < ui_build_style_stack_max);
    state->styleStack[state->styleStackCount] = state->styleStack[state->styleStackCount - 1];
    ++state->styleStackCount;
    break;
  case UiCmd_StylePop:
    diag_assert(state->styleStackCount);
    --state->styleStackCount;
    break;
  case UiCmd_StyleColor:
    ui_build_style_currect(state)->color = cmd->styleColor.value;
    break;
  case UiCmd_StyleOutline:
    ui_build_style_currect(state)->outline = cmd->styleOutline.value;
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
      .ctx             = ctx,
      .window          = ctx->window,
      .font            = ctx->font,
      .rectStack[0]    = g_ui_defaultRect,
      .rectStackCount  = 1,
      .styleStack[0]   = {g_ui_defaultColor, g_ui_defaultOutline},
      .styleStackCount = 1,
      .hoveredId       = sentinel_u64,
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
