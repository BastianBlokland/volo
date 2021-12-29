#pragma once
#include "asset_mesh.h"
#include "core_alloc.h"

typedef struct sAssetMeshBuilder AssetMeshBuilder;

AssetMeshBuilder* asset_mesh_builder_create(Allocator*, usize maxVertexCount);
void              asset_mesh_builder_destroy(AssetMeshBuilder*);
AssetMeshIndex    asset_mesh_builder_push(AssetMeshBuilder*, AssetMeshVertex);
AssetMeshComp     asset_mesh_create(const AssetMeshBuilder*);

/**
 * Calculate smooth tangents based on the vertex normals and texcoords.
 */
void asset_mesh_compute_tangents(AssetMeshBuilder*);
