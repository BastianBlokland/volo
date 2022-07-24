#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"

/**
 * Gizmo identifier.
 * Used to track gizmo identity across frames.
 */
typedef u64 GizmoId;

ecs_comp_extern(DebugGizmoComp);

/**
 * Add a new debug-gizmo component to the given entity.
 */
DebugGizmoComp* debug_shape_create(EcsWorld*, EcsEntityId entity);

/**
 * Draw a translation gizmo.
 * Updated translation value is written to the given GeoVector pointer.
 * Return true if the gizmo is currently being used.
 * NOTE: Pass a stable GizmoId to track edits across frames.
 */
bool debug_gizmo_translate(DebugGizmoComp*, GizmoId, GeoVector* translation, GeoQuat rotation);
