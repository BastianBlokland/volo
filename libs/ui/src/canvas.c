#include "core_dynarray.h"
#include "ecs_world.h"
#include "ui_canvas.h"

typedef enum {
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiId    id;
  Unicode cp;
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
  UiFlags  flags;
  DynArray commands; // UiCmd[]
  UiId     nexId;
};

ecs_module_init(ui_canvas_module) { ecs_register_comp(UiCanvasComp); }

UiCanvasComp* ui_canvas_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(world, entity, UiCanvasComp);
}

void ui_canvas_reset(UiCanvasComp* comp) {
  comp->flags |= UiFlags_Dirty;
  dynarray_clear(&comp->commands);
  comp->nexId = 0;
}

UiId ui_canvas_draw_glyph(UiCanvasComp* comp, const Unicode cp) {
  const UiId id = comp->nexId++;

  *dynarray_push_t(&comp->commands, UiCmd) = (UiCmd){
      .type      = UiCmd_DrawGlyph,
      .drawGlyph = {
          .id = id,
          .cp = cp,
      }};
  return id;
}
