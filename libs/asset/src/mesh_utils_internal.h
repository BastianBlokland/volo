#pragma once
#include "asset_mesh.h"
#include "core_alloc.h"

typedef struct sAssetMeshBuilder AssetMeshBuilder;

AssetMeshBuilder* asset_mesh_builder_create(Allocator*, usize maxVertexCount);
void              asset_mesh_builder_destroy(AssetMeshBuilder*);
AssetMeshIndex    asset_mesh_builder_push(AssetMeshBuilder*, AssetMeshVertex);
AssetMeshComp     asset_mesh_create(const AssetMeshBuilder*);

GeoVector asset_mesh_tri_norm(GeoVector a, GeoVector b, GeoVector c);

/**
 * Calculate flat normals based on the vertex positions.
 * NOTE: Does not split vertices at the moment, so if vertices are shared by multiple triangles then
 * the last triangle's normal will be used.
 */
void asset_mesh_compute_flat_normals(AssetMeshBuilder*);

/**
 * Calculate smooth tangents based on the vertex normals and texcoords.
 */
void asset_mesh_compute_tangents(AssetMeshBuilder*);
