#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"

/**
 * Gizmo identifier.
 * Used to track gizmo identity across frames.
 */
typedef u32 DebugGizmoId;

ecs_comp_extern(DebugGizmoComp);

/**
 * Create a gizmo-id for the given entity.
 * NOTE: Only unique between existing entities.
 */
DebugGizmoId debug_gizmo_id_entity(EcsEntityId);

// clang-format off

/**
 * Draw a translation gizmo.
 * Updated translation value is written to the given GeoVector pointer.
 * Return true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool debug_gizmo_translation(DebugGizmoComp*, DebugGizmoId, GeoVector* translation, GeoQuat rotation);

// clang-format on
