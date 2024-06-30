#include "ecs_utils.h"
#include "log_logger.h"

#include "loader_shader_internal.h"
#include "manager_internal.h"

typedef enum {
  GlslKind_Vertex,
  GlslKind_Fragment,
} GlslKind;

ecs_comp_define(AssetGlslLoadComp) {
  GlslKind     kind;
  AssetSource* src;
};

static void ecs_destruct_glsl_load_comp(void* data) {
  AssetGlslLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_read(AssetGlslLoadComp); }

/**
 * Load glsl-shader assets.
 */
ecs_system_define(LoadGlslAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    (void)entity;
  }
}

ecs_module_init(asset_shader_glsl_module) {
  ecs_register_comp(AssetGlslLoadComp, .destructor = ecs_destruct_glsl_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);

  ecs_register_system(LoadGlslAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
}

void asset_load_glsl_vert(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = GlslKind_Vertex, .src = src);
}

void asset_load_glsl_frag(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGlslLoadComp, .kind = GlslKind_Fragment, .src = src);
}
