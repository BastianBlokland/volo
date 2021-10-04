#pragma once
#include "core_annotation.h"
#include "core_string.h"
#include "ecs_comp.h"

// Forward declare from 'ecs_world.h'.
typedef struct sEcsWorld EcsWorld;

typedef u16 EcsViewId;
typedef u16 EcsSystemId;

typedef struct sEcsModuleBuilder EcsModuleBuilder;
typedef struct sEcsViewBuilder   EcsViewBuilder;

typedef void (*EcsModuleInit)(EcsModuleBuilder*);
typedef void (*EcsViewInit)(EcsViewBuilder*);
typedef void (*EcsSystemRoutine)(EcsWorld*);

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
 * ecs_comp_define(PositionComp) {
 *   f32 x, y;
 * };
 * ```
 */
#define ecs_comp_define(_NAME_)                                                                    \
  typedef struct s##_NAME_ _NAME_;                                                                 \
  EcsCompId ecs_comp_id(_NAME_);                                                                   \
  struct s##_NAME_

/**
 * Define a view initialization routine.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_view_define(ApplyCharacterVelocity) {
 *   ecs_filter_with(CharacterComp);
 *   ecs_access_read(VelocityComp);
 *   ecs_access_write(PositionComp);
 * }
 * ```
 */
#define ecs_view_define(_NAME_)                                                                    \
  static EcsViewId ecs_view_id(_NAME_);                                                            \
  static void _ecs_view_init_##_NAME_(MAYBE_UNUSED EcsViewBuilder* _builder)

#define ecs_filter_with(_COMP_)    ecs_module_filter_with(_builder, ecs_comp_id(_COMP_))
#define ecs_filter_without(_COMP_) ecs_module_filter_without(_builder, ecs_comp_id(_COMP_))
#define ecs_access_read(_COMP_)    ecs_module_access_read(_builder, ecs_comp_id(_COMP_))
#define ecs_access_write(_COMP_)   ecs_module_access_write(_builder, ecs_comp_id(_COMP_))

/**
 * Define a system routine.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_system_define(ApplyVelocity) {
 *   EcsView* view = ecs_world_view_t(world, ReadVeloWritePosView);
 *   EcsIterator itr = ecs_view_itr_stack(view);
 *   while(ecs_view_itr_walk(itr)) {
 *     const Velocity* velo = ecs_view_read_t(itr, Velocity);
 *     Position* pos = ecs_view_write_t(itr, Position);
 *     ...
 *   }
 * }
 * ```
 */
#define ecs_system_define(_NAME_)                                                                  \
  static EcsSystemId ecs_system_id(_NAME_);                                                        \
  static void _ecs_system_##_NAME_(MAYBE_UNUSED EcsWorld* world)

/**
 * Register a new component type.
 * NOTE: Can only be used inside a module-init function.
 * NOTE: The component has to be defined using 'ecs_comp_define()' in exactly one compilation unit.
 *
 * Pre-condition: No other component with the same name has been registered already.
 */
#define ecs_register_comp(_NAME_)                                                                  \
  ASSERT(sizeof(_NAME_) != 0, "Components are not allowed to be empty");                           \
  ecs_comp_id(_NAME_) =                                                                            \
      ecs_module_register_comp(_builder, string_lit(#_NAME_), sizeof(_NAME_), alignof(_NAME_))

/**
 * Register a new view.
 * NOTE: Can only be used inside a module-init function.
 * NOTE: The view has to be defined using 'ecs_view_define()' in the same compilation unit.
 */
#define ecs_register_view(_NAME_)                                                                  \
  ecs_view_id(_NAME_) =                                                                            \
      ecs_module_register_view(_builder, string_lit(#_NAME_), &_ecs_view_init_##_NAME_)

/**
 * Register a new system with a list of view-ids as dependencies.
 * NOTE: Can only be used inside a module-init function.
 *
 * Example usage:
 * ```
 * ecs_register_system(ApplyVelocity, ecs_view_id(ReadVeloWritePos));
 * ```
 */
#define ecs_register_system(_NAME_, ...)                                                           \
  ecs_system_id(_NAME_) = ecs_module_register_system(                                              \
      _builder,                                                                                    \
      string_lit(#_NAME_),                                                                         \
      &_ecs_system_##_NAME_,                                                                       \
      (const EcsViewId[]){ VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, 0) }, COUNT_VA_ARGS(__VA_ARGS__))

// clang-format on

/**
 * Compare two EcsViewId's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 ecs_compare_view(const void* a, const void* b);

/**
 * Compare two EcsSystemId's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 ecs_compare_system(const void* a, const void* b);

EcsCompId   ecs_module_register_comp(EcsModuleBuilder*, String name, usize size, usize align);
EcsViewId   ecs_module_register_view(EcsModuleBuilder*, String name, EcsViewInit);
EcsSystemId ecs_module_register_system(
    EcsModuleBuilder*, String name, EcsSystemRoutine, const EcsViewId* views, usize viewCount);

void ecs_module_filter_with(EcsViewBuilder*, EcsCompId);
void ecs_module_filter_without(EcsViewBuilder*, EcsCompId);
void ecs_module_access_read(EcsViewBuilder*, EcsCompId);
void ecs_module_access_write(EcsViewBuilder*, EcsCompId);
