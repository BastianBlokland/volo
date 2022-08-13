#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_selection.h"
#include "scene_tag.h"

typedef enum {
  SelectionRequestType_Add,
  SelectionRequestType_Remove,
  SelectionRequestType_Clear,
} SelectionRequestType;

typedef struct {
  SelectionRequestType type;
  EcsEntityId          target;
} SelectionRequest;

ecs_comp_define(SceneSelectionComp) {
  DynArray    selectedEntities; // EcsEntityId[], sorted.
  EcsEntityId mainSelectedEntity;
  DynArray    requests; // SelectionRequest[].
};

static void ecs_destruct_selection_comp(void* data) {
  SceneSelectionComp* comp = data;
  dynarray_destroy(&comp->selectedEntities);
  dynarray_destroy(&comp->requests);
}

ecs_view_define(UpdateView) { ecs_access_write(SceneSelectionComp); }
ecs_view_define(TagView) { ecs_access_write(SceneTagComp); }

static void selection_tag_clear(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* tagItr = ecs_view_itr(ecs_world_view_t(world, TagView));
  if (ecs_view_maybe_jump(tagItr, entity)) {
    ecs_view_write_t(tagItr, SceneTagComp)->tags &= ~SceneTags_Selected;
  }
}

static void selection_tag_set(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* tagItr = ecs_view_itr(ecs_world_view_t(world, TagView));
  if (ecs_view_maybe_jump(tagItr, entity)) {
    ecs_view_write_t(tagItr, SceneTagComp)->tags |= SceneTags_Selected;
  } else {
    scene_tag_add(world, entity, SceneTags_Default | SceneTags_Selected);
  }
}

static void selection_add(EcsWorld* world, SceneSelectionComp* comp, const EcsEntityId tgt) {
  *dynarray_insert_sorted_t(&comp->selectedEntities, EcsEntityId, ecs_compare_entity, &tgt) = tgt;
  if (!comp->mainSelectedEntity) {
    comp->mainSelectedEntity = tgt;
  }
  selection_tag_set(world, tgt);
}

static void selection_remove(EcsWorld* world, SceneSelectionComp* comp, const EcsEntityId tgt) {
  EcsEntityId* entry = dynarray_search_binary(&comp->selectedEntities, ecs_compare_entity, &tgt);
  if (entry) {
    const usize index = entry - dynarray_begin_t(&comp->selectedEntities, EcsEntityId);
    dynarray_remove(&comp->selectedEntities, index, 1);
  }
  if (comp->mainSelectedEntity == tgt) {
    if (comp->selectedEntities.size) {
      comp->mainSelectedEntity = *dynarray_begin_t(&comp->selectedEntities, EcsEntityId);
    } else {
      comp->mainSelectedEntity = 0;
    }
  }
  selection_tag_clear(world, tgt);
}

static void selection_clear(EcsWorld* world, SceneSelectionComp* comp) {
  dynarray_for_t(&comp->selectedEntities, EcsEntityId, entity) {
    selection_tag_clear(world, *entity);
  }
  dynarray_clear(&comp->selectedEntities);
  comp->mainSelectedEntity = 0;
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
      .selectedEntities = dynarray_create_t(g_alloc_heap, EcsEntityId, 128),
      .requests         = dynarray_create_t(g_alloc_heap, SelectionRequest, 128));
}

ecs_system_define(SceneSelectionUpdateSys) {
  SceneSelectionComp* selection = scene_selection_get_or_create(world);
  dynarray_for_t(&selection->requests, SelectionRequest, req) {
    switch (req->type) {
    case SelectionRequestType_Add:
      selection_add(world, selection, req->target);
      continue;
    case SelectionRequestType_Remove:
      selection_remove(world, selection, req->target);
      continue;
    case SelectionRequestType_Clear:
      selection_clear(world, selection);
      continue;
    }
    diag_crash_msg("Unsupported selection request type");
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

EcsEntityId scene_selection_main(const SceneSelectionComp* comp) {
  return comp->mainSelectedEntity;
}

bool scene_selection_contains(const SceneSelectionComp* comp, const EcsEntityId entity) {
  if (dynarray_search_binary((DynArray*)&comp->selectedEntities, ecs_compare_entity, &entity)) {
    return true;
  }
  return false;
}

bool scene_selection_empty(const SceneSelectionComp* comp) {
  diag_assert((comp->selectedEntities.size != 0) == (comp->mainSelectedEntity != 0));
  return comp->mainSelectedEntity == 0;
}

void scene_selection_add(SceneSelectionComp* comp, const EcsEntityId entity) {
  *dynarray_push_t(&comp->requests, SelectionRequest) = (SelectionRequest){
      .type   = SelectionRequestType_Add,
      .target = entity,
  };
}

void scene_selection_remove(SceneSelectionComp* comp, const EcsEntityId entity) {
  *dynarray_push_t(&comp->requests, SelectionRequest) = (SelectionRequest){
      .type   = SelectionRequestType_Remove,
      .target = entity,
  };
}

void scene_selection_clear(SceneSelectionComp* comp) {
  *dynarray_push_t(&comp->requests, SelectionRequest) = (SelectionRequest){
      .type = SelectionRequestType_Clear,
  };
}
