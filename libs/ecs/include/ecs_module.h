#pragma once
#include "core.h"
#include "core_macro.h"
#include "core_string.h"
#include "ecs_comp.h"

// Forward declare from 'ecs_world.h'.
typedef struct sEcsWorld EcsWorld;

// Forward declare from 'jobs_executor.h'.
extern u16 g_jobsWorkerCount;

typedef u16 EcsModuleId;
typedef u16 EcsViewId;
typedef u16 EcsSystemId;

typedef struct sEcsModuleBuilder EcsModuleBuilder;
typedef struct sEcsViewBuilder   EcsViewBuilder;

typedef void (*EcsModuleInit)(EcsModuleBuilder*);
typedef void (*EcsViewInit)(EcsViewBuilder*);
typedef void (*EcsSystemRoutine)(EcsWorld*, u16 parCount, u16 parIndex);
typedef void (*EcsCompDestructor)(void*);
typedef void (*EcsCompCombinator)(void*, void*);

typedef struct {
  String            name; // Has to be persistently allocated.
  usize             size, align;
  EcsCompDestructor destructor;
  i32               destructOrder; // Respected per-entity mid-frame and globally on shutdown.
  EcsCompCombinator combinator;
} EcsCompConfig;

typedef struct {
  String      name; // Has to be persistently allocated.
  EcsViewInit initRoutine;
} EcsViewConfig;

typedef enum {
  EcsSystemFlags_None = 0,

  /**
   * The system should always be run on the same thread.
   * NOTE: Incurs an additional scheduling overhead.
   */
  EcsSystemFlags_ThreadAffinity = 1 << 0,

  /**
   * No other systems are allowed to run in parallel with this system.
   */
  EcsSystemFlags_Exclusive = 1 << 1,

} EcsSystemFlags;

typedef enum {
  EcsViewFlags_None = 0,

  /**
   * Indicates at this view is only used to access entities that are exclusively managed by this
   * view. Multiple exclusive views are not allowed to access the same entity, which allows two
   * systems with exclusive views that would otherwise conflict to run in parallel.
   */
  EcsViewFlags_Exclusive = 1 << 0,

  /**
   * Allow parallel systems to construct random-write iterators over this view.
   * By default the Ecs will disallow this because its unsafe, only disable this if you can
   * guarantee the access is synchronized through some external mechanism.
   */
  EcsViewFlags_AllowParallelRandomWrite = 1 << 1,

} EcsViewFlags;

typedef struct {
  String           name; // Has to be persistently allocated.
  EcsSystemRoutine routine;
  EcsSystemFlags   flags;
  const EcsViewId* views;
  usize            viewCount;
} EcsSystemConfig;

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
#define ecs_system_id(_NAME_) g_ecs_sys_##_NAME_

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
 * NOTE: Can optionally be declared using ecs_comp_extern().
 */
#define ecs_comp_define(_NAME_)                                                                    \
  typedef struct s##_NAME_ _NAME_;                                                                 \
  EcsCompId ecs_comp_id(_NAME_);                                                                   \
  struct s##_NAME_

/**
 * Declare an external component struct.
 *
 * Example usage:
 * ```
 * ecs_comp_extern(PositionComp);
 * ```
 * NOTE: Needs to be defined using ecs_comp_define().
 */
#define ecs_comp_extern(_NAME_)                                                                    \
  typedef struct s##_NAME_ _NAME_;                                                                 \
  extern EcsCompId ecs_comp_id(_NAME_)

/**
 * Define a public component struct.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_comp_define_public(PositionComp);
 * ```
 * NOTE: The component body should be declared using ecs_comp_extern_public().
 */
#define ecs_comp_define_public(_NAME_)                                                             \
  typedef struct s##_NAME_ _NAME_;                                                                 \
  EcsCompId ecs_comp_id(_NAME_);                                                                   \
  struct s##_NAME_

/**
 * Declare a external public component struct
 *
 * Example usage:
 * ```
 * ecs_comp_extern_public(PositionComp) {
 *   f32 x, y;
 * };
 * NOTE: Needs to be defined using ecs_comp_define_public().
 * ```
 */
#define ecs_comp_extern_public(_NAME_)                                                             \
  typedef struct s##_NAME_ _NAME_;                                                                 \
  extern EcsCompId ecs_comp_id(_NAME_);                                                            \
  struct s##_NAME_

/**
 * Define a view initialization routine.
 * Should only be used inside compilation-units.
 *
 * Example usage:
 * ```
 * ecs_view_define(ApplyCharacterVelocityView) {
 *   ecs_access_with(CharacterComp);
 *   ecs_access_read(VelocityComp);
 *   ecs_access_write(PositionComp);
 * }
 * ```
 */
#define ecs_view_define(_NAME_)                                                                    \
  static EcsViewId ecs_view_id(_NAME_);                                                            \
  static void _ecs_view_init_##_NAME_(MAYBE_UNUSED EcsViewBuilder* _builder)

#define ecs_view_flags(_FLAGS_)         ecs_module_view_flags(_builder, (_FLAGS_))
#define ecs_access_with(_COMP_)         ecs_module_access_with(_builder, ecs_comp_id(_COMP_))
#define ecs_access_without(_COMP_)      ecs_module_access_without(_builder, ecs_comp_id(_COMP_))
#define ecs_access_read(_COMP_)         ecs_module_access_read(_builder, ecs_comp_id(_COMP_))
#define ecs_access_write(_COMP_)        ecs_module_access_write(_builder, ecs_comp_id(_COMP_))
#define ecs_access_maybe_read(_COMP_)   ecs_module_access_maybe_read(_builder, ecs_comp_id(_COMP_))
#define ecs_access_maybe_write(_COMP_)  ecs_module_access_maybe_write(_builder, ecs_comp_id(_COMP_))

/**
 * Define a system routine.
 * Should only be used inside compilation-units.
 *
 * 'parCount' and 'parIndex' are provided to the system for parallel systems to execute different
 * work on each parallel invocation.
 *
 * Example usage:
 * ```
 * ecs_system_define(ApplyVelocitySys) {
 *   EcsView* view = ecs_world_view_t(world, ReadVeloWritePosView);
 *   for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
 *     const Velocity* velo = ecs_view_read_t(itr, Velocity);
 *     Position*       pos  = ecs_view_write_t(itr, Position);
 *     ...
 *   }
 * }
 * ```
 */
#define ecs_system_define(_NAME_)                                                                  \
  static EcsSystemId ecs_system_id(_NAME_);                                                        \
  static void _ecs_sys_##_NAME_(                                                                   \
    MAYBE_UNUSED EcsWorld* world,                                                                  \
    MAYBE_UNUSED const u16 parCount,                                                               \
    MAYBE_UNUSED const u16 parIndex)

/**
 * Register a new component type.
 * NOTE: Can only be used inside a module-init function.
 * NOTE: The component has to be defined using 'ecs_comp_define()' in exactly one compilation unit.
 *
 * Pre-condition: No other component with the same name has been registered already.
 */
#define ecs_register_comp(_NAME_, ...)                                                             \
  ASSERT(sizeof(_NAME_) != 0, "Use 'ecs_register_comp_empty' for empty components")                \
  ecs_module_register_comp(_builder, &ecs_comp_id(_NAME_), &(EcsCompConfig){                       \
      .name = string_lit(#_NAME_), .size = sizeof(_NAME_), .align = alignof(_NAME_), ##__VA_ARGS__})

#define ecs_register_comp_empty(_NAME_, ...)                                                       \
  ecs_module_register_comp(_builder, &ecs_comp_id(_NAME_), &(EcsCompConfig){                       \
      .name = string_lit(#_NAME_), .size = 0, .align = 1, ##__VA_ARGS__})

/**
 * Register a new view.
 * NOTE: Can only be used inside a module-init function.
 * NOTE: The view has to be defined using 'ecs_view_define()' in the same compilation unit.
 */
#define ecs_register_view(_NAME_)                                                                  \
  ecs_module_register_view(_builder, &ecs_view_id(_NAME_), &(EcsViewConfig){                       \
      .name = string_lit(#_NAME_), .initRoutine = &_ecs_view_init_##_NAME_})

/**
 * Register a new system with a list of view-ids as dependencies.
 * NOTE: Can only be used inside a module-init function.
 *
 * Example usage:
 * ```
 * ecs_register_system(ApplyVelocity, ecs_view_id(ReadVeloWritePosView));
 * ```
 */
#define ecs_register_system(_NAME_, ...)                                                           \
  ecs_register_system_with_flags(_NAME_, EcsSystemFlags_None, ##__VA_ARGS__)

#define ecs_register_system_with_flags(_NAME_, _FLAGS_, ...)                                       \
  ecs_module_register_system(_builder, &ecs_system_id(_NAME_), &(EcsSystemConfig){                 \
      .name      = string_lit(#_NAME_),                                                            \
      .routine   = &_ecs_sys_##_NAME_,                                                             \
      .flags     = (_FLAGS_),                                                                      \
      .views     = (const EcsViewId[]){ VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, 0) },                 \
      .viewCount = COUNT_VA_ARGS(__VA_ARGS__)})

/**
 * Specify the execution order for the given system.
 * NOTE: Order is a signed 32 bit integer.
 */
#define ecs_order(_SYSTEM_, _ORDER_)                                                               \
  ecs_module_update_order(_builder, ecs_system_id(_SYSTEM_), (_ORDER_))

/**
 * Specify the parallel count for the given system.
 * The given system will be executed '_PARALLEL_COUNT_' times each tick.
 *
 * NOTE: 'parCount' and 'parIndex' will be provided as arguments to the system, and can be used to
 * execute different work for each invocation.
 *
 * NOTE: Care must be taken that the system supports running in parallel. This means different
 * invocations of the same system should not write to the same component on the same entity, or read
 * a component that is written by another invocation.
 *
 * Example of using 'parCount' and 'parIndex' with a stepped iterator, each invocation will execute
 * on a different subset of the entities in the 'ReadVeloWritePosView' view.
 * ```
 * ecs_system_define(ApplyVelocitySys) {
 *   EcsView* view = ecs_world_view_t(world, ReadVeloWritePosView);
 *   for (EcsIterator* itr = ecs_view_itr_step(view, parCount, parIndex); ecs_view_walk(itr);) {
 *     const Velocity* velo = ecs_view_read_t(itr, Velocity);
 *     Position*       pos  = ecs_view_write_t(itr, Position);
 *     ...
 *   }
 * }
 * ```
 */
#define ecs_parallel(_SYSTEM_, _PARALLEL_COUNT_)                                                   \
  ecs_module_update_parallel(_builder, ecs_system_id(_SYSTEM_), (_PARALLEL_COUNT_))

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

EcsCompId   ecs_module_register_comp(EcsModuleBuilder*, EcsCompId*, const EcsCompConfig*);
EcsViewId   ecs_module_register_view(EcsModuleBuilder*, EcsViewId*, const EcsViewConfig*);
EcsSystemId ecs_module_register_system(EcsModuleBuilder*, EcsSystemId*, const EcsSystemConfig*);
void        ecs_module_update_order(EcsModuleBuilder*, EcsSystemId, i32 order);
void        ecs_module_update_parallel(EcsModuleBuilder*, EcsSystemId, u16 parallelCount);

void ecs_module_view_flags(EcsViewBuilder*, EcsViewFlags);
void ecs_module_access_with(EcsViewBuilder*, EcsCompId);
void ecs_module_access_without(EcsViewBuilder*, EcsCompId);
void ecs_module_access_read(EcsViewBuilder*, EcsCompId);
void ecs_module_access_write(EcsViewBuilder*, EcsCompId);
void ecs_module_access_maybe_read(EcsViewBuilder*, EcsCompId);
void ecs_module_access_maybe_write(EcsViewBuilder*, EcsCompId);
