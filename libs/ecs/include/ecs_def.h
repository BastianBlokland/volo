#pragma once
#include "ecs_module.h"

typedef struct sEcsDef EcsDef;

/**
 * Register a module to a 'EcsDef' definition.
 * Define a module initialization routine using the 'ecs_module_init(name)' macro.
 */
#define ecs_register_module(_DEF_, _NAME_)                                                         \
  void ecs_module_init_name(_NAME_)(EcsModuleBuilder*);                                            \
  ecs_def_register_module(_DEF_, string_lit(#_NAME_), &ecs_module_init_name(_NAME_))

/**
 * Create a new (empty) Ecs definition.
 * Destroy using 'ecs_def_destroy()'.
 */
EcsDef* ecs_def_create(Allocator*);

/**
 * Destroy a Ecs definition.
 */
void ecs_def_destroy(EcsDef*);

/**
 * Register a module to a 'EcsDef' definition.
 *
 * Pre-condition: No other module with the same name has been registered.
 */
EcsModuleId ecs_def_register_module(EcsDef*, String name, EcsModuleInit);

/**
 * Retrieve the name of a module.
 * NOTE: Module names are not required to be unique.
 *
 * Pre-condition: EcsModuleId is a valid module-id registered to the given EcsDef.
 */
String ecs_def_module_name(const EcsDef*, EcsModuleId);

/**
 * Retrieve the amount of registered components / views / systems / modules.
 */
u32 ecs_def_comp_count(const EcsDef*);
u32 ecs_def_view_count(const EcsDef*);
u32 ecs_def_system_count(const EcsDef*);
u32 ecs_def_module_count(const EcsDef*);

/**
 * Retrieve the name of a component.
 *
 * Pre-condition: EcsCompId is a valid component-id registered to the given EcsDef.
 */
String ecs_def_comp_name(const EcsDef*, EcsCompId);

/**
 * Retrieve the size of a component.
 *
 * Pre-condition: EcsCompId is a valid component-id registered to the given EcsDef.
 */
usize ecs_def_comp_size(const EcsDef*, EcsCompId);

/**
 * Retrieve the alignment required of a component.
 *
 * Pre-condition: EcsCompId is a valid component-id registered to the given EcsDef.
 */
usize ecs_def_comp_align(const EcsDef*, EcsCompId);

/**
 * Retrieve the name of a view.
 * NOTE: View names are not required to be unique.
 *
 * Pre-condition: EcsViewId is a valid view-id registered to the given EcsDef.
 */
String ecs_def_view_name(const EcsDef*, EcsViewId);

/**
 * Retrieve the name of a system.
 * NOTE: System names are not required to be unique.
 *
 * Pre-condition: EcsSystemId is a valid system-id registered to the given EcsDef.
 */
String ecs_def_system_name(const EcsDef*, EcsSystemId);

/**
 * Retrieve the configured execution order of a system.
 * NOTE: When multiple systems have the same order the runner is free to reorder them.
 *
 * Pre-condition: EcsSystemId is a valid system-id registered to the given EcsDef.
 */
i32 ecs_def_system_order(const EcsDef*, EcsSystemId);

/**
 * Retrieve the configured parallel count of a system.
 *
 * Pre-condition: EcsSystemId is a valid system-id registered to the given EcsDef.
 */
u32 ecs_def_system_parallel(const EcsDef*, EcsSystemId);

typedef struct {
  EcsViewId* values;
  usize      count;
} EcsDefSystemViews;

/**
 * Retrieve the registered views of a system.
 *
 * Pre-condition: EcsSystemId is a valid system-id registered to the given EcsDef.
 */
EcsDefSystemViews ecs_def_system_views(const EcsDef*, EcsSystemId);

/**
 * Check if the given system has access to the view.
 *
 * Pre-condition: EcsSystemId is a valid system-id registered to the given EcsDef.
 * Pre-condition: EcsViewId is a valid view-id registered to the given EcsDef.
 */
bool ecs_def_system_has_access(const EcsDef*, EcsSystemId, EcsViewId);
