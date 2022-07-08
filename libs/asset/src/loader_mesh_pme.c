#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "log_logger.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * ProceduralMEsh - Procedurally generated mesh.
 */

#define pme_max_subdivisions 400

static DataReg* g_dataReg;
static DataMeta g_dataPmeDefMeta;

typedef enum {
  PmeAxis_Up,
  PmeAxis_Down,
  PmeAxis_Right,
  PmeAxis_Left,
  PmeAxis_Forward,
  PmeAxis_Backward,
} PmeAxis;

typedef enum {
  PmeType_Triangle,
  PmeType_Quad,
  PmeType_Cube,
  PmeType_Capsule,
  PmeType_Cone,
  PmeType_Cylinder,
  PmeType_Hemisphere,
} PmeType;

typedef struct {
  f32 minX, minY, minZ;
  f32 maxX, maxY, maxZ;
} PmeBounds;

typedef struct {
  PmeType    type;
  PmeAxis    axis;
  u32        subdivisions;
  f32        length;
  f32        scaleX, scaleY, scaleZ;
  f32        offsetX, offsetY, offsetZ;
  bool       uncapped;
  PmeBounds* bounds;
} PmeDef;

static void pme_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_enum_t(g_dataReg, PmeType);
    data_reg_const_t(g_dataReg, PmeType, Triangle);
    data_reg_const_t(g_dataReg, PmeType, Quad);
    data_reg_const_t(g_dataReg, PmeType, Cube);
    data_reg_const_t(g_dataReg, PmeType, Capsule);
    data_reg_const_t(g_dataReg, PmeType, Cone);
    data_reg_const_t(g_dataReg, PmeType, Cylinder);
    data_reg_const_t(g_dataReg, PmeType, Hemisphere);

    data_reg_enum_t(g_dataReg, PmeAxis);
    data_reg_const_t(g_dataReg, PmeAxis, Up);
    data_reg_const_t(g_dataReg, PmeAxis, Down);
    data_reg_const_t(g_dataReg, PmeAxis, Right);
    data_reg_const_t(g_dataReg, PmeAxis, Left);
    data_reg_const_t(g_dataReg, PmeAxis, Forward);
    data_reg_const_t(g_dataReg, PmeAxis, Backward);

    data_reg_struct_t(g_dataReg, PmeBounds);
    data_reg_field_t(g_dataReg, PmeBounds, minX, data_prim_t(f32));
    data_reg_field_t(g_dataReg, PmeBounds, minY, data_prim_t(f32));
    data_reg_field_t(g_dataReg, PmeBounds, minZ, data_prim_t(f32));
    data_reg_field_t(g_dataReg, PmeBounds, maxX, data_prim_t(f32));
    data_reg_field_t(g_dataReg, PmeBounds, maxY, data_prim_t(f32));
    data_reg_field_t(g_dataReg, PmeBounds, maxZ, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, PmeDef);
    data_reg_field_t(g_dataReg, PmeDef, type, t_PmeType);
    data_reg_field_t(g_dataReg, PmeDef, axis, t_PmeAxis);
    data_reg_field_t(g_dataReg, PmeDef, subdivisions, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, length, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, scaleX, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PmeDef, scaleY, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PmeDef, scaleZ, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PmeDef, offsetX, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, offsetY, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, offsetZ, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, uncapped, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, bounds, t_PmeBounds, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
    // clang-format on

    g_dataPmeDefMeta = data_meta_t(t_PmeDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef struct {
  const PmeDef*     def;
  AssetMeshBuilder* builder;
  GeoMatrix         transformGlobal, transformLocal;
} PmeGenerator;

static GeoVector pme_def_axis(const PmeDef* def) {
  switch (def->axis) {
  case PmeAxis_Up:
    return geo_up;
  case PmeAxis_Down:
    return geo_down;
  case PmeAxis_Right:
    return geo_right;
  case PmeAxis_Left:
    return geo_left;
  case PmeAxis_Forward:
    return geo_forward;
  case PmeAxis_Backward:
    return geo_backward;
  }
  diag_crash();
}

static f32 pme_def_axis_scale(const PmeDef* def) {
  switch (def->axis) {
  case PmeAxis_Right:
  case PmeAxis_Left:
    return def->scaleX != 0.0f ? math_max(def->scaleX, f32_epsilon) : 1.0f;
  case PmeAxis_Up:
  case PmeAxis_Down:
    return def->scaleY != 0.0f ? math_max(def->scaleY, f32_epsilon) : 1.0f;
  case PmeAxis_Forward:
  case PmeAxis_Backward:
    return def->scaleZ != 0.0f ? math_max(def->scaleZ, f32_epsilon) : 1.0f;
  }
  diag_crash();
}

static u32 pme_max_verts(PmeDef* def) {
  /**
   * Get a conservative maximum amount of needed vertices.
   */
  switch (def->type) {
  case PmeType_Triangle:
    return (def->subdivisions + 1) * (def->subdivisions + 1) * 3;
  case PmeType_Quad:
    return (def->subdivisions + 1) * (def->subdivisions + 1) * 4;
  case PmeType_Cube:
    return (def->subdivisions + 1) * (def->subdivisions + 1) * 4 * 6;
  case PmeType_Capsule:
    return (math_max(4, def->subdivisions) + 2) * (math_max(4, def->subdivisions) + 2) * 4;
  case PmeType_Cone:
    return math_max(4, def->subdivisions) * 2 * 3;
  case PmeType_Cylinder:
    return math_max(4, def->subdivisions) * 4 * 3;
  case PmeType_Hemisphere:
    return (math_max(4, def->subdivisions) + 2) * (math_max(4, def->subdivisions) + 2) * 2;
  }
  diag_crash();
}

static GeoMatrix pme_def_matrix(const PmeDef* def) {
  const GeoMatrix t = geo_matrix_translate(geo_vector(def->offsetX, def->offsetY, def->offsetZ));
  const GeoMatrix r = geo_matrix_rotate_look(pme_def_axis(def), geo_up);
  const GeoMatrix s = geo_matrix_scale(geo_vector(
      def->scaleX != 0.0f ? math_max(def->scaleX, f32_epsilon) : 1.0f,
      def->scaleY != 0.0f ? math_max(def->scaleY, f32_epsilon) : 1.0f,
      def->scaleZ != 0.0f ? math_max(def->scaleZ, f32_epsilon) : 1.0f));

  const GeoMatrix ts = geo_matrix_mul(&t, &s);
  return geo_matrix_mul(&ts, &r);
}

static void pme_push_vert(PmeGenerator* gen, const GeoVector pos, const GeoVector texcoord) {
  const GeoMatrix mat = geo_matrix_mul(&gen->transformGlobal, &gen->transformLocal);
  asset_mesh_builder_push(
      gen->builder,
      (AssetMeshVertex){
          .position = geo_matrix_transform3_point(&mat, pos),
          .texcoord = texcoord,
      });
}

static void pme_push_vert_nrm(
    PmeGenerator* gen, const GeoVector pos, const GeoVector texcoord, const GeoVector normal) {
  const GeoMatrix mat = geo_matrix_mul(&gen->transformGlobal, &gen->transformLocal);
  asset_mesh_builder_push(
      gen->builder,
      (AssetMeshVertex){
          .position = geo_matrix_transform3_point(&mat, pos),
          .texcoord = texcoord,
          .normal   = geo_matrix_transform3(&mat, normal),
      });
}

void pme_push_triangle(PmeGenerator* gen) {
  /**
   * Subdivided triangle.
   *
   *    /\
   *   /\/\
   *  /\/\/\
   * /\/\/\/\
   *
   */
  const u32 numSteps = gen->def->subdivisions + 1;
  const f32 step     = 1.0f / numSteps;
  for (u32 y = numSteps; y-- != 0;) {
    const f32 yMin = (y + 0.0f) * step;
    const f32 yMax = (y + 1.0f) * step;
    for (u32 x = 0; x != (numSteps - y); ++x) {
      const f32 xMin = (x + y * 0.5f + 0.0f) * step;
      const f32 xMid = (x + y * 0.5f + 0.5f) * step;
      const f32 xMax = (x + y * 0.5f + 1.0f) * step;

      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMin - 0.5f), geo_vector(xMax, yMin));
      pme_push_vert(gen, geo_vector(xMid - 0.5f, yMax - 0.5f), geo_vector(xMid, yMax));
      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));

      if (y) {
        /**
         * Fill in the hole in the row below us.
         */
        const f32 yLastRow = yMin - step;
        pme_push_vert(gen, geo_vector(xMid - 0.5f, yLastRow - 0.5f), geo_vector(xMid, yLastRow));
        pme_push_vert(gen, geo_vector(xMax - 0.5f, yMin - 0.5f), geo_vector(xMax, yMin));
        pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
      }
    }
  }
}

void pme_push_quad(PmeGenerator* gen) {
  /**
   * Subdivided quad.
   */
  const u32 numSteps = gen->def->subdivisions + 1;
  const f32 step     = 1.0f / numSteps;
  for (u32 y = 0; y != numSteps; ++y) {
    const f32 yMin = y * step;
    const f32 yMax = yMin + step;
    for (u32 x = 0; x != numSteps; ++x) {
      const f32 xMin = x * step;
      const f32 xMax = xMin + step;

      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMax - 0.5f), geo_vector(xMax, yMax));
      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMax - 0.5f), geo_vector(xMin, yMax));
      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMin - 0.5f), geo_vector(xMax, yMin));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMax - 0.5f), geo_vector(xMax, yMax));
    }
  }
}

static void pme_generate_triangle(PmeGenerator* gen) {
  pme_push_triangle(gen);

  // TODO: Compute the normals and tangents directly instead of these separate passes.
  asset_mesh_compute_flat_normals(gen->builder);
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_quad(PmeGenerator* gen) {
  pme_push_quad(gen);

  // TODO: Compute the normals and tangents directly instead of these separate passes.
  asset_mesh_compute_flat_normals(gen->builder);
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_cube(PmeGenerator* gen) {
  const GeoMatrix faceRotations[] = {
      geo_matrix_rotate_look(geo_up, geo_forward),
      geo_matrix_rotate_look(geo_down, geo_forward),
      geo_matrix_rotate_look(geo_right, geo_up),
      geo_matrix_rotate_look(geo_left, geo_up),
      geo_matrix_rotate_look(geo_forward, geo_up),
      geo_matrix_rotate_look(geo_backward, geo_up),
  };
  array_for_t(faceRotations, GeoMatrix, rotMat) {
    const GeoVector offset = geo_vector_mul(geo_matrix_transform3(rotMat, geo_backward), 0.5f);
    const GeoMatrix t      = geo_matrix_translate(offset);
    gen->transformLocal    = geo_matrix_mul(&t, rotMat);
    pme_push_quad(gen);
  }
  // TODO: Compute the normals and tangents directly instead of these separate passes.
  asset_mesh_compute_flat_normals(gen->builder);
  asset_mesh_compute_tangents(gen->builder);
}

static GeoVector pme_capsule_position(const f32 vAngle, const f32 hAngle, const f32 height) {
  const f32 vSin = math_sin_f32(vAngle), vCos = math_cos_f32(vAngle);
  return geo_vector(
          .x = vCos * math_sin_f32(hAngle),
          .y = vCos * math_cos_f32(hAngle),
          .z = (height * -0.5f) + (vAngle >= 0 ? height + vSin : vSin));
}

static void pme_generate_capsule(PmeGenerator* gen, const f32 height) {
  u32 numSegs = math_max(4, gen->def->subdivisions);
  if (height > 0) {
    // Additional segments for the straight part (1 for even sub-divs and 2 for odd sub-divs).
    numSegs += 1 + numSegs % 2;
  }
  const f32 segStepVer = math_pi_f32 / numSegs;
  const f32 segStepHor = math_pi_f32 * 2.0f / numSegs;
  const f32 invNumSegs = 1.0f / numSegs;
  const f32 radius     = 0.5f;

  /**
   * Generate 2 triangles on each segment (except for the first and last vertical segment).
   * TODO: Pretty inefficient as we generate the same point 4 times (each of the quad corners).
   */

  for (u32 v = 0; v != numSegs; ++v) {
    const f32 vAngleMax = math_pi_f32 * 0.5f - v * segStepVer;
    const f32 vAngleMin = vAngleMax - segStepVer;

    const f32 texYMin = 1.0f - (v + 1.0f) * invNumSegs;
    const f32 texYMax = 1.0f - v * invNumSegs;

    for (u32 h = 0; h != numSegs; ++h) {
      const f32 hAngleMax = h * segStepHor;
      const f32 hAngleMin = hAngleMax - segStepHor;

      const GeoVector posA = pme_capsule_position(vAngleMin, hAngleMin, height);
      const GeoVector posB = pme_capsule_position(vAngleMax, hAngleMin, height);
      const GeoVector posC = pme_capsule_position(vAngleMax, hAngleMax, height);
      const GeoVector posD = pme_capsule_position(vAngleMin, hAngleMax, height);

      const f32 texXMin = h * invNumSegs;
      const f32 texXMax = (h + 1.0f) * invNumSegs;

      if (v) {
        pme_push_vert_nrm(gen, geo_vector_mul(posC, radius), geo_vector(texXMax, texYMax), posC);
        pme_push_vert_nrm(gen, geo_vector_mul(posB, radius), geo_vector(texXMin, texYMax), posB);
        pme_push_vert_nrm(gen, geo_vector_mul(posA, radius), geo_vector(texXMin, texYMin), posA);
      }
      if (v != numSegs - 1) {
        pme_push_vert_nrm(gen, geo_vector_mul(posD, radius), geo_vector(texXMax, texYMin), posD);
        pme_push_vert_nrm(gen, geo_vector_mul(posC, radius), geo_vector(texXMax, texYMax), posC);
        pme_push_vert_nrm(gen, geo_vector_mul(posA, radius), geo_vector(texXMin, texYMin), posA);
      }
    }
  }

  // TODO: Compute the tangents directly instead of this separate pass.
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_cone(PmeGenerator* gen) {
  const u32 numSegs    = math_max(4, gen->def->subdivisions);
  const f32 segStep    = math_pi_f32 * 2.0f / numSegs;
  const f32 invNumSegs = 1.0f / numSegs;
  const f32 radius     = 0.5f;
  for (u32 i = 0; i != numSegs; ++i) {
    const f32 angleRight = i * segStep;
    const f32 angleLeft  = angleRight - segStep;

    const GeoVector leftPos = {math_sin_f32(angleLeft), math_cos_f32(angleLeft), -1};
    const GeoVector leftNrm = {leftPos.x, leftPos.y};
    const GeoVector leftTex = {i * invNumSegs, 0};

    const GeoVector rightPos = {math_sin_f32(angleRight), math_cos_f32(angleRight), -1};
    const GeoVector rightNrm = {rightPos.x, rightPos.y};
    const GeoVector rightTex = {(i + 1.0f) * invNumSegs, 0};

    const GeoVector topTex = {(leftTex.x + rightTex.x) * 0.5f, 1};
    const GeoVector topNrm = geo_vector_norm(
        geo_vector((leftPos.x + rightPos.x) * 0.5f, (leftPos.y + rightPos.y) * 0.5f));

    // Add side triangle.
    pme_push_vert_nrm(gen, geo_vector_mul(rightPos, radius), rightTex, rightNrm);
    pme_push_vert_nrm(gen, geo_vector(0, 0, 0.5f), topTex, topNrm);
    pme_push_vert_nrm(gen, geo_vector_mul(leftPos, radius), leftTex, leftNrm);

    if (!gen->def->uncapped) {
      // Add bottom triangle.
      pme_push_vert_nrm(gen, geo_vector(0, 0, -0.5f), topTex, geo_backward);
      pme_push_vert_nrm(gen, geo_vector_mul(rightPos, radius), rightTex, geo_backward);
      pme_push_vert_nrm(gen, geo_vector_mul(leftPos, radius), leftTex, geo_backward);
    }
  }

  // TODO: Compute the tangents directly instead of this separate pass.
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_cylinder(PmeGenerator* gen) {
  const u32 numSegs    = math_max(4, gen->def->subdivisions);
  const f32 segStep    = math_pi_f32 * 2.0f / numSegs;
  const f32 invNumSegs = 1.0f / numSegs;
  const f32 radius     = 0.5f;
  for (u32 i = 0; i != numSegs; ++i) {
    const f32 angleRight = i * segStep;
    const f32 angleLeft  = angleRight - segStep;
    const f32 leftX = math_sin_f32(angleLeft), leftY = math_cos_f32(angleLeft);
    const f32 rightX = math_sin_f32(angleRight), rightY = math_cos_f32(angleRight);

    const GeoVector leftBottomPos = {leftX, leftY, -1};
    const GeoVector leftTopPos    = {leftX, leftY, 1};
    const GeoVector leftNrm       = {leftX, leftY};
    const GeoVector leftBottomTex = {i * invNumSegs, 0};
    const GeoVector leftTopTex    = {i * invNumSegs, 1};

    const GeoVector rightBottomPos = {rightX, rightY, -1};
    const GeoVector rightTopPos    = {rightX, rightY, 1};
    const GeoVector rightNrm       = {rightX, rightY};
    const GeoVector rightBottomTex = {(i + 1.0f) * invNumSegs, 0};
    const GeoVector rightTopTex    = {(i + 1.0f) * invNumSegs, 1};

    // Add side triangle 1.
    pme_push_vert_nrm(gen, geo_vector_mul(rightBottomPos, radius), rightBottomTex, rightNrm);
    pme_push_vert_nrm(gen, geo_vector_mul(leftTopPos, radius), leftTopTex, leftNrm);
    pme_push_vert_nrm(gen, geo_vector_mul(leftBottomPos, radius), leftBottomTex, leftNrm);

    // Add side triangle 2.
    pme_push_vert_nrm(gen, geo_vector_mul(rightBottomPos, radius), rightBottomTex, rightNrm);
    pme_push_vert_nrm(gen, geo_vector_mul(rightTopPos, radius), rightTopTex, rightNrm);
    pme_push_vert_nrm(gen, geo_vector_mul(leftTopPos, radius), leftTopTex, leftNrm);

    if (!gen->def->uncapped) {
      // Add top triangle.
      const GeoVector centerTopTex = {(leftTopTex.x + rightTopTex.x) * 0.5f, 1};
      pme_push_vert_nrm(gen, geo_vector_mul(rightTopPos, radius), rightTopTex, geo_forward);
      pme_push_vert_nrm(gen, geo_vector(0, 0, 0.5f), centerTopTex, geo_forward);
      pme_push_vert_nrm(gen, geo_vector_mul(leftTopPos, radius), leftTopTex, geo_forward);

      // Add bottom triangle.
      const GeoVector centerBottomTex = {(leftBottomTex.x + rightBottomTex.x) * 0.5f, 0};
      pme_push_vert_nrm(gen, geo_vector(0, 0, -0.5f), centerBottomTex, geo_backward);
      pme_push_vert_nrm(gen, geo_vector_mul(rightBottomPos, radius), rightBottomTex, geo_backward);
      pme_push_vert_nrm(gen, geo_vector_mul(leftBottomPos, radius), leftBottomTex, geo_backward);
    }
  }

  // TODO: Compute the tangents directly instead of this separate pass.
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_hemisphere(PmeGenerator* gen) {
  const u32 numSegsHor    = math_max(4, gen->def->subdivisions);
  const u32 numSegsVer    = numSegsHor / 2;
  const f32 segStepVer    = math_pi_f32 * 0.5f / numSegsVer;
  const f32 segStepHor    = math_pi_f32 * 2.0f / numSegsHor;
  const f32 invNumSegsHor = 1.0f / numSegsHor;
  const f32 invNumSegsVer = 1.0f / numSegsVer;
  const f32 radius        = 0.5f;

  /**
   * Generate 2 triangles on each segment (except for the first) and an additional bottom one.
   * TODO: Pretty inefficient as we generate the same point 4 times (each of the quad corners).
   */

  for (u32 v = 0; v != numSegsVer; ++v) {
    const f32 vAngleMax = math_pi_f32 * 0.5f - v * segStepVer;
    const f32 vAngleMin = vAngleMax - segStepVer;

    const f32 texYMin = 1.0f - (v + 1.0f) * invNumSegsVer;
    const f32 texYMax = 1.0f - v * invNumSegsVer;

    for (u32 h = 0; h != numSegsHor; ++h) {
      const f32 hAngleMax = h * segStepHor;
      const f32 hAngleMin = hAngleMax - segStepHor;

      const GeoVector posA = pme_capsule_position(vAngleMin, hAngleMin, 0);
      const GeoVector posB = pme_capsule_position(vAngleMax, hAngleMin, 0);
      const GeoVector posC = pme_capsule_position(vAngleMax, hAngleMax, 0);
      const GeoVector posD = pme_capsule_position(vAngleMin, hAngleMax, 0);

      const f32 texXMin = h * invNumSegsHor;
      const f32 texXMax = (h + 1.0f) * invNumSegsHor;

      if (v) {
        pme_push_vert_nrm(gen, geo_vector_mul(posC, radius), geo_vector(texXMax, texYMax), posC);
        pme_push_vert_nrm(gen, geo_vector_mul(posB, radius), geo_vector(texXMin, texYMax), posB);
        pme_push_vert_nrm(gen, geo_vector_mul(posA, radius), geo_vector(texXMin, texYMin), posA);
      }
      pme_push_vert_nrm(gen, geo_vector_mul(posD, radius), geo_vector(texXMax, texYMin), posD);
      pme_push_vert_nrm(gen, geo_vector_mul(posC, radius), geo_vector(texXMax, texYMax), posC);
      pme_push_vert_nrm(gen, geo_vector_mul(posA, radius), geo_vector(texXMin, texYMin), posA);

      if ((v == numSegsVer - 1) && !gen->def->uncapped) {
        // Add bottom triangle.
        const GeoVector nrm = geo_backward;
        pme_push_vert_nrm(gen, geo_vector(0), geo_vector((texXMin + texXMax) * 0.5f, texYMin), nrm);
        pme_push_vert_nrm(gen, geo_vector_mul(posD, radius), geo_vector(texXMax, texYMin), nrm);
        pme_push_vert_nrm(gen, geo_vector_mul(posA, radius), geo_vector(texXMin, texYMin), nrm);
      }
    }
  }

  // TODO: Compute the tangents directly instead of this separate pass.
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate(PmeGenerator* gen) {
  switch (gen->def->type) {
  case PmeType_Triangle:
    pme_generate_triangle(gen);
    break;
  case PmeType_Quad:
    pme_generate_quad(gen);
    break;
  case PmeType_Cube:
    pme_generate_cube(gen);
    break;
  case PmeType_Capsule: {
    const f32 height = 1.0f / pme_def_axis_scale(gen->def) * gen->def->length;
    pme_generate_capsule(gen, height);
  } break;
  case PmeType_Cone:
    pme_generate_cone(gen);
    break;
  case PmeType_Cylinder:
    pme_generate_cylinder(gen);
    break;
  case PmeType_Hemisphere:
    pme_generate_hemisphere(gen);
    break;
  }
}

typedef enum {
  PmeError_None = 0,
  PmeError_TooManySubdivisions,

  PmeError_Count,
} PmeError;

static String pme_error_str(const PmeError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Pme specifies more subdivisions then are supported"),
  };
  ASSERT(array_elems(g_msgs) == PmeError_Count, "Incorrect number of pme-error messages");
  return g_msgs[err];
}

void asset_load_pme(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  pme_datareg_init();

  String            errMsg;
  AssetMeshBuilder* builder = null;
  PmeDef            def;
  DataReadResult    result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataPmeDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(def.subdivisions > pme_max_subdivisions)) {
    errMsg = pme_error_str(PmeError_TooManySubdivisions);
    goto Error;
  }

  builder = asset_mesh_builder_create(g_alloc_heap, pme_max_verts(&def));
  pme_generate(&(PmeGenerator){
      .def             = &def,
      .builder         = builder,
      .transformGlobal = pme_def_matrix(&def),
      .transformLocal  = geo_matrix_ident(),
  });

  if (def.bounds) {
    const GeoBox box = (GeoBox){
        .min = geo_vector(def.bounds->minX, def.bounds->minY, def.bounds->minZ),
        .max = geo_vector(def.bounds->maxX, def.bounds->maxY, def.bounds->maxZ),
    };
    asset_mesh_builder_override_bounds(builder, box);
  }

  *ecs_world_add_t(world, entity, AssetMeshComp) = asset_mesh_create(builder);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Done;

Error:
  log_e("Failed to load pme mesh", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Done:
  if (builder) {
    asset_mesh_builder_destroy(builder);
  }
  data_destroy(g_dataReg, g_alloc_heap, g_dataPmeDefMeta, mem_var(def));
  asset_repo_source_close(src);
}
