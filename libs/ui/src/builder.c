#include "core_diag.h"
#include "core_math.h"
#include "ui_canvas.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "text_internal.h"

#define ui_build_rect_stack_max 5
#define ui_build_style_stack_max 5
#define ui_build_container_stack_max 5

static const UiRect  g_ui_defaultRect    = {0, 0, 100, 100};
static const UiColor g_ui_defaultColor   = {255, 255, 255, 255};
static const u8      g_ui_defaultOutline = 1;

typedef struct {
  UiColor color;
  u8      outline;
  UiLayer layer;
  u8      variation;
} UiBuildStyle;

typedef struct {
  UiRect rect;
  u8     clipId;
} UiBuildContainer;

typedef struct {
  const UiBuildCtx*   ctx;
  const AssetFtxComp* font;
  UiRect              rectStack[ui_build_rect_stack_max];
  u32                 rectStackCount;
  UiBuildStyle        styleStack[ui_build_style_stack_max];
  u32                 styleStackCount;
  UiBuildContainer    containerStack[ui_build_container_stack_max];
  u32                 containerStackCount;
  UiId                hoveredId;
} UiBuildState;

static UiRect* ui_build_rect_currect(UiBuildState* state) {
  diag_assert(state->rectStackCount);
  return &state->rectStack[state->rectStackCount - 1];
}

static UiBuildStyle* ui_build_style_currect(UiBuildState* state) {
  diag_assert(state->rectStackCount);
  return &state->styleStack[state->styleStackCount - 1];
}

static UiBuildContainer* ui_build_container_currect(UiBuildState* state) {
  diag_assert(state->containerStackCount);
  return &state->containerStack[state->containerStackCount - 1];
}

static UiBuildContainer* ui_build_container_active(UiBuildState* state) {
  const UiBuildStyle style = *ui_build_style_currect(state);
  return style.layer == UiLayer_Overlay ? &state->containerStack[0]
                                        : ui_build_container_currect(state);
}

static UiVector ui_resolve_vec(UiBuildState* state, const UiVector vec, const UiBase units) {
  switch (units) {
  case UiBase_Absolute:
    return vec;
  case UiBase_Current: {
    const UiRect currentRect = *ui_build_rect_currect(state);
    return ui_vector(vec.x * currentRect.width, vec.y * currentRect.height);
  }
  case UiBase_Container: {
    const UiBuildContainer container = *ui_build_container_currect(state);
    return ui_vector(vec.x * container.rect.width, vec.y * container.rect.height);
  }
  case UiBase_Window: {
    return ui_vector(vec.x * state->ctx->canvasRes.width, vec.y * state->ctx->canvasRes.height);
  }
  case UiBase_Cursor:
    return ui_vector(0, 0);
  }
  diag_crash();
}

static UiVector ui_resolve_origin(UiBuildState* state, const UiBase origin) {
  switch (origin) {
  case UiBase_Absolute:
    return ui_vector(0, 0);
  case UiBase_Current: {
    const UiRect currentRect = *ui_build_rect_currect(state);
    return ui_vector(currentRect.x, currentRect.y);
  }
  case UiBase_Container: {
    const UiBuildContainer container = *ui_build_container_currect(state);
    return ui_vector(container.rect.x, container.rect.y);
  }
  case UiBase_Window:
    return ui_vector(0, 0);
  case UiBase_Cursor: {
    return state->ctx->inputPos;
  }
  }
  diag_crash();
}

static UiVector ui_resolve_pos(
    UiBuildState* state, const UiBase origin, const UiVector offset, const UiBase units) {
  const UiVector originVec = ui_resolve_origin(state, origin);
  const UiVector offsetVec = ui_resolve_vec(state, offset, units);
  return ui_vector(originVec.x + offsetVec.x, originVec.y + offsetVec.y);
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
  const u8 clipId = info->layer == UiLayer_Overlay ? 0 : ui_build_container_currect(state)->clipId;

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
          .clipId       = clipId,
          .outlineWidth = info->outline,
      },
      info->layer);
}

static bool ui_rect_contains(const UiRect rect, const UiVector point) {
  const f32 minX = rect.x, minY = rect.y;
  const f32 maxX = minX + rect.width, maxY = minY + rect.height;
  return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

static bool ui_rect_intersect(const UiRect a, const UiRect b, const f32 padding) {
  return a.x + a.width > b.x - padding && b.x + b.width > a.x - padding &&
         a.y + a.height > b.y - padding && b.y + b.height > a.y - padding;
}

static bool
ui_build_cull(const UiBuildContainer container, const UiRect rect, const UiBuildStyle style) {
  return !ui_rect_intersect(container.rect, rect, style.outline);
}

static bool
ui_build_is_hovered(UiBuildState* state, const UiBuildContainer container, const UiRect rect) {
  return ui_rect_contains(rect, state->ctx->inputPos) &&
         ui_rect_contains(container.rect, state->ctx->inputPos);
}

static void ui_build_draw_text(UiBuildState* state, const UiDrawText* cmd) {
  const UiRect           layoutRect = *ui_build_rect_currect(state);
  const UiBuildStyle     style      = *ui_build_style_currect(state);
  const UiBuildContainer container  = *ui_build_container_active(state);

  if (ui_build_cull(container, layoutRect, style)) {
    return;
  }
  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state, container, layoutRect)) {
    state->hoveredId = cmd->id;
  }

  const UiTextBuildResult result = ui_text_build(
      state->font,
      layoutRect,
      cmd->text,
      cmd->fontSize,
      style.color,
      style.outline,
      style.layer,
      style.variation,
      cmd->align,
      state,
      &ui_build_text_char);

  state->ctx->outputRect(state->ctx->userCtx, cmd->id, result.rect);
}

static void ui_build_draw_glyph(UiBuildState* state, const UiDrawGlyph* cmd) {
  const UiRect           layoutRect = *ui_build_rect_currect(state);
  const UiBuildStyle     style      = *ui_build_style_currect(state);
  const UiBuildContainer container  = *ui_build_container_active(state);

  if (ui_build_cull(container, layoutRect, style)) {
    return;
  }
  if (cmd->flags & UiFlags_Interactable && ui_build_is_hovered(state, container, layoutRect)) {
    state->hoveredId = cmd->id;
  }
  state->ctx->outputRect(state->ctx->userCtx, cmd->id, layoutRect);

  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cmd->cp, style.variation);
  if (sentinel_check(ch->glyphIndex)) {
    return; // No glyph for the given codepoint.
  }
  const f32    halfMinDim = math_min(layoutRect.width, layoutRect.height) * 0.5f;
  const f32    corner     = cmd->maxCorner ? math_min(cmd->maxCorner, halfMinDim) : halfMinDim;
  const f32    border     = ch->border * corner * 2.0f;
  const UiRect rect       = {
      .pos  = {layoutRect.x - border, layoutRect.y - border},
      .size = {layoutRect.width + border * 2, layoutRect.height + border * 2},
  };
  state->ctx->outputGlyph(
      state->ctx->userCtx,
      (UiGlyphData){
          .rect         = rect,
          .color        = style.color,
          .atlasIndex   = ch->glyphIndex,
          .borderFrac   = (u16)(border / rect.size.width * u16_max),
          .cornerFrac   = (u16)((corner + border) / rect.size.width * u16_max),
          .clipId       = container.clipId,
          .outlineWidth = style.outline,
      },
      style.layer);
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
        ui_resolve_pos(state, cmd->rectPos.origin, cmd->rectPos.offset, cmd->rectPos.units),
        cmd->rectPos.axis);
    break;
  case UiCmd_RectSize:
    ui_build_set_size(
        state, ui_resolve_vec(state, cmd->rectSize.size, cmd->rectSize.units), cmd->rectSize.axis);
    break;
  case UiCmd_RectSizeGrow: {
    const UiVector cur   = ui_build_rect_currect(state)->size;
    const UiVector delta = ui_resolve_vec(state, cmd->rectSizeGrow.delta, cmd->rectSizeGrow.units);
    ui_build_set_size(
        state,
        ui_vector(math_max(cur.x + delta.x, 0), math_max(cur.y + delta.y, 0)),
        cmd->rectSizeGrow.axis);
  } break;
  case UiCmd_ContainerPush: {
    diag_assert(state->containerStackCount < ui_build_container_stack_max);
    const UiRect rect                                   = *ui_build_rect_currect(state);
    state->containerStack[state->containerStackCount++] = (UiBuildContainer){
        .rect   = rect,
        .clipId = state->ctx->outputClipRect(state->ctx->userCtx, rect),
    };
  } break;
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
  case UiCmd_StyleColorMult: {
    const UiColor cur                    = ui_build_style_currect(state)->color;
    ui_build_style_currect(state)->color = ui_color(
        (u8)math_min(cur.r * cmd->styleColorMult.value, u8_max),
        (u8)math_min(cur.g * cmd->styleColorMult.value, u8_max),
        (u8)math_min(cur.b * cmd->styleColorMult.value, u8_max),
        cur.a);
  } break;
  case UiCmd_StyleOutline:
    ui_build_style_currect(state)->outline = cmd->styleOutline.value;
    break;
  case UiCmd_StyleLayer:
    ui_build_style_currect(state)->layer = cmd->styleLayer.value;
    break;
  case UiCmd_StyleVariation:
    ui_build_style_currect(state)->variation = cmd->styleVariation.value;
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
      .font            = ctx->font,
      .rectStack[0]    = g_ui_defaultRect,
      .rectStackCount  = 1,
      .styleStack[0]   = {g_ui_defaultColor, g_ui_defaultOutline},
      .styleStackCount = 1,
      .containerStack[0] =
          {.rect.size = {ctx->canvasRes.width, ctx->canvasRes.height}, .clipId = 0},
      .containerStackCount = 1,
      .hoveredId           = sentinel_u64,
  };

  UiCmd* cmd = null;
  while ((cmd = ui_cmd_next(cmdBuffer, cmd))) {
    ui_build_cmd(&state, cmd);
  }

  return (UiBuildResult){
      .hoveredId = state.hoveredId,
  };
}
