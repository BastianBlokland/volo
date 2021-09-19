#pragma once
#include "core_annotation.h"
#include "core_string.h"

/**
 * Identifier of a component. Unique within a Ecs definition.
 */
typedef u16 EcsCompId;

/**
 * Identifier of a view. Unique within a Ecs definition.
 */
typedef u16 EcsViewId;

typedef struct sEcsModuleBuilder EcsModuleBuilder;
typedef struct sEcsViewBuilder   EcsViewBuilder;

/**
 * Module initialization routine.
 * Components, Views, and Systems used by a module should be registed within this routine.
 */
typedef void (*EcsModuleInit)(EcsModuleBuilder*);

/**
 * View initialization routine.
 */
typedef void (*EcsViewInit)(EcsViewBuilder*);

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
 * Variable that stores the EcsCompId for a component with the given name.
 */
#define ecs_comp_id(_NAME_) g_ecs_comp_##_NAME_

/**
 * Variable that stores the EcsViewId for a view with the given name.
 */
#define ecs_view_id(_NAME_) g_ecs_view_##_NAME_

/**
 * Define a component struct and a variable for storing the EcsCompId.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_comp_define(PositionComp, {
 *   f32 x, y;
 * });
 * ```
 */
#define ecs_comp_define(_NAME_, ...)                                                               \
  typedef struct __VA_ARGS__ _NAME_;                                                               \
  EcsCompId ecs_comp_id(_NAME_)

/**
 * Define a view initialization routine and a variable for storing EcsViewId.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_view_define(ApplyVelocityView, {
 *   ecs_view_read(VelocityComp);
 *   ecs_view_write(PositionComp);
 * });
 * ```
 */
#define ecs_view_define(_NAME_, ...)                                                               \
  static void _ecs_view_init_##_NAME_(MAYBE_UNUSED EcsViewBuilder* _builder) __VA_ARGS__           \
  static EcsViewId ecs_view_id(_NAME_)

#define ecs_view_with(_COMP_)        ecs_module_view_with(_builder, ecs_comp_id(_COMP_))
#define ecs_view_without(_COMP_)     ecs_module_view_without(_builder, ecs_comp_id(_COMP_))
#define ecs_view_read(_COMP_)        ecs_module_view_read(_builder, ecs_comp_id(_COMP_))
#define ecs_view_write(_COMP_)       ecs_module_view_write(_builder, ecs_comp_id(_COMP_))
#define ecs_view_maybe_read(_COMP_)  ecs_module_view_maybe_read(_builder, ecs_comp_id(_COMP_))
#define ecs_view_maybe_write(_COMP_) ecs_module_view_maybe_write(_builder, ecs_comp_id(_COMP_))

/**
 * Register a new component type.
 * Note: Can only be used inside a module-init function.
 * Note: The component has to be defined using 'ecs_comp_define()' in exactly one compilation unit.
 *
 * Pre-condition: No other component with the same name has been registered already.
 */
#define ecs_register_comp(_NAME_)                                                                  \
  ecs_comp_id(_NAME_) =                                                                            \
      ecs_module_register_comp(_builder, string_lit(#_NAME_), sizeof(_NAME_), alignof(_NAME_))

/**
 * Register a new view.
 * Note: Can only be used inside a module-init function.
 * Note: The view has to be defined using 'ecs_view_define()' in the same compilation unit.
 */
#define ecs_register_view(_NAME_)                                                                  \
  ecs_view_id(_NAME_) =                                                                            \
      ecs_module_register_view(_builder, string_lit(#_NAME_), &_ecs_view_init_##_NAME_)

// clang-format on

EcsCompId ecs_module_register_comp(EcsModuleBuilder*, String name, usize size, usize align);
EcsViewId ecs_module_register_view(EcsModuleBuilder*, String name, EcsViewInit);

void ecs_module_view_with(EcsViewBuilder*, EcsCompId);
void ecs_module_view_without(EcsViewBuilder*, EcsCompId);
void ecs_module_view_read(EcsViewBuilder*, EcsCompId);
void ecs_module_view_write(EcsViewBuilder*, EcsCompId);
void ecs_module_view_maybe_read(EcsViewBuilder*, EcsCompId);
void ecs_module_view_maybe_write(EcsViewBuilder*, EcsCompId);
