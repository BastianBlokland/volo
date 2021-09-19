#pragma once
#include "core_annotation.h"
#include "core_string.h"

typedef u16 EcsCompId;
typedef u16 EcsViewId;
typedef u16 EcsSystemId;

typedef struct sEcsModuleBuilder EcsModuleBuilder;
typedef struct sEcsViewBuilder   EcsViewBuilder;

typedef void (*EcsModuleInit)(EcsModuleBuilder*);
typedef void (*EcsViewInit)(EcsViewBuilder*);
typedef void (*EcsSystem)();

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
 * Variable that stores the EcsSystemId for a system with the given name.
 */
#define ecs_system_id(_NAME_) g_ecs_system_##_NAME_

/**
 * Define a component struct.
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
  _Static_assert(sizeof(_NAME_) != 0, "Components are not allowed to be empty");                   \
  EcsCompId ecs_comp_id(_NAME_)

/**
 * Define a view initialization routine.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_view_define(ApplyVelocity, {
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
 * Define a system routine.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_system_define(ApplyVelocity, {
 *   TODO:
 * });
 * ```
 */
#define ecs_system_define(_NAME_, ...)                                                             \
  static void _ecs_system_##_NAME_() __VA_ARGS__                                                   \
  static EcsSystemId ecs_system_id(_NAME_)

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

/**
 * Register a new system.
 * Note: Can only be used inside a module-init function.
 */
#define ecs_register_system(_NAME_)                                                                \
  ecs_system_id(_NAME_) =                                                                          \
    ecs_module_register_system(_builder, string_lit(#_NAME_), &_ecs_system_##_NAME_)

// clang-format on

EcsCompId   ecs_module_register_comp(EcsModuleBuilder*, String name, usize size, usize align);
EcsViewId   ecs_module_register_view(EcsModuleBuilder*, String name, EcsViewInit);
EcsSystemId ecs_module_register_system(EcsModuleBuilder*, String name, EcsSystem);

void ecs_module_view_with(EcsViewBuilder*, EcsCompId);
void ecs_module_view_without(EcsViewBuilder*, EcsCompId);
void ecs_module_view_read(EcsViewBuilder*, EcsCompId);
void ecs_module_view_write(EcsViewBuilder*, EcsCompId);
void ecs_module_view_maybe_read(EcsViewBuilder*, EcsCompId);
void ecs_module_view_maybe_write(EcsViewBuilder*, EcsCompId);
