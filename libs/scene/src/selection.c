#include "core_alloc.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_selection.h"
#include "scene_tag.h"

typedef struct {
  EcsEntityId target;
} SceneSelectionRequest;

ecs_comp_define(SceneSelectionComp) {
  EcsEntityId selectedEntity;
  DynArray    requests; // SceneSelectionRequest[].
};

static void ecs_destruct_selection_comp(void* data) {
  SceneSelectionComp* comp = data;
  dynarray_destroy(&comp->requests);
}

ecs_view_define(UpdateView) { ecs_access_write(SceneSelectionComp); }
ecs_view_define(TagView) { ecs_access_write(SceneTagComp); }

static void scene_selection_tag_clear(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* tagItr = ecs_view_itr(ecs_world_view_t(world, TagView));
  if (ecs_view_maybe_jump(tagItr, entity)) {
    ecs_view_write_t(tagItr, SceneTagComp)->tags &= ~SceneTags_Selected;
  }
}

static void scene_selection_tag_set(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* tagItr = ecs_view_itr(ecs_world_view_t(world, TagView));
  if (ecs_view_maybe_jump(tagItr, entity)) {
    ecs_view_write_t(tagItr, SceneTagComp)->tags |= SceneTags_Selected;
  } else {
    scene_tag_add(world, entity, SceneTags_Selected);
  }
}

static SceneSelectionComp* scene_selection_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, UpdateView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  if (LIKELY(itr)) {
    return ecs_view_write_t(itr, SceneSelectionComp);
  }
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneSelectionComp,
      .requests = dynarray_create_t(g_alloc_heap, SceneSelectionRequest, 1));
}

ecs_system_define(SceneSelectionUpdateSys) {
  SceneSelectionComp* selection = scene_selection_get_or_create(world);
  dynarray_for_t(&selection->requests, SceneSelectionRequest, req) {

    /**
     * Clear the current selection.
     */
    if (selection->selectedEntity) {
      scene_selection_tag_clear(world, selection->selectedEntity);
      selection->selectedEntity = 0;
    }

    /**
     * Set the new selection.
     */
    if (req->target) {
      scene_selection_tag_set(world, req->target);
      selection->selectedEntity = req->target;
    }
  }

  dynarray_clear(&selection->requests);
}

ecs_module_init(scene_selection_module) {
  ecs_register_comp(SceneSelectionComp, .destructor = ecs_destruct_selection_comp);

  ecs_register_view(UpdateView);
  ecs_register_view(TagView);

  ecs_register_system(SceneSelectionUpdateSys, ecs_view_id(UpdateView), ecs_view_id(TagView));

  ecs_order(SceneSelectionUpdateSys, SceneOrder_SelectionUpdate);
}

EcsEntityId scene_selected(const SceneSelectionComp* comp) { return comp->selectedEntity; }

void scene_select(SceneSelectionComp* comp, const EcsEntityId entity) {
  *dynarray_push_t(&comp->requests, SceneSelectionRequest) = (SceneSelectionRequest){
      .target = entity,
  };
}

void scene_deselect(SceneSelectionComp* comp) {
  *dynarray_push_t(&comp->requests, SceneSelectionRequest) = (SceneSelectionRequest){
      .target = 0,
  };
}
