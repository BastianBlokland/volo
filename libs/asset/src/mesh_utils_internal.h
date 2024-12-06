#pragma once
#include "asset_mesh.h"
#include "core_alloc.h"
#include "geo.h"

typedef struct sAssetMeshBuilder AssetMeshBuilder;

typedef struct {
  GeoVector position; // x, y, z position
  GeoVector normal;   // x, y, z normal
  GeoVector tangent;  // z, y, z tangent, w tangent handedness
  GeoVector texcoord; // x, y texcoord0
} AssetMeshVertex;

typedef struct {
  u8        joints[4]; // joint indices.
  GeoVector weights;   // joint weights.
} AssetMeshSkin;

void asset_mesh_vertex_transform(AssetMeshVertex*, const GeoMatrix*);
void asset_mesh_vertex_quantize(AssetMeshVertex*);

AssetMeshBuilder* asset_mesh_builder_create(Allocator*, u32 maxVertexCount);
void              asset_mesh_builder_destroy(AssetMeshBuilder*);
void              asset_mesh_builder_clear(AssetMeshBuilder*);
AssetMeshIndex    asset_mesh_builder_push(AssetMeshBuilder*, const AssetMeshVertex*);
void              asset_mesh_builder_set_skin(AssetMeshBuilder*, AssetMeshIndex, AssetMeshSkin);

AssetMeshComp asset_mesh_create(const AssetMeshBuilder*);

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
