#include "core_diag.h"
#include "core_math.h"
#include "gap_window.h"
#include "ui_canvas.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "text_internal.h"

#define ui_build_rect_stack_max 10
#define ui_build_style_stack_max 10
#define ui_build_container_stack_max 10

static const UiRect  g_ui_defaultRect    = {0, 0, 100, 100};
static const UiColor g_ui_defaultColor   = {255, 255, 255, 255};
static const u8      g_ui_defaultOutline = 2;

typedef struct {
  UiColor color;
  u8      outline;
  UiLayer layer;
} UiBuildStyle;

typedef struct {
  const UiBuildCtx*    ctx;
  const GapWindowComp* window;
  const AssetFtxComp*  font;
  UiRect               rectStack[ui_build_rect_stack_max];
  u32                  rectStackCount;
  UiBuildStyle         styleStack[ui_build_style_stack_max];
  u32                  styleStackCount;
  UiRect               containerStack[ui_build_container_stack_max];
  u32                  containerStackCount;
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

static UiRect* ui_build_container_currect(UiBuildState* state) {
  diag_assert(state->containerStackCount);
  return &state->containerStack[state->containerStackCount - 1];
}

static UiDrawData ui_build_drawdata(const UiBuildState* state) {
  return (UiDrawData){
      .glyphsPerDim    = state->font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)state->font->glyphsPerDim,
  };
}

static UiVector ui_resolve_vec(UiBuildState* state, const UiVector vec, const UiUnits units) {
  const GapVector winSize = gap_window_param(state->window, GapParam_WindowSize);
  switch (units) {
  case UiUnits_Current: {
    const UiRect currentRect = *ui_build_rect_currect(state);
    return ui_vector(vec.x * currentRect.width, vec.y * currentRect.height);
  }
  case UiUnits_Container: {
    const UiRect currentContainer = *ui_build_container_currect(state);
    return ui_vector(vec.x * currentContainer.width, vec.y * currentContainer.height);
  }
  case UiUnits_Absolute:
    return vec;
  case UiUnits_Window:
    return ui_vector(vec.x * winSize.width, vec.y * winSize.height);
  }
  diag_crash();
}

static UiVector ui_resolve_pos(
    UiBuildState* state, const UiVector pos, const UiOrigin origin, const UiUnits units) {
  const GapVector winSize = gap_window_param(state->window, GapParam_WindowSize);
  const UiVector  vec     = ui_resolve_vec(state, pos, units);
  switch (origin) {
  case UiOrigin_Current: {
    const UiRect currentRect = *ui_build_rect_currect(state);
    return ui_vector(currentRect.x + vec.x, currentRect.y + vec.y);
  }
  case UiOrigin_Container: {
    const UiRect containerRect = *ui_build_container_currect(state);
    return ui_vector(containerRect.x + vec.x, containerRect.y + vec.y);
  }
  case UiOrigin_Cursor: {
    const GapVector cursorPos = gap_window_param(state->window, GapParam_CursorPos);
    return ui_vector(cursorPos.x + vec.x, cursorPos.y + vec.y);
  }
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
      },
      info->layer);
}

static bool ui_build_is_hovered(UiBuildState* state, const UiRect rect) {
  const f32 minX = rect.x, minY = rect.y;
  const f32 maxX = minX + rect.width, maxY = minY + rect.height;

  const GapVector cursorPos = gap_window_param(state->window, GapParam_CursorPos);
  return cursorPos.x >= minX && cursorPos.x <= maxX && cursorPos.y >= minY && cursorPos.y <= maxY;
}

static void ui_build_draw_text(UiBuildState* state, const UiDrawText* cmd) {
  const UiRect       currentRect  = *ui_build_rect_currect(state);
  const UiBuildStyle currentStyle = *ui_build_style_currect(state);

  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state, currentRect)) {
    state->hoveredId = cmd->id;
  }

  const UiTextBuildResult result = ui_text_build(
      state->font,
      currentRect,
      cmd->text,
      cmd->fontSize,
      currentStyle.color,
      currentStyle.outline,
      currentStyle.layer,
      cmd->align,
      state,
      &ui_build_text_char);

  state->ctx->outputRect(state->ctx->userCtx, cmd->id, result.rect);
}

static void ui_build_draw_glyph(UiBuildState* state, const UiDrawGlyph* cmd) {
  const UiRect       currentRect  = *ui_build_rect_currect(state);
  const UiBuildStyle currentStyle = *ui_build_style_currect(state);

  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state, currentRect)) {
    state->hoveredId = cmd->id;
  }
  state->ctx->outputRect(state->ctx->userCtx, cmd->id, currentRect);

  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cmd->cp);
  if (sentinel_check(ch->glyphIndex)) {
    return; // No glyph for the given codepoint.
  }
  const f32    halfMinDim = math_min(currentRect.width, currentRect.height) * 0.5f;
  const f32    corner     = cmd->maxCorner ? math_min(cmd->maxCorner, halfMinDim) : halfMinDim;
  const f32    border     = ch->border * corner * 2.0f;
  const UiRect rect       = {
      .pos  = {currentRect.x - border, currentRect.y - border},
      .size = {currentRect.width + border * 2, currentRect.height + border * 2},
  };
  state->ctx->outputGlyph(
      state->ctx->userCtx,
      (UiGlyphData){
          .rect         = rect,
          .color        = currentStyle.color,
          .atlasIndex   = ch->glyphIndex,
          .borderFrac   = (u16)(border / rect.size.width * u16_max),
          .cornerFrac   = (u16)((corner + border) / rect.size.width * u16_max),
          .outlineWidth = currentStyle.outline,
      },
      currentStyle.layer);
}

static void ui_build_cmd(UiBuildState* state, const UiCmd* cmd) {
  switch (cmd->type) {
  case UiCmd_RectPush:
    diag_assert(state->rectStackCount < ui_build_rect_stack_max);
    state->rectStack[state->rectStackCount] = state->rectStack[state->rectStackCount - 1];
    ++state->rectStackCount;
    break;
  case UiCmd_RectPop:
    diag_assert(state->rectStackCount > 1);
    --state->rectStackCount;
    break;
  case UiCmd_RectPos:
    ui_build_set_pos(
        state,
        ui_resolve_pos(state, cmd->rectPos.pos, cmd->rectPos.origin, cmd->rectPos.unit),
        cmd->rectPos.axis);
    break;
  case UiCmd_RectSize:
    ui_build_set_size(
        state, ui_resolve_vec(state, cmd->rectSize.size, cmd->rectSize.unit), cmd->rectSize.axis);
    break;
  case UiCmd_RectSizeTo:
    ui_build_set_size(
        state,
        ui_resolve_size_to(
            state, cmd->rectSizeTo.pos, cmd->rectSizeTo.origin, cmd->rectSizeTo.unit),
        cmd->rectSizeTo.axis);
    break;
  case UiCmd_ContainerPush:
    diag_assert(state->containerStackCount < ui_build_container_stack_max);
    state->containerStack[state->containerStackCount] = *ui_build_rect_currect(state);
    ++state->containerStackCount;
    break;
  case UiCmd_ContainerPop:
    diag_assert(state->containerStackCount > 1);
    --state->containerStackCount;
    break;
  case UiCmd_StylePush:
    diag_assert(state->styleStackCount < ui_build_style_stack_max);
    state->styleStack[state->styleStackCount] = state->styleStack[state->styleStackCount - 1];
    ++state->styleStackCount;
    break;
  case UiCmd_StylePop:
    diag_assert(state->styleStackCount > 1);
    --state->styleStackCount;
    break;
  case UiCmd_StyleColor:
    ui_build_style_currect(state)->color = cmd->styleColor.value;
    break;
  case UiCmd_StyleOutline:
    ui_build_style_currect(state)->outline = cmd->styleOutline.value;
    break;
  case UiCmd_StyleLayer:
    ui_build_style_currect(state)->layer = cmd->styleLayer.value;
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
  const GapVector winSize = gap_window_param(ctx->window, GapParam_WindowSize);
  UiBuildState    state   = {
      .ctx                 = ctx,
      .window              = ctx->window,
      .font                = ctx->font,
      .rectStack[0]        = g_ui_defaultRect,
      .rectStackCount      = 1,
      .styleStack[0]       = {g_ui_defaultColor, g_ui_defaultOutline},
      .styleStackCount     = 1,
      .containerStack[0]   = {.size = {winSize.width, winSize.height}},
      .containerStackCount = 1,
      .hoveredId           = sentinel_u64,
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
