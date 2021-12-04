#pragma once
#include "asset_mesh.h"
#include "core_alloc.h"

typedef struct sAssetMeshBuilder AssetMeshBuilder;

AssetMeshBuilder* asset_mesh_builder_create(Allocator*, usize maxVertexCount);
void              asset_mesh_builder_destroy(AssetMeshBuilder*);
u16               asset_mesh_builder_push(AssetMeshBuilder*, const AssetMeshVertex*);
AssetMeshComp     asset_mesh_create(const AssetMeshBuilder*);
