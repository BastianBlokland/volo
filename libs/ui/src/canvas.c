#include "asset_ftx.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_dynstring.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "ui_canvas.h"

#include "resource_internal.h"

typedef enum {
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiElementId id;
  Unicode     cp;
} UiDrawGlyph;

typedef struct {
  UiCmdType type;
  union {
    UiDrawGlyph drawGlyph;
  };
} UiCmd;

typedef enum {
  UiFlags_Dirty = 1 << 0,
} UiFlags;

ecs_comp_define(UiCanvasComp) {
  UiFlags     flags;
  DynArray    commands; // UiCmd[]
  UiElementId nextId;
};

static void ecs_destruct_commands(void* data) {
  UiCanvasComp* comp = data;
  dynarray_destroy(&comp->commands);
}

// TODO: Ensure alignment.
typedef struct {
  //  ALIGNAS(16)
  f32 glyphsPerDim;
  f32 invGlyphsPerDim;
  f32 padding[2];
} ShaderCanvasData;

ASSERT(sizeof(ShaderCanvasData) == 16, "Size needs to match the size defined in glsl");

// TODO: Ensure alignment.
typedef struct {
  //  ALIGNAS(8)
  f32 position[2];
  f32 size;
  u32 index;
} ShaderGlyphData;

ASSERT(sizeof(ShaderGlyphData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  const UiCanvasComp*        canvas;
  const AssetFtxComp*        font;
  SceneRenderableUniqueComp* renderable;
  DynString*                 output;
  u32                        outputNumGlyphs;
  UiVector                   cursor;
} UiBuilder;

static void ui_canvas_process_draw_glyph(UiBuilder* builder, const UiDrawGlyph* drawGlyph) {
  const AssetFtxChar* ch = asset_ftx_lookup(builder->font, drawGlyph->cp);
  if (!sentinel_check(ch->glyphIndex)) {
    /**
     * This character has a glyph, output it to the shader.
     */
    const f32        glyphSize = 100;
    ShaderGlyphData* glyphData = dynstring_push(builder->output, sizeof(ShaderGlyphData)).ptr;
    *glyphData                 = (ShaderGlyphData){
        .position =
            {
                ch->offsetX * glyphSize + builder->cursor.x,
                ch->offsetY * glyphSize + builder->cursor.y,
            },
        .size  = ch->size * glyphSize,
        .index = ch->glyphIndex,
    };
    ++builder->outputNumGlyphs;
  }
}

static void ui_canvas_process_cmd(UiBuilder* builder, const UiCmd* cmd) {
  switch (cmd->type) {
  case UiCmd_DrawGlyph:
    ui_canvas_process_draw_glyph(builder, &cmd->drawGlyph);
    break;
  }
}

static void ui_canvas_build(UiBuilder* builder) {
  /**
   * Setup per-canvas data (shared between all glyphs in this text).
   */
  ShaderCanvasData* fontData = dynstring_push(builder->output, sizeof(ShaderCanvasData)).ptr;
  fontData->glyphsPerDim     = builder->font->glyphsPerDim;
  fontData->invGlyphsPerDim  = 1.0f / (f32)builder->font->glyphsPerDim;

  /**
   * Process all commands.
   */
  dynarray_for_t(&builder->canvas->commands, UiCmd, cmd) { ui_canvas_process_cmd(builder, cmd); }

  /**
   * Write the output to the renderable.
   */
  const Mem instData = scene_renderable_unique_data_set(builder->renderable, builder->output->size);
  mem_cpy(instData, dynstring_view(builder->output));
  builder->renderable->vertexCountOverride = builder->outputNumGlyphs * 6; // 6 verts per quad.
}

ecs_view_define(GlobalResourcesView) { ecs_access_read(UiGlobalResourcesComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }

ecs_view_define(CanvasBuildView) {
  ecs_access_write(UiCanvasComp);
  ecs_access_write(SceneRenderableUniqueComp);
}

static const UiGlobalResourcesComp* ui_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_read_t(globalItr, UiGlobalResourcesComp) : null;
}

static const AssetFtxComp* ui_global_font(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, FtxView), entity);
  return itr ? ecs_view_read_t(itr, AssetFtxComp) : null;
}

ecs_system_define(UiCanvasBuildSys) {
  const UiGlobalResourcesComp* globalRes = ui_global_resources(world);
  if (!globalRes) {
    return; // Global resources not initialized yet.
  }
  const AssetFtxComp* font = ui_global_font(world, ui_resource_font(globalRes));
  if (!font) {
    return; // Global font not loaded yet.
  }

  EcsView* buildView = ecs_world_view_t(world, CanvasBuildView);
  for (EcsIterator* itr = ecs_view_itr(buildView); ecs_view_walk(itr);) {
    UiCanvasComp*              canvasComp = ecs_view_write_t(itr, UiCanvasComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);
    if (!(canvasComp->flags & UiFlags_Dirty)) {
      continue; // Canvas did not change, no need to rebuild.
    }
    canvasComp->flags &= ~UiFlags_Dirty;
    renderable->graphic = ui_resource_graphic(globalRes);

    DynString dataBuffer = dynstring_create(g_alloc_heap, 512);
    ui_canvas_build(&(UiBuilder){
        .canvas     = canvasComp,
        .font       = font,
        .renderable = renderable,
        .output     = &dataBuffer,
    });
    dynstring_destroy(&dataBuffer);
  }
}

ecs_module_init(ui_canvas_module) {
  ecs_register_comp(UiCanvasComp, .destructor = ecs_destruct_commands);

  ecs_register_view(CanvasBuildView);
  ecs_register_view(GlobalResourcesView);
  ecs_register_view(FtxView);

  ecs_register_system(
      UiCanvasBuildSys,
      ecs_view_id(CanvasBuildView),
      ecs_view_id(GlobalResourcesView),
      ecs_view_id(FtxView));
}

UiCanvasComp* ui_canvas_create(EcsWorld* world, const EcsEntityId entity) {
  UiCanvasComp* canvasComp = ecs_world_add_t(
      world, entity, UiCanvasComp, .commands = dynarray_create_t(g_alloc_heap, UiCmd, 128));
  ecs_world_add_t(world, entity, SceneRenderableUniqueComp);
  return canvasComp;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  comp->flags |= UiFlags_Dirty;
  dynarray_clear(&comp->commands);
  comp->nextId = 0;
}

UiElementId ui_canvas_draw_glyph(UiCanvasComp* comp, const Unicode cp) {
  const UiElementId id = comp->nextId++;

  *dynarray_push_t(&comp->commands, UiCmd) = (UiCmd){
      .type      = UiCmd_DrawGlyph,
      .drawGlyph = {
          .id = id,
          .cp = cp,
      }};
  return id;
}
