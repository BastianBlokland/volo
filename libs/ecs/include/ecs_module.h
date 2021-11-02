#pragma once
#include "core_annotation.h"
#include "core_macro.h"
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
typedef void (*EcsCompDestructor)(void*);

typedef struct {
  String            name;
  usize             size;
  usize             align;
  EcsCompDestructor destructor;
} EcsCompConfig;

typedef struct {
  String      name;
  EcsViewInit initRoutine;
} EcsViewConfig;

typedef enum {
  EcsSystemFlags_None = 0,

  /**
   * The system should always be run on the same thread.
   * NOTE: Incurs an additional scheduling overhead.
   */
  EcsSystemFlags_ThreadAffinity = 1 << 0,
} EcsSystemFlags;

typedef struct {
  String           name;
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
 * NOTE: Can optionally be declared using ecs_comp_extern().
 */
#define ecs_comp_define(_NAME_)                                                            \
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
#define ecs_comp_extern(_NAME_)                                                            \
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
  static void _ecs_system_##_NAME_(MAYBE_UNUSED EcsWorld* world)

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
      .routine   = &_ecs_system_##_NAME_,                                                          \
      .flags     = (_FLAGS_),                                                                      \
      .views     = (const EcsViewId[]){ VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, 0) },                 \
      .viewCount = COUNT_VA_ARGS(__VA_ARGS__)})

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

void ecs_module_access_with(EcsViewBuilder*, EcsCompId);
void ecs_module_access_without(EcsViewBuilder*, EcsCompId);
void ecs_module_access_read(EcsViewBuilder*, EcsCompId);
void ecs_module_access_write(EcsViewBuilder*, EcsCompId);
void ecs_module_access_maybe_read(EcsViewBuilder*, EcsCompId);
void ecs_module_access_maybe_write(EcsViewBuilder*, EcsCompId);
