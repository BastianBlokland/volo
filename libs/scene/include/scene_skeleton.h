#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

#define scene_skeleton_joints_max 75

typedef struct {
  const u32* childIndices;
  u32        childCount;
  StringHash nameHash;
} SceneSkeletonJoint;

typedef struct {
  u8 jointBits[scene_skeleton_joints_max / 8 + 1];
} SceneSkeletonMask;

ecs_comp_extern(SceneSkeletonTemplComp);

ecs_comp_extern_public(SceneSkeletonComp) {
  u32        jointCount;
  GeoMatrix* jointTransforms;
};

typedef struct {
  f32               time;
  f32               duration;
  f32               speed;
  f32               weight;
  SceneSkeletonMask mask;
  StringHash        nameHash;
} SceneAnimLayer;

typedef struct {
  u32 frameCountT, frameCountR, frameCountS;
} SceneAnimJointInfo;

ecs_comp_extern_public(SceneAnimationComp) {
  SceneAnimLayer* layers;
  u32             layerCount;
};

u32 scene_skeleton_root_index(const SceneSkeletonTemplComp*);

u32                       scene_skeleton_joint_count(const SceneSkeletonTemplComp*);
const SceneSkeletonJoint* scene_skeleton_joint(const SceneSkeletonTemplComp*, u32 index);
SceneAnimJointInfo scene_skeleton_anim_info(const SceneSkeletonTemplComp*, u32 anim, u32 joint);

void scene_skeleton_mask_set(SceneSkeletonMask*, u32 joint);
void scene_skeleton_mask_set_all(SceneSkeletonMask*);
void scene_skeleton_mask_clear(SceneSkeletonMask*, u32 joint);
void scene_skeleton_mask_clear_all(SceneSkeletonMask*);
void scene_skeleton_mask_flip(SceneSkeletonMask*, u32 joint);
bool scene_skeleton_mask_test(const SceneSkeletonMask*, u32 joint);

void scene_skeleton_delta(const SceneSkeletonComp*, const SceneSkeletonTemplComp*, GeoMatrix* out);
