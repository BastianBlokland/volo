#include "core_diag.h"

#include "builder_internal.h"
#include "cmd_internal.h"

static const UiVector g_ui_defaultCursor = {0, 0};
static const UiVector g_ui_defaultSize   = {100, 100};
static const UiColor  g_ui_defaultColor  = {255, 255, 255, 255};

typedef struct {
  const UiBuildCtx*   ctx;
  const AssetFtxComp* font;
  UiVector            cursor;
  UiVector            size;
  UiColor             color;
} UiBuildState;

static UiDrawData ui_build_drawdata(const UiBuildState* state) {
  return (UiDrawData){
      .glyphsPerDim    = state->font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)state->font->glyphsPerDim,
  };
}

static void ui_build_draw_glyph(UiBuildState* state, const UiDrawGlyph* drawGlyph) {
  const AssetFtxChar* ch = asset_ftx_lookup(state->font, drawGlyph->cp);
  if (!sentinel_check(ch->glyphIndex)) {
    const UiRect rect = {
        .position =
            {
                ch->offsetX * state->size.x + state->cursor.x,
                ch->offsetY * state->size.y + state->cursor.y,
            },
        .size = {ch->size * state->size.x, ch->size * state->size.y},
    };

    state->ctx->outputGlyph(
        state->ctx->userCtx,
        (UiGlyphData){
            .rect       = rect,
            .color      = state->color,
            .atlasIndex = ch->glyphIndex,
        });
  }
}

static void ui_build_cmd(UiBuildState* state, const UiCmd* cmd) {
  switch (cmd->type) {
  case UiCmd_SetSize:
    state->size = cmd->setSize.size;
    break;
  case UiCmd_SetColor:
    state->color = cmd->setColor.color;
    break;
  case UiCmd_DrawGlyph:
    ui_build_draw_glyph(state, &cmd->drawGlyph);
    break;
  }
}

void ui_build(const UiCmdBuffer* cmdBuffer, const AssetFtxComp* font, const UiBuildCtx* ctx) {
  UiBuildState state = {
      .ctx    = ctx,
      .font   = font,
      .cursor = g_ui_defaultCursor,
      .size   = g_ui_defaultSize,
      .color  = g_ui_defaultColor,
  };
  ctx->outputDraw(ctx->userCtx, ui_build_drawdata(&state));

  UiCmd* cmd = null;
  while ((cmd = ui_cmd_next(cmdBuffer, cmd))) {
    ui_build_cmd(&state, cmd);
  }
}
