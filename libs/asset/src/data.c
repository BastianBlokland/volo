#include "asset_manager.h"
#include "asset_ref.h"
#include "core.h"
#include "core_math.h"
#include "core_thread.h"
#include "data_registry.h"
#include "data_utils.h"
#include "geo_box_rotated.h"
#include "geo_capsule.h"
#include "geo_color.h"
#include "geo_line.h"
#include "geo_matrix.h"
#include "geo_plane.h"
#include "geo_quat.h"
#include "geo_sphere.h"
#include "geo_vector.h"

#include "data_internal.h"

typedef union {
  struct {
    f32 x, y;
  };
  ALIGNAS(16) f32 comps[4];
} GeoVector2;

ASSERT(sizeof(GeoVector2) == sizeof(GeoVector), "Invalid vector size")
ASSERT(alignof(GeoVector2) == alignof(GeoVector), "Invalid vector alignment")

typedef union {
  struct {
    f32 x, y, z;
  };
  ALIGNAS(16) f32 comps[4];
} GeoVector3;

ASSERT(sizeof(GeoVector3) == sizeof(GeoVector), "Invalid vector size")
ASSERT(alignof(GeoVector3) == alignof(GeoVector), "Invalid vector alignment")

typedef union {
  struct {
    f32 x, y, z, w;
  };
  ALIGNAS(16) f32 comps[4];
} GeoVector4;

ASSERT(sizeof(GeoVector4) == sizeof(GeoVector), "Invalid vector size")
ASSERT(alignof(GeoVector4) == alignof(GeoVector), "Invalid vector alignment")

DataType g_assetRefType;
DataType g_assetGeoColorType;
DataType g_assetGeoVec2Type, g_assetGeoVec3Type, g_assetGeoVec4Type;
DataType g_assetGeoQuatType;
DataType g_assetGeoBoxType, g_assetGeoBoxRotatedType;
DataType g_assetGeoLineType;
DataType g_assetGeoSphereType;
DataType g_assetGeoCapsuleType;
DataType g_assetGeoMatrixType;
DataType g_assetGeoPlaneType;

static bool asset_data_normalizer_quat(const Mem data) {
  GeoQuat* quat = mem_as_t(data, GeoQuat);
  *quat         = geo_quat_norm_or_ident(*quat);
  return true;
}

static bool asset_data_normalizer_box(const Mem data) {
  GeoBox* box = mem_as_t(data, GeoBox);
  box->min    = geo_vector_min(box->min, box->max);
  box->max    = geo_vector_max(box->min, box->max);
  return true;
}

static bool asset_data_normalizer_box_rotated(const Mem data) {
  GeoBoxRotated* boxRot = mem_as_t(data, GeoBoxRotated);
  boxRot->box.min       = geo_vector_min(boxRot->box.min, boxRot->box.max);
  boxRot->box.max       = geo_vector_max(boxRot->box.min, boxRot->box.max);
  return true;
}

static bool asset_data_normalizer_sphere(const Mem data) {
  GeoSphere* sphere = mem_as_t(data, GeoSphere);
  sphere->radius    = math_max(sphere->radius, 0.0f);
  return true;
}

static bool asset_data_normalizer_capsule(const Mem data) {
  GeoCapsule* capsule = mem_as_t(data, GeoCapsule);
  capsule->radius     = math_max(capsule->radius, 0.0f);
  return true;
}

static bool asset_data_normalizer_matrix(const Mem data) {
  GeoMatrix* matrix      = mem_as_t(data, GeoMatrix);
  const f32  determinant = geo_matrix_determinant(matrix);
  return determinant != 0.0f;
}

static bool asset_data_normalizer_plane(const Mem data) {
  GeoPlane* plane = mem_as_t(data, GeoPlane);
  plane->normal   = geo_vector_norm_or(plane->normal, geo_up);
  return true;
}

static void asset_data_init_types(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetRef);
  data_reg_field_t(g_dataReg, AssetRef, id, data_prim_t(String), .flags = DataFlags_NotEmpty | DataFlags_InlineField);
  data_reg_comment_t(g_dataReg, AssetRef, "Asset reference");

  data_reg_struct_t(g_dataReg, GeoColor);
  data_reg_field_t(g_dataReg, GeoColor, r, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoColor, g, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoColor, b, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoColor, a, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoColor, "HDR Color");

  data_reg_struct_t(g_dataReg, GeoVector2);
  data_reg_field_t(g_dataReg, GeoVector2, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector2, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoVector2, "2D Vector");

  data_reg_struct_t(g_dataReg, GeoVector3);
  data_reg_field_t(g_dataReg, GeoVector3, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector3, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector3, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoVector3, "3D Vector");

  data_reg_struct_t(g_dataReg, GeoVector4);
  data_reg_field_t(g_dataReg, GeoVector4, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector4, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector4, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector4, w, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoVector4, "4D Vector");

  data_reg_struct_t(g_dataReg, GeoQuat);
  data_reg_field_t(g_dataReg, GeoQuat, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, w, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, GeoQuat, asset_data_normalizer_quat);
  data_reg_comment_t(g_dataReg, GeoQuat, "Quaternion");

  data_reg_struct_t(g_dataReg, GeoBox);
  data_reg_field_t(g_dataReg, GeoBox, min, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoBox, max, t_GeoVector3);
  data_reg_normalizer_t(g_dataReg, GeoBox, asset_data_normalizer_box);
  data_reg_comment_t(g_dataReg, GeoBox, "3D Axis-Aligned Box");

  data_reg_struct_t(g_dataReg, GeoBoxRotated);
  data_reg_field_t(g_dataReg, GeoBoxRotated, box.min, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoBoxRotated, box.max, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoBoxRotated, rotation, t_GeoQuat, .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, GeoBoxRotated, asset_data_normalizer_box_rotated);
  data_reg_comment_t(g_dataReg, GeoBoxRotated, "3D Rotated Box");

  data_reg_struct_t(g_dataReg, GeoLine);
  data_reg_field_t(g_dataReg, GeoLine, a, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoLine, b, t_GeoVector3);
  data_reg_comment_t(g_dataReg, GeoLine, "3D Line");

  data_reg_struct_t(g_dataReg, GeoSphere);
  data_reg_field_t(g_dataReg, GeoSphere, point, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoSphere, radius, data_prim_t(f32));
  data_reg_normalizer_t(g_dataReg, GeoSphere, asset_data_normalizer_sphere);
  data_reg_comment_t(g_dataReg, GeoSphere, "3D Sphere");

  data_reg_struct_t(g_dataReg, GeoCapsule);
  data_reg_field_t(g_dataReg, GeoCapsule, line.a, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoCapsule, line.b, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoCapsule, radius, data_prim_t(f32));
  data_reg_normalizer_t(g_dataReg, GeoCapsule, asset_data_normalizer_capsule);
  data_reg_comment_t(g_dataReg, GeoCapsule, "3D Capsule");

  data_reg_struct_t(g_dataReg, GeoMatrix);
  data_reg_field_t(g_dataReg, GeoMatrix, columns, t_GeoVector4, .container = DataContainer_InlineArray, .fixedCount = 4);
  data_reg_normalizer_t(g_dataReg, GeoMatrix, asset_data_normalizer_matrix);
  data_reg_comment_t(g_dataReg, GeoMatrix, "3D Matrix");

  data_reg_struct_t(g_dataReg, GeoPlane);
  data_reg_field_t(g_dataReg, GeoPlane, normal, t_GeoVector3);
  data_reg_field_t(g_dataReg, GeoPlane, distance, data_prim_t(f32));
  data_reg_normalizer_t(g_dataReg, GeoPlane, asset_data_normalizer_plane);
  data_reg_comment_t(g_dataReg, GeoPlane, "3D Plane");

  // clang-format on

  g_assetRefType           = t_AssetRef;
  g_assetGeoColorType      = t_GeoColor;
  g_assetGeoVec2Type       = t_GeoVector2;
  g_assetGeoVec3Type       = t_GeoVector3;
  g_assetGeoVec4Type       = t_GeoVector4;
  g_assetGeoQuatType       = t_GeoQuat;
  g_assetGeoBoxType        = t_GeoBox;
  g_assetGeoBoxRotatedType = t_GeoBoxRotated;
  g_assetGeoSphereType     = t_GeoSphere;
  g_assetGeoLineType       = t_GeoLine;
  g_assetGeoCapsuleType    = t_GeoCapsule;
  g_assetGeoMatrixType     = t_GeoMatrix;
  g_assetGeoPlaneType      = t_GeoPlane;
}

void asset_data_init(void) {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (g_init) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_init) {
    // Generic types.
    asset_data_init_types();

    // Shared types (need to be first as other types can depend on these).
    asset_data_init_tex();

    // Other types (order does not matter).
    asset_data_init_arraytex();
    asset_data_init_atlas();
    asset_data_init_cache();
    asset_data_init_decal();
    asset_data_init_fonttex();
    asset_data_init_graphic();
    asset_data_init_icon();
    asset_data_init_import_mesh();
    asset_data_init_import_texture();
    asset_data_init_inputmap();
    asset_data_init_level();
    asset_data_init_mesh();
    asset_data_init_prefab();
    asset_data_init_procmesh();
    asset_data_init_proctex();
    asset_data_init_product();
    asset_data_init_script_scene();
    asset_data_init_script();
    asset_data_init_shader();
    asset_data_init_sound();
    asset_data_init_terrain();
    asset_data_init_vfx();
    asset_data_init_weapon();

    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* manager;
} AssetDataPatchCtx;

static void asset_data_patch_refs_visitor(void* ctx, const Mem data) {
  AssetDataPatchCtx* patchCtx = ctx;
  AssetRef*          refData  = mem_as_t(data, AssetRef);
  refData->entity             = asset_maybe_lookup(patchCtx->world, patchCtx->manager, refData->id);
}

void asset_data_patch_refs(
    EcsWorld* world, AssetManagerComp* manager, const DataMeta meta, const Mem data) {
  AssetDataPatchCtx ctx = {
      .world   = world,
      .manager = manager,
  };
  data_visit(g_dataReg, meta, data, g_assetRefType, &ctx, asset_data_patch_refs_visitor);
}
