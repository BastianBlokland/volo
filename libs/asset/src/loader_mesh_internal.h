#include "asset_mesh.h"

#include "repo_internal.h"

ecs_comp_extern_public(AssetMeshSourceComp) { AssetSource* src; };

typedef struct {
  AssetMeshComp          mesh;
  AssetMeshSkeletonComp* skeleton; // Optional.
} AssetMeshBundle;
