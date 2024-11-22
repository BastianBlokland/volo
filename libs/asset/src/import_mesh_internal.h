#pragma once
#include "asset_mesh.h"

#include "import_internal.h"

typedef struct {
  StringHash nameHash; // Interned in the global string table.
} AssetImportJoint;

typedef struct {
  StringHash nameHash; // Interned in the global string table.
  u32        index;    // Data index, immutable.
  i32        layer;    // Sort order; sorting wil be applied after importing.
  f32        duration, time, speed, weight;
} AssetImportAnim;

typedef struct {
  f32              vertexScale;
  AssetImportJoint joints[asset_mesh_joints_max];
  u32              jointCount;
  AssetImportAnim  anims[asset_mesh_anims_max];
  u32              animCount;
} AssetImportMesh;

bool asset_import_mesh(const AssetImportEnvComp*, String id, AssetImportMesh*);
