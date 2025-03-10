#pragma once
#include "asset_mesh.h"
#include "geo_quat.h"

#include "import_internal.h"

typedef struct {
  StringHash nameHash;    // Interned in the global string table.
  u32        parentIndex; // Same as own index for the root joint.
} AssetImportJoint;

typedef struct {
  StringHash         nameHash; // Interned in the global string table.
  u32                index;    // Data index, immutable.
  i32                layer;    // Sort order; sorting wil be applied after importing.
  AssetMeshAnimFlags flags;
  f32                duration, time, speed, speedVariance, weight;
  f32                mask[asset_mesh_joints_max];
} AssetImportAnim;

typedef struct {
  bool flatNormals;

  GeoVector vertexTranslation;
  GeoQuat   vertexRotation;
  GeoVector vertexScale;

  GeoVector rootTranslation;
  GeoQuat   rootRotation;
  GeoVector rootScale;

  AssetImportJoint joints[asset_mesh_joints_max]; // Guaranteed to be topologically sorted.
  u32              jointCount;

  AssetImportAnim anims[asset_mesh_anims_max];
  u32             animCount;
} AssetImportMesh;

bool asset_import_mesh(const AssetImportEnvComp*, String id, AssetImportMesh*);
