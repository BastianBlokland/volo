#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_math.h"
#include "ui_canvas.h"
#include "ui_shape.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "text_internal.h"

#define ui_build_rect_stack_max 10
#define ui_build_style_stack_max 10
#define ui_build_container_stack_max 10

static const String g_uiAtomTypeNames[] = {
    string_static("glyph"),
    string_static("image"),
};
ASSERT(array_elems(g_uiAtomTypeNames) == UiAtomType_Count, "Incorrect number of names");

typedef struct {
  UiColor  color;
  u8       outline;
  u8       variation;
  UiWeight weight : 8;
  UiLayer  layer : 8;
} UiBuildStyle;

typedef struct {
  /**
   * Logic rectangle is not clipped by the parent container, clipRect however is.
   */
  UiRect  logicRect, clipRect;
  u8      clipId;
  UiLayer clipLayer : 8;
} UiBuildContainer;

typedef struct {
  const UiBuildCtx*       ctx;
  const AssetFontTexComp* atlasFont;
  const AssetAtlasComp*   atlasImage;
  UiRect                  rectStack[ui_build_rect_stack_max];
  u32                     rectStackCount;
  UiBuildStyle            styleStack[ui_build_style_stack_max];
  u32                     styleStackCount;
  UiBuildContainer        containerStack[ui_build_container_stack_max];
  u32                     containerStackCount;
  UiBuildHover            hover;
} UiBuildState;

static UiRect* ui_build_rect_current(UiBuildState* state) {
  diag_assert(state->rectStackCount);
  return &state->rectStack[state->rectStackCount - 1];
}

static UiBuildStyle* ui_build_style_current(UiBuildState* state) {
  diag_assert(state->rectStackCount);
  return &state->styleStack[state->styleStackCount - 1];
}

static UiBuildContainer* ui_build_container_current(UiBuildState* state, const UiLayer layer) {
  for (u32 i = state->containerStackCount; i--;) {
    if (state->containerStack[i].clipLayer >= layer) {
      return &state->containerStack[i];
    }
  }
  return &state->containerStack[0]; // All elements are affected by the root container.
}

INLINE_HINT static UiVector
ui_resolve_vec(UiBuildState* state, const UiVector vec, const UiBase units) {
  switch (units) {
  case UiBase_Absolute:
    return vec;
  case UiBase_Current: {
    const UiRect currentRect = *ui_build_rect_current(state);
    return ui_vector(vec.x * currentRect.width, vec.y * currentRect.height);
  }
  case UiBase_Container: {
    const UiBuildContainer container = *ui_build_container_current(state, UiLayer_Normal);
    return ui_vector(vec.x * container.logicRect.width, vec.y * container.logicRect.height);
  }
  case UiBase_Canvas: {
    return ui_vector(vec.x * state->ctx->canvasRes.width, vec.y * state->ctx->canvasRes.height);
  }
  case UiBase_Input:
    return ui_vector(0, 0);
  }
  UNREACHABLE
}

static UiVector ui_resolve_origin(UiBuildState* state, const UiBase origin) {
  switch (origin) {
  case UiBase_Absolute:
    return ui_vector(0, 0);
  case UiBase_Current: {
    const UiRect currentRect = *ui_build_rect_current(state);
    return ui_vector(currentRect.x, currentRect.y);
  }
  case UiBase_Container: {
    const UiBuildContainer container = *ui_build_container_current(state, UiLayer_Normal);
    return ui_vector(container.logicRect.x, container.logicRect.y);
  }
  case UiBase_Canvas:
    return ui_vector(0, 0);
  case UiBase_Input: {
    return state->ctx->inputPos;
  }
  }
  UNREACHABLE
}

INLINE_HINT static UiVector ui_resolve_pos(
    UiBuildState* state, const UiBase origin, const UiVector offset, const UiBase units) {
  const UiVector originVec = ui_resolve_origin(state, origin);
  const UiVector offsetVec = ui_resolve_vec(state, offset, units);
  return ui_vector(originVec.x + offsetVec.x, originVec.y + offsetVec.y);
}

static void ui_build_set_pos(UiBuildState* state, const UiVector val, const UiAxis axis) {
  if (axis & Ui_X) {
    ui_build_rect_current(state)->pos.x = val.x;
  }
  if (axis & Ui_Y) {
    ui_build_rect_current(state)->pos.y = val.y;
  }
}

static void ui_build_set_size(UiBuildState* state, const UiVector val, const UiAxis axis) {
  if (axis & Ui_X) {
    ui_build_rect_current(state)->size.x = val.x;
  }
  if (axis & Ui_Y) {
    ui_build_rect_current(state)->size.y = val.y;
  }
}

static void ui_build_set_size_to(UiBuildState* state, const UiVector val, const UiAxis axis) {
  UiRect* rect = ui_build_rect_current(state);
  if (axis & Ui_X) {
    rect->width = math_abs(val.x - rect->x);
    rect->x     = math_min(rect->x, val.x);
  }
  if (axis & Ui_Y) {
    rect->height = math_abs(val.y - rect->y);
    rect->y      = math_min(rect->y, val.y);
  }
}

static f32 ui_build_angle_rad_to_frac(const f32 angle) {
  static const f32 g_radToFrac = 1.0f / (math_pi_f32 * 2.0f);
  return math_mod_f32(angle * g_radToFrac, 1.0f);
}

static void ui_build_atom_glyph(
    UiBuildState*      state,
    const Unicode      cp,
    const UiRect       rect,
    const UiBuildStyle style,
    const u16          maxCorner,
    const f32          angleRad,
    const u8           clipId) {
  const AssetFontTexChar* ch = asset_fonttex_lookup(state->atlasFont, cp, style.variation);
  if (sentinel_check(ch->glyphIndex)) {
    return; // No glyph for the given codepoint.
  }
  const f32    halfMinDim = math_min(rect.width, rect.height) * 0.5f;
  const f32    corner     = maxCorner ? math_min(maxCorner, halfMinDim) : halfMinDim;
  const f32    border     = state->atlasFont->border * corner * 2.0f;
  const UiRect outputRect = {
      .pos  = {rect.x - border, rect.y - border},
      .size = {rect.width + border * 2, rect.height + border * 2},
  };
  if (UNLIKELY(outputRect.size.width < f32_epsilon || outputRect.size.height < f32_epsilon)) {
    return; // Glyph too small.
  }
  const bool rotated = math_abs(angleRad) > f32_epsilon;
  state->ctx->outputAtom(
      state->ctx->userCtx,
      (UiAtomData){
          .atomType   = UiAtomType_Glyph,
          .rect       = outputRect,
          .color      = style.color,
          .atlasIndex = ch->glyphIndex,
          .angleFrac  = rotated ? (u16)(ui_build_angle_rad_to_frac(angleRad) * u16_max) : 0,
          .cornerFrac = (u16)((corner + border) / outputRect.size.width * u16_max),
          .clipId     = clipId,

          .glyphBorderFrac   = (u16)(border / outputRect.size.width * u16_max),
          .glyphOutlineWidth = style.outline,
          .glyphWeight       = style.weight,
      },
      style.layer);
}

static void ui_build_atom_image(
    UiBuildState*      state,
    const StringHash   img,
    const UiRect       rect,
    const UiBuildStyle style,
    const u16          maxCorner,
    const f32          angleRad,
    const u8           clipId) {
  if (UNLIKELY(rect.size.width < f32_epsilon || rect.size.height < f32_epsilon)) {
    return; // Image too small.
  }
  const AssetAtlasEntry* entry = asset_atlas_lookup(state->atlasImage, img);
  if (UNLIKELY(!entry)) {
    // Image not found in atlas; draw a replacement square.
    // TODO: Should we also log an error/warning in this case?
    ui_build_atom_glyph(state, UiShape_Square, rect, style, maxCorner, angleRad, clipId);
    return;
  }
  const f32  halfMinDim = math_min(rect.width, rect.height) * 0.5f;
  const f32  corner     = maxCorner ? math_min(maxCorner, halfMinDim) : halfMinDim;
  const bool rotated    = math_abs(angleRad) > f32_epsilon;
  state->ctx->outputAtom(
      state->ctx->userCtx,
      (UiAtomData){
          .atomType   = UiAtomType_Image,
          .rect       = rect,
          .color      = style.color,
          .atlasIndex = entry->atlasIndex,
          .angleFrac  = rotated ? (u16)(ui_build_angle_rad_to_frac(angleRad) * u16_max) : 0,
          .cornerFrac = (u16)(corner / rect.size.width * u16_max),
          .clipId     = clipId,
      },
      style.layer);
}

static void ui_build_atom_text_char(void* userCtx, const UiTextCharInfo* info) {
  UiBuildState* state = userCtx;

  const u8       clipId = ui_build_container_current(state, info->layer)->clipId;
  const f32      border = info->font->border * info->size;
  const f32      size   = (info->ch->size + info->font->border * 2.0f) * info->size;
  const UiVector pos    = {
      info->pos.x + info->ch->offsetX * info->size - border,
      info->pos.y + info->ch->offsetY * info->size - border,
  };
  state->ctx->outputAtom(
      state->ctx->userCtx,
      (UiAtomData){
          .atomType   = UiAtomType_Glyph,
          .rect       = {pos, ui_vector(size, size)},
          .color      = info->color,
          .atlasIndex = info->ch->glyphIndex,
          .cornerFrac = (u16)(0.5f * u16_max),
          .clipId     = clipId,

          .glyphBorderFrac   = (u16)(border / size * u16_max),
          .glyphOutlineWidth = info->outline,
          .glyphWeight       = info->weight,
      },
      info->layer);
}

static void ui_build_atom_text_background(void* userCtx, const UiTextBackgroundInfo* info) {
  UiBuildState* state = userCtx;

  const u8           clipId = ui_build_container_current(state, info->layer)->clipId;
  const UiBuildStyle style  = {
       .color  = info->color,
       .weight = UiWeight_Normal,
       .layer  = info->layer,
  };
  const u8  maxCorner = 4; // Roundedness of the backgrounds.
  const f32 angleRad  = 0.0f;
  ui_build_atom_glyph(state, UiShape_Circle, info->rect, style, maxCorner, angleRad, clipId);
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
  return !ui_rect_intersect(container.clipRect, rect, style.outline);
}

static UiRect ui_build_clip(const UiBuildContainer container, const UiRect rect) {
  const f32 minX = math_max(rect.x, container.clipRect.x);
  const f32 minY = math_max(rect.y, container.clipRect.y);
  const f32 maxX = math_min(rect.x + rect.width, container.clipRect.x + container.clipRect.width);
  const f32 maxY = math_min(rect.y + rect.height, container.clipRect.y + container.clipRect.height);
  return (UiRect){
      .x      = minX,
      .y      = minY,
      .width  = maxX - minX,
      .height = maxY - minY,
  };
}

static bool ui_build_is_hovered(
    UiBuildState* state, const UiBuildContainer container, const UiRect rect, const UiLayer layer) {
  if (!sentinel_check(state->hover.id) && state->hover.layer > layer) {
    return false; // Something is already hovered on a higher layer.
  }
  return ui_rect_contains(rect, state->ctx->inputPos) &&
         ui_rect_contains(container.clipRect, state->ctx->inputPos);
}

static void ui_build_draw_text(UiBuildState* state, const UiDrawText* cmd) {
  UiRect                 rect      = *ui_build_rect_current(state);
  const UiBuildStyle     style     = *ui_build_style_current(state);
  const UiBuildContainer container = *ui_build_container_current(state, style.layer);

  if (ui_build_cull(container, rect, style)) {
    return;
  }

  const UiTextBuildResult result = ui_text_build(
      state->atlasFont,
      cmd->flags,
      rect,
      state->ctx->inputPos,
      mem_create(cmd->textPtr, cmd->textSize),
      cmd->fontSize,
      style.color,
      style.outline,
      style.layer,
      style.variation,
      style.weight,
      cmd->align,
      state,
      &ui_build_atom_text_char,
      &ui_build_atom_text_background);

  if (cmd->flags & UiFlags_TightTextRect) {
    rect = result.rect;
  }

  const bool debugAllInteract = state->ctx->settings->inspectorMode == UiInspectorMode_DebugAll;
  const bool hoverable        = cmd->flags & UiFlags_Interactable || debugAllInteract;

  if (hoverable && ui_build_is_hovered(state, container, rect, style.layer)) {
    state->hover = (UiBuildHover){
        .id    = cmd->id,
        .layer = style.layer,
        .flags = cmd->flags,
    };
  }

  if (cmd->flags & UiFlags_TrackRect) {
    state->ctx->outputRect(state->ctx->userCtx, cmd->id, result.rect);
  }
  if (cmd->flags & UiFlags_TrackTextInfo) {
    state->ctx->outputTextInfo(
        state->ctx->userCtx,
        cmd->id,
        (UiBuildTextInfo){
            .lineCount        = result.lineCount,
            .maxLineCharWidth = result.maxLineCharWidth,
            .hoveredCharIndex = result.hoveredCharIndex,
        });
  }
}

static void ui_build_draw_glyph(UiBuildState* state, const UiDrawGlyph* cmd) {
  const UiRect           rect      = *ui_build_rect_current(state);
  const UiBuildStyle     style     = *ui_build_style_current(state);
  const UiBuildContainer container = *ui_build_container_current(state, style.layer);

  const bool rotated = math_abs(cmd->angleRad) > f32_epsilon;
  // TODO: Support culling for rotated glyphs.
  if (!rotated && ui_build_cull(container, rect, style)) {
    return;
  }
  const bool debugAllInteract = state->ctx->settings->inspectorMode == UiInspectorMode_DebugAll;
  const bool hoverable        = cmd->flags & UiFlags_Interactable || debugAllInteract;

  if (hoverable && ui_build_is_hovered(state, container, rect, style.layer)) {
    // TODO: Implement proper hovering for rotated glyphs.
    state->hover = (UiBuildHover){
        .id    = cmd->id,
        .layer = style.layer,
        .flags = cmd->flags,
    };
  }

  ui_build_atom_glyph(state, cmd->cp, rect, style, cmd->maxCorner, cmd->angleRad, container.clipId);

  if (cmd->flags & UiFlags_TrackRect) {
    diag_assert(!rotated); // Tracking is not supported for rotated glyphs.
    state->ctx->outputRect(state->ctx->userCtx, cmd->id, rect);
  }
}

static void ui_build_draw_image(UiBuildState* state, const UiDrawImage* cmd) {
  const UiRect           rect      = *ui_build_rect_current(state);
  const UiBuildStyle     style     = *ui_build_style_current(state);
  const UiBuildContainer container = *ui_build_container_current(state, style.layer);
  const u8               clipId    = container.clipId;

  const bool rotated = math_abs(cmd->angleRad) > f32_epsilon;
  // TODO: Support culling for rotated images.
  if (!rotated && ui_build_cull(container, rect, style)) {
    return;
  }
  const bool debugAllInteract = state->ctx->settings->inspectorMode == UiInspectorMode_DebugAll;
  const bool hoverable        = cmd->flags & UiFlags_Interactable || debugAllInteract;

  if (hoverable && ui_build_is_hovered(state, container, rect, style.layer)) {
    // TODO: Implement proper hovering for rotated images.
    state->hover = (UiBuildHover){
        .id    = cmd->id,
        .layer = style.layer,
        .flags = cmd->flags,
    };
  }

  if (style.outline) {
    /**
     * Image atoms do not support outlines, to work around this we additionally output a transparent
     * square glyph with an outline.
     */
    const UiBuildStyle outlineStyle = {
        .outline = style.outline,
        .color   = ui_color_clear,
        .layer   = style.layer,
    };
    ui_build_atom_glyph(state, UiShape_Square, rect, outlineStyle, 10, cmd->angleRad, clipId);
  }
  ui_build_atom_image(state, cmd->img, rect, style, cmd->maxCorner, cmd->angleRad, clipId);

  if (cmd->flags & UiFlags_TrackRect) {
    diag_assert(!rotated); // Tracking is not supported for rotated images.
    state->ctx->outputRect(state->ctx->userCtx, cmd->id, rect);
  }
}

static void ui_build_debug_inspector(
    UiBuildState*    state,
    const UiId       id,
    const UiFlags    flags,
    const f32        angleRad,
    const UiAtomType atomType) {
  const UiRect           rect      = *ui_build_rect_current(state);
  const UiBuildStyle     style     = *ui_build_style_current(state);
  const UiBuildContainer container = *ui_build_container_current(state, style.layer);

  const UiBuildStyle styleShape          = {.color = {255, 0, 0, 178}, .layer = UiLayer_Debug};
  const UiBuildStyle styleContainerLogic = {.color = {0, 0, 255, 178}, .layer = UiLayer_Debug};
  const UiBuildStyle styleContainerClip  = {.color = {0, 255, 0, 178}, .layer = UiLayer_Debug};

  const UiBuildStyle styleText = {
      .color     = ui_color_white,
      .outline   = 3,
      .variation = 1,
      .weight    = UiWeight_Bold,
      .layer     = UiLayer_Debug,
  };

  ui_build_atom_glyph(state, UiShape_Square, container.logicRect, styleContainerLogic, 5, 0.0f, 0);
  ui_build_atom_glyph(state, UiShape_Square, container.clipRect, styleContainerClip, 5, 0.0f, 0);
  ui_build_atom_glyph(state, UiShape_Square, rect, styleShape, 5, 0.0f, 0);

  DynString str = dynstring_create(g_allocScratch, usize_kibibyte);
  fmt_write(&str, "Id\a>0B{}\n", fmt_int(id));
  fmt_write(&str, "AtomType\a>0B{}\n", fmt_text(g_uiAtomTypeNames[atomType]));
  fmt_write(&str, "X\a>0B{}\n", fmt_float(rect.x, .maxDecDigits = 2));
  fmt_write(&str, "Y\a>0B{}\n", fmt_float(rect.y, .maxDecDigits = 2));
  fmt_write(&str, "Width\a>0B{}\n", fmt_float(rect.width, .maxDecDigits = 2));
  fmt_write(&str, "Height\a>0B{}\n", fmt_float(rect.height, .maxDecDigits = 2));
  fmt_write(
      &str,
      "Color\a>0B#{}{}{}{}\n",
      fmt_int(style.color.r, .base = 16, .minDigits = 2),
      fmt_int(style.color.g, .base = 16, .minDigits = 2),
      fmt_int(style.color.b, .base = 16, .minDigits = 2),
      fmt_int(style.color.a, .base = 16, .minDigits = 2));
  fmt_write(&str, "Outline\a>0B{}\n", fmt_int(style.outline));
  fmt_write(&str, "Layer\a>0B{}\n", fmt_int(style.layer));
  fmt_write(&str, "Variation\a>0B{}\n", fmt_int(style.variation));
  fmt_write(&str, "ClipId\a>0B{}\n", fmt_int(container.clipId));
  fmt_write(&str, "Interact\a>0B{}\n", fmt_int((flags & UiFlags_Interactable) != 0));
  fmt_write(
      &str,
      "Angle\a>0B{} rad ({} deg)\n",
      fmt_float(angleRad, .minDecDigits = 2, .maxDecDigits = 2),
      fmt_float(angleRad * math_rad_to_deg, .maxDecDigits = 0));

  fmt_write(&str, "Containers\n");
  for (u32 i = state->containerStackCount; i--;) {
    const UiBuildContainer* entry = &state->containerStack[i];
    fmt_write(
        &str,
        " [{}] ClipId: {}, ClipLayer: {}\n",
        fmt_int(i),
        fmt_int(entry->clipId),
        fmt_int(entry->clipLayer));
  }

  const f32    textSize = 500;
  const u16    fontSize = 20;
  const UiRect textRect = {
      .pos  = {state->ctx->canvasRes.width * 0.5f, state->ctx->canvasRes.height - textSize},
      .size = {textSize, textSize},
  };
  ui_text_build(
      state->atlasFont,
      UiFlags_None,
      textRect,
      state->ctx->inputPos,
      dynstring_view(&str),
      fontSize,
      styleText.color,
      styleText.outline,
      styleText.layer,
      styleText.variation,
      styleText.weight,
      UiAlign_TopLeft,
      state,
      &ui_build_atom_text_char,
      &ui_build_atom_text_background);
}

INLINE_HINT static void ui_build_cmd(UiBuildState* state, const UiCmd* cmd) {
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
  case UiCmd_RectSizeTo:
    ui_build_set_size_to(
        state,
        ui_resolve_pos(
            state, cmd->rectSizeTo.origin, cmd->rectSizeTo.offset, cmd->rectSizeTo.units),
        cmd->rectSizeTo.axis);
    break;
  case UiCmd_RectSizeGrow: {
    const UiVector cur   = ui_build_rect_current(state)->size;
    const UiVector delta = ui_resolve_vec(state, cmd->rectSizeGrow.delta, cmd->rectSizeGrow.units);
    ui_build_set_size(
        state,
        ui_vector(math_max(cur.x + delta.x, 0), math_max(cur.y + delta.y, 0)),
        cmd->rectSizeGrow.axis);
  } break;
  case UiCmd_ContainerPush: {
    diag_assert(state->containerStackCount < ui_build_container_stack_max);
    const UiLayer          layer            = cmd->containerPush.layer;
    const UiBuildContainer currentContainer = *ui_build_container_current(state, layer);
    const UiRect           logicRect        = *ui_build_rect_current(state);
    UiRect                 clipRect;
    u8                     clipId;
    switch (cmd->containerPush.clip) {
    case UiClip_None:
      clipRect = currentContainer.clipRect;
      clipId   = currentContainer.clipId;
      break;
    case UiClip_Rect:
      clipRect = ui_build_clip(currentContainer, logicRect);
      clipId   = state->ctx->outputClipRect(state->ctx->userCtx, clipRect);
      break;
    }
    state->containerStack[state->containerStackCount++] = (UiBuildContainer){
        .logicRect = logicRect,
        .clipRect  = clipRect,
        .clipId    = clipId,
        .clipLayer = layer,
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
    ui_build_style_current(state)->color = cmd->styleColor.value;
    break;
  case UiCmd_StyleColorMult: {
    const UiColor cur                    = ui_build_style_current(state)->color;
    ui_build_style_current(state)->color = ui_color(
        (u8)math_min(cur.r * cmd->styleColorMult.value, u8_max),
        (u8)math_min(cur.g * cmd->styleColorMult.value, u8_max),
        (u8)math_min(cur.b * cmd->styleColorMult.value, u8_max),
        cur.a);
  } break;
  case UiCmd_StyleOutline:
    ui_build_style_current(state)->outline = cmd->styleOutline.value;
    break;
  case UiCmd_StyleLayer:
    ui_build_style_current(state)->layer = cmd->styleLayer.value;
    break;
  case UiCmd_StyleVariation:
    ui_build_style_current(state)->variation = cmd->styleVariation.value;
    break;
  case UiCmd_StyleWeight:
    ui_build_style_current(state)->weight = cmd->styleWeight.value;
    break;
  case UiCmd_DrawText:
    ui_build_draw_text(state, &cmd->drawText);
    if (UNLIKELY(cmd->drawText.id == state->ctx->debugElem)) {
      const UiId id       = cmd->drawText.id;
      const f32  angleRad = 0.0f;
      ui_build_debug_inspector(state, id, cmd->drawText.flags, angleRad, UiAtomType_Glyph);
    }
    break;
  case UiCmd_DrawGlyph:
    ui_build_draw_glyph(state, &cmd->drawGlyph);
    if (UNLIKELY(cmd->drawGlyph.id == state->ctx->debugElem)) {
      const UiId id       = cmd->drawGlyph.id;
      const f32  angleRad = cmd->drawGlyph.angleRad;
      ui_build_debug_inspector(state, id, cmd->drawGlyph.flags, angleRad, UiAtomType_Glyph);
    }
    break;
  case UiCmd_DrawImage:
    ui_build_draw_image(state, &cmd->drawImage);
    if (UNLIKELY(cmd->drawImage.id == state->ctx->debugElem)) {
      const UiId id       = cmd->drawImage.id;
      const f32  angleRad = cmd->drawImage.angleRad;
      ui_build_debug_inspector(state, id, cmd->drawImage.flags, angleRad, UiAtomType_Image);
    }
    break;
  }
}

UiBuildResult ui_build(const UiCmdBuffer* cmdBuffer, const UiBuildCtx* ctx) {
  UiBuildState state = {
      .ctx            = ctx,
      .atlasFont      = ctx->atlasFont,
      .atlasImage     = ctx->atlasImage,
      .rectStack[0]   = {.width = 100, .height = 100},
      .rectStackCount = 1,
      .styleStack[0] =
          {
              .color     = ctx->settings->defaultColor,
              .outline   = ctx->settings->defaultOutline,
              .variation = ctx->settings->defaultVariation,
              .weight    = ctx->settings->defaultWeight,
              .layer     = UiLayer_Normal,
          },
      .styleStackCount = 1,
      .containerStack[0] =
          {
              .logicRect.size = {ctx->canvasRes.width, ctx->canvasRes.height},
              .clipRect.size  = {ctx->canvasRes.width, ctx->canvasRes.height},
              .clipId         = 0,
              .clipLayer      = UiLayer_Normal,
          },
      .containerStackCount = 1,
      .hover               = {.id = sentinel_u64},
  };

  UiCmd* cmd = null;
  while ((cmd = ui_cmd_next(cmdBuffer, cmd))) {
    ui_build_cmd(&state, cmd);
  }

  return (UiBuildResult){
      .commandCount = ui_cmdbuffer_count(cmdBuffer),
      .hover        = state.hover,
  };
}
