#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"

/**
 * Gizmo identifier.
 * Used to track gizmo identity across frames.
 */
typedef u64 DebugGizmoId;

ecs_comp_extern(DebugGizmoComp);

// clang-format off

/**
 * Draw a translation gizmo.
 * Updated translation value is written to the given GeoVector pointer.
 * Returns true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool debug_gizmo_translation(DebugGizmoComp*, DebugGizmoId, GeoVector* translation, GeoQuat rotation);

/**
 * Draw a rotation gizmo.
 * Updated rotation value is written to the given GeoQuat pointer.
 * Returns true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool debug_gizmo_rotation(DebugGizmoComp*, DebugGizmoId, GeoVector translation, GeoQuat* rotation);

/**
 * Draw a uniform scale gizmo.
 * Updated scale value is written to the given f32 pointer.
 * Returns true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool debug_gizmo_scale_uniform(DebugGizmoComp*, DebugGizmoId, GeoVector translation, f32* scale);

// clang-format on
