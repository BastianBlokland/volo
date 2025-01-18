#pragma once
#include "dev.h"
#include "geo_quat.h"

/**
 * Gizmo identifier.
 * Used to track gizmo identity across frames.
 */
typedef u64 DebugGizmoId;

ecs_comp_extern(DevGizmoComp);

/**
 * Check if the given gizmo is being interacted with.
 */
bool dev_gizmo_interacting(const DevGizmoComp*, DebugGizmoId);

// clang-format off

/**
 * Draw a translation gizmo.
 * Updated translation value is written to the given GeoVector pointer.
 * Returns true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool dev_gizmo_translation(DevGizmoComp*, DebugGizmoId, GeoVector* translation, GeoQuat rotation);

/**
 * Draw a rotation gizmo.
 * Updated rotation value is written to the given GeoQuat pointer.
 * Returns true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool dev_gizmo_rotation(DevGizmoComp*, DebugGizmoId, GeoVector translation, GeoQuat* rotation);

/**
 * Draw a uniform scale gizmo.
 * Updated scale value is written to the given f32 pointer.
 * Returns true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool dev_gizmo_scale_uniform(DevGizmoComp*, DebugGizmoId, GeoVector translation, f32* scale);

// clang-format on
