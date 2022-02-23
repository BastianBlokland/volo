#include "core_diag.h"
#include "core_math.h"
#include "gap_window.h"

#include "builder_internal.h"
#include "cmd_internal.h"

static const UiVector g_ui_defaultPos          = {0, 0};
static const UiVector g_ui_defaultSize         = {100, 100};
static const UiColor  g_ui_defaultColor        = {255, 255, 255, 255};
static const UiFlow   g_ui_defaultFlow         = UiFlow_Right;
static const u8       g_ui_defaultOutlineWidth = 0;

typedef struct {
  const UiBuildCtx*    ctx;
  const GapWindowComp* window;
  const AssetFtxComp*  font;
  UiVector             pos;
  UiVector             size;
  UiFlow               flow;
  UiColor              color;
  u8                   outlineWidth;
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
  case UiOrigin_BottomLeft:
    return vec;
  case UiOrigin_BottomRight:
    return ui_vector(winSize.width - vec.x, vec.y);
  case UiOrigin_TopLeft:
    return ui_vector(vec.x, winSize.height - vec.y);
  case UiOrigin_TopRight:
    return ui_vector(winSize.width - vec.x, winSize.height - vec.y);
  }
  diag_crash();
}

static void ui_advance(UiBuildState* state, const UiVector size) {
  switch (state->flow) {
  case UiFlow_Left:
    state->pos.x -= size.width;
    break;
  case UiFlow_Right:
    state->pos.x += size.width;
    break;
  case UiFlow_Down:
    state->pos.y -= size.height;
    break;
  case UiFlow_Up:
    state->pos.y += size.height;
    break;
  }
}

static void ui_build_draw_glyph(UiBuildState* state, const UiDrawGlyph* cmd) {
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, cmd->cp);
  if (!sentinel_check(ch->glyphIndex)) {
    /**
     * NOTE: Take the border into account as the glyph will need to be drawn bigger to compensate.
     */
    const f32    border     = ch->border * state->size.x; // TODO: Handle non square.
    const UiRect renderRect = {
        .position =
            {
                state->pos.x - border,
                state->pos.y - border,
            },
        .size =
            {
                state->size.x + border * 2,
                state->size.y + border * 2,
            },
    };

    state->ctx->outputGlyph(
        state->ctx->userCtx,
        (UiGlyphData){
            .rect         = renderRect,
            .color        = state->color,
            .atlasIndex   = ch->glyphIndex,
            .invBorder    = 1.0f / border,
            .outlineWidth = state->outlineWidth,
        });
  }
  ui_advance(state, state->size);
}

static void ui_build_cmd(UiBuildState* state, const UiCmd* cmd) {
  switch (cmd->type) {
  case UiCmd_SetPos:
    state->pos = ui_resolve_pos(state, cmd->setPos.pos, cmd->setPos.origin, cmd->setPos.units);
    break;
  case UiCmd_SetSize:
    state->size = ui_resolve_vec(state, cmd->setSize.size, cmd->setSize.units);
    break;
  case UiCmd_SetFlow:
    state->flow = cmd->setFlow.flow;
    break;
  case UiCmd_SetColor:
    state->color        = cmd->setColor.color;
    state->outlineWidth = cmd->setColor.outlineWidth;
    break;
  case UiCmd_DrawGlyph:
    ui_build_draw_glyph(state, &cmd->drawGlyph);
    break;
  }
}

void ui_build(
    const UiCmdBuffer*   cmdBuffer,
    const GapWindowComp* window,
    const AssetFtxComp*  font,
    const UiBuildCtx*    ctx) {

  UiBuildState state = {
      .ctx          = ctx,
      .window       = window,
      .font         = font,
      .pos          = g_ui_defaultPos,
      .size         = g_ui_defaultSize,
      .flow         = g_ui_defaultFlow,
      .color        = g_ui_defaultColor,
      .outlineWidth = g_ui_defaultOutlineWidth,
  };
  ctx->outputDraw(ctx->userCtx, ui_build_drawdata(&state));

  UiCmd* cmd = null;
  while ((cmd = ui_cmd_next(cmdBuffer, cmd))) {
    ui_build_cmd(&state, cmd);
  }
}
