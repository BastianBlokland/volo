#pragma once
#include "core_string.h"

/**
 * Identifier of a component. Unique within a Ecs definition.
 */
typedef u16 EcsCompId;

typedef struct sEcsModuleBuilder EcsModuleBuilder;

/**
 * Module initialization routine.
 * Components, Views, and Systems used by a module should be registed within this routine.
 */
typedef void (*EcsModuleInit)(EcsModuleBuilder*);

// clang-format off

/**
 * Retrieve the name of a module initialization routine.
 */
#define ecs_module_init_name(_NAME_) _ecs_module_init_##_NAME_

/**
 * Define a module initialization function.
 * Each module should have exactly one initialization function.
 */
#define ecs_module_init(_NAME_)                                                                    \
  void ecs_module_init_name(_NAME_)(MAYBE_UNUSED EcsModuleBuilder* _builder)

/**
 * Variable that stores the EcsCompId for the component with the given name.
 */
#define ecs_comp_id(_NAME_) g_ecs_comp_##_NAME_

/**
 * Define a component struct and a variable for storing the EcsCompId.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_comp_define(Health, {
 *   u32  hitPoints;
 *   bool invulnerable;
 * });
 * ```
 */
#define ecs_comp_define(_NAME_, ...)                                                               \
  typedef struct __VA_ARGS__ _NAME_;                                                               \
  EcsCompId ecs_comp_id(_NAME_)

/**
 * Register a new component type.
 * Note: Can only be used inside a module-init function.
 * Note: The component has to be defined using 'ecs_comp_define()' in one compilation unit.
 *
 * Pre-condition: No other component with the same name has been registered already.
 */
#define ecs_register_comp(_NAME_)                                                                  \
  ecs_comp_id(_NAME_) =                                                                            \
      ecs_module_register_comp_id(_builder, string_lit(#_NAME_), sizeof(_NAME_), alignof(_NAME_))

// clang-format on

EcsCompId ecs_module_register_comp_id(EcsModuleBuilder*, String name, usize size, usize align);
