#include "asset_graphic.h"
#include "asset_manager.h"
#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"

#define scene_skeleton_max_loads 16

ecs_comp_define_public(SceneSkeletonComp);

typedef enum {
  SkeletonTemplateState_Start,
  SkeletonTemplateState_LoadGraphic,
  SkeletonTemplateState_LoadMesh,
  SkeletonTemplateState_Finished,
} SkeletonTemplateState;

typedef struct {
  StringHash nameHash;
} SceneSkeletonAnimation;

/**
 * NOTE: On the graphic asset.
 */
ecs_comp_define(SceneSkeletonTemplateComp) {
  SkeletonTemplateState   state;
  EcsEntityId             mesh;
  u32                     jointCount;
  SceneSkeletonJoint*     joints;
  u32                     jointRootIndex;
  SceneSkeletonAnimation* anims;
  u32                     animCount;
  Mem                     animData;
};

ecs_comp_define(SceneSkeletonTemplateLoadedComp);

static void ecs_destruct_skeleton_comp(void* data) {
  SceneSkeletonComp* comp = data;
  if (comp->jointCount) {
    alloc_free_array_t(g_alloc_heap, comp->jointTransforms, comp->jointCount);
  }
}

static void ecs_combine_skeleton_template(void* dataA, void* dataB) {
  SceneSkeletonTemplateComp* tmplA = dataA;
  SceneSkeletonTemplateComp* tmplB = dataB;

  (void)tmplA;
  (void)tmplB;
  diag_assert_msg(
      tmplA->state == SkeletonTemplateState_Start && tmplB->state == SkeletonTemplateState_Start,
      "Skeleton templates can only be combined in the starting phase");
}

static void ecs_destruct_skeleton_template_comp(void* data) {
  SceneSkeletonTemplateComp* comp = data;
  if (comp->jointCount) {
    alloc_free_array_t(g_alloc_heap, comp->joints, comp->jointCount);
    if (comp->animCount) {
      alloc_free_array_t(g_alloc_heap, comp->anims, comp->animCount);
    }
    alloc_free(g_alloc_heap, comp->animData);
  }
}

ecs_view_define(SkeletonInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_without(SceneSkeletonComp);
}

ecs_view_define(SkeletonTemplateView) { ecs_access_read(SceneSkeletonTemplateComp); }

static void scene_skeleton_init_empty(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(world, entity, SceneSkeletonComp);
}

static void scene_skeleton_init_from_template(
    EcsWorld* world, const EcsEntityId entity, const SceneSkeletonTemplateComp* template) {

  if (!template->jointCount) {
    scene_skeleton_init_empty(world, entity);
    return;
  }

  GeoMatrix* jointTransforms = alloc_array_t(g_alloc_heap, GeoMatrix, template->jointCount);
  for (u32 i = 0; i != template->jointCount; ++i) {
    jointTransforms[i] = geo_matrix_inverse(&template->joints[i].invBindTransform);
  }
  ecs_world_add_t(
      world,
      entity,
      SceneSkeletonComp,
      .jointCount      = template->jointCount,
      .jointTransforms = jointTransforms);
}

ecs_system_define(SceneSkeletonInitSys) {
  EcsView*     initView    = ecs_world_view_t(world, SkeletonInitView);
  EcsIterator* templateItr = ecs_view_itr(ecs_world_view_t(world, SkeletonTemplateView));

  u32 startedLoads = 0;

  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const EcsEntityId          graphic    = renderable->graphic;
    if (!graphic) {
      scene_skeleton_init_empty(world, entity);
      continue;
    }

    if (ecs_view_maybe_jump(templateItr, graphic)) {
      const SceneSkeletonTemplateComp* template =
          ecs_view_read_t(templateItr, SceneSkeletonTemplateComp);
      if (template->state == SkeletonTemplateState_Finished) {
        scene_skeleton_init_from_template(world, entity, template);
      }
      continue;
    }

    if (++startedLoads > scene_skeleton_max_loads) {
      continue; // Limit the amount of loads to start in a single frame.
    }
    ecs_world_add_t(world, graphic, SceneSkeletonTemplateComp);
  }
}

ecs_view_define(TemplateLoadView) {
  ecs_access_write(SceneSkeletonTemplateComp);
  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_without(SceneSkeletonTemplateLoadedComp);
}

ecs_view_define(MeshView) {
  ecs_access_with(AssetMeshComp);
  ecs_access_read(AssetMeshSkeletonComp);
}

static bool scene_asset_is_loaded(EcsWorld* world, const EcsEntityId asset) {
  return ecs_world_has_t(world, asset, AssetLoadedComp) ||
         ecs_world_has_t(world, asset, AssetFailedComp);
}

static void
scene_asset_template_init(SceneSkeletonTemplateComp* template, const AssetMeshSkeletonComp* asset) {
  template->jointRootIndex = asset->rootJointIndex;
  template->animData       = alloc_dup(g_alloc_heap, asset->animData, 1);

  template->joints     = alloc_array_t(g_alloc_heap, SceneSkeletonJoint, asset->jointCount);
  template->jointCount = asset->jointCount;
  for (u32 jointIndex = 0; jointIndex != asset->jointCount; ++jointIndex) {
    template->joints[jointIndex] = (SceneSkeletonJoint){
        .invBindTransform = asset->joints[jointIndex].invBindTransform,
        .childIndices = (u32*)mem_at_u8(template->animData, asset->joints[jointIndex].childData),
        .childCount   = asset->joints[jointIndex].childCount,
        .nameHash     = asset->joints[jointIndex].nameHash,
    };
  }

  template->anims     = alloc_array_t(g_alloc_heap, SceneSkeletonAnimation, asset->animCount);
  template->animCount = asset->animCount;
  for (u32 animIndex = 0; animIndex != asset->animCount; ++animIndex) {
    template->anims[animIndex] = (SceneSkeletonAnimation){
        .nameHash = asset->anims[animIndex].nameHash,
    };
  }
}

static void scene_skeleton_template_load_done(EcsWorld* world, EcsIterator* itr) {
  const EcsEntityId entity            = ecs_view_entity(itr);
  SceneSkeletonTemplateComp* template = ecs_view_write_t(itr, SceneSkeletonTemplateComp);

  asset_release(world, entity);
  if (template->mesh) {
    asset_release(world, template->mesh);
  }
  template->state = SkeletonTemplateState_Finished;
  ecs_world_add_empty_t(world, entity, SceneSkeletonTemplateLoadedComp);
}

ecs_system_define(SceneSkeletonTemplateLoadSys) {
  EcsView*     loadView = ecs_world_view_t(world, TemplateLoadView);
  EcsIterator* meshItr  = ecs_view_itr(ecs_world_view_t(world, MeshView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity            = ecs_view_entity(itr);
    SceneSkeletonTemplateComp* template = ecs_view_write_t(itr, SceneSkeletonTemplateComp);
    const AssetGraphicComp* graphic     = ecs_view_read_t(itr, AssetGraphicComp);
    switch (template->state) {
    case SkeletonTemplateState_Start: {
      asset_acquire(world, entity);
      ++template->state;
      // Fallthrough.
    }
    case SkeletonTemplateState_LoadGraphic: {
      if (!scene_asset_is_loaded(world, entity)) {
        break; // Graphic has not loaded yet; wait.
      }
      if (!graphic) {
        scene_skeleton_template_load_done(world, itr);
        break; // Graphic failed to load, or was of unexpected type.
      }
      if (!graphic->mesh) {
        scene_skeleton_template_load_done(world, itr);
        break; // Graphic did not have a mesh.
      }
      template->mesh = graphic->mesh;
      asset_acquire(world, graphic->mesh);
      ++template->state;
      // Fallthrough.
    }
    case SkeletonTemplateState_LoadMesh: {
      if (!scene_asset_is_loaded(world, template->mesh)) {
        break; // Mesh has not loaded yet; wait.
      }
      if (ecs_view_maybe_jump(meshItr, template->mesh)) {
        scene_asset_template_init(template, ecs_view_read_t(meshItr, AssetMeshSkeletonComp));
      }
      scene_skeleton_template_load_done(world, itr);
      break;
    }
    case SkeletonTemplateState_Finished:
      diag_crash();
    }
  }
}

ecs_module_init(scene_skeleton_module) {
  ecs_register_comp(SceneSkeletonComp, .destructor = ecs_destruct_skeleton_comp);
  ecs_register_comp(
      SceneSkeletonTemplateComp,
      .combinator = ecs_combine_skeleton_template,
      .destructor = ecs_destruct_skeleton_template_comp);
  ecs_register_comp_empty(SceneSkeletonTemplateLoadedComp);

  ecs_register_view(SkeletonInitView);
  ecs_register_view(SkeletonTemplateView);

  ecs_register_view(TemplateLoadView);
  ecs_register_view(MeshView);

  ecs_register_system(
      SceneSkeletonInitSys, ecs_view_id(SkeletonInitView), ecs_view_id(SkeletonTemplateView));

  ecs_register_system(
      SceneSkeletonTemplateLoadSys, ecs_view_id(TemplateLoadView), ecs_view_id(MeshView));
}

u32 scene_skeleton_root_index(const SceneSkeletonTemplateComp* templ) {
  return templ->jointRootIndex;
}

const SceneSkeletonJoint*
scene_skeleton_joint(const SceneSkeletonTemplateComp* templ, const u32 jointIndex) {
  diag_assert(jointIndex < templ->jointCount);
  return &templ->joints[jointIndex];
}

void scene_skeleton_joint_delta(
    const SceneSkeletonComp* skeleton, const SceneSkeletonTemplateComp* templ, GeoMatrix* out) {
  diag_assert(skeleton->jointCount == templ->jointCount);
  for (u32 i = 0; i != skeleton->jointCount; ++i) {
    out[i] = geo_matrix_mul(&skeleton->jointTransforms[i], &templ->joints[i].invBindTransform);
  }
}
