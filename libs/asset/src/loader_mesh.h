#include "asset/mesh.h"

#include "repo.h"

ecs_comp_extern_public(AssetMeshSourceComp) { AssetSource* src; };

typedef struct {
  AssetMeshComp          mesh;
  AssetMeshSkeletonComp* skeleton; // Optional.
} AssetMeshBundle;
