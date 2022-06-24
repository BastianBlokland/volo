#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

typedef struct {
  GeoMatrix  invBindTransform;
  const u32* childIndices;
  u32        childCount;
  StringHash nameHash;
} SceneSkeletonJoint;

ecs_comp_extern(SceneSkeletonTemplateComp);

ecs_comp_extern_public(SceneSkeletonComp) {
  f32        playHead;
  u32        jointCount;
  GeoMatrix* jointTransforms;
};

u32 scene_skeleton_root_index(const SceneSkeletonTemplateComp*);

const SceneSkeletonJoint* scene_skeleton_joint(const SceneSkeletonTemplateComp*, u32 jointIndex);

void scene_skeleton_joint_delta(
    const SceneSkeletonComp*, const SceneSkeletonTemplateComp*, GeoMatrix* out);
