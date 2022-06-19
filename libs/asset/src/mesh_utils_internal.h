#pragma once
#include "asset_mesh.h"
#include "core_alloc.h"

typedef struct sAssetMeshBuilder AssetMeshBuilder;

AssetMeshBuilder* asset_mesh_builder_create(Allocator*, usize maxVertexCount);
void              asset_mesh_builder_destroy(AssetMeshBuilder*);
void              asset_mesh_builder_clear(AssetMeshBuilder*);
AssetMeshIndex    asset_mesh_builder_push(AssetMeshBuilder*, AssetMeshVertex);
void              asset_mesh_builder_set_skin(AssetMeshBuilder*, AssetMeshIndex, AssetMeshSkin);
void              asset_mesh_builder_override_bounds(AssetMeshBuilder*, GeoBox);
AssetMeshComp     asset_mesh_create(const AssetMeshBuilder*);

GeoVector asset_mesh_tri_norm(GeoVector a, GeoVector b, GeoVector c);

/**
 * Calculate flat normals based on the vertex positions.
 * NOTE: Potentially needs to split vertices, meaning it has to rebuild the index mapping.
 */
void asset_mesh_compute_flat_normals(AssetMeshBuilder*);

/**
 * Calculate smooth tangents based on the vertex normals and texcoords.
 */
void asset_mesh_compute_tangents(AssetMeshBuilder*);
