#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

typedef struct {
  const u32* childIndices;
  u32        childCount;
  StringHash nameHash;
} SceneSkeletonJoint;

ecs_comp_extern(SceneSkeletonTemplateComp);

ecs_comp_extern_public(SceneSkeletonComp) {
  u32        jointCount;
  GeoMatrix* jointTransforms;
};

typedef struct {
  f32        time;
  f32        duration;
  f32        speed;
  StringHash nameHash;
} SceneAnimLayer;

ecs_comp_extern_public(SceneAnimationComp) {
  SceneAnimLayer* layers;
  u32             layerCount;
};

u32 scene_skeleton_root_index(const SceneSkeletonTemplateComp*);

const SceneSkeletonJoint* scene_skeleton_joint(const SceneSkeletonTemplateComp*, u32 jointIndex);

void scene_skeleton_joint_delta(
    const SceneSkeletonComp*, const SceneSkeletonTemplateComp*, GeoMatrix* out);
