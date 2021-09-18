#pragma once
#include "core_alignof.h"
#include "core_string.h"

/**
 * Identifier of a component. Unique within a single EcsMeta.
 */
typedef u16 EcsCompId;

/**
 * Variable that stores the EcsCompId for the component with the given name.
 */
#define ecs_comp_id(_NAME_) g_ecs_comp_##_NAME_

/**
 * Define a variable for storing the EcsCompId for the component with the given name.
 */
#define ecs_comp_define(_NAME_) EcsCompId ecs_comp_id(_NAME_)

/**
 * Declare the variable that stores the EcsCompId for the component with the given name.
 */
#define ecs_comp_declare(_NAME_) extern EcsCompId ecs_comp_id(_NAME_)

/**
 * Register a component to the given EcsMeta.
 * Note: The component has to be defined using 'ecs_comp_define()' in one compilation unit.
 */
#define ecs_comp_register_t(_META_, _NAME_)                                                        \
  ecs_comp_id(_NAME_) =                                                                            \
      ecs_register_comp_id((_META_), string_lit(#_NAME_), sizeof(_NAME_), alignof(_NAME_))

/**
 * Structure containing meta information about components and systems.
 */
typedef struct sEcsMeta EcsMeta;

/**
 * Create a new EcsMeta structure.
 * Note: Should be destroyed using 'ecs_meta_destroy()'.
 */
EcsMeta* ecs_meta_create(Allocator*);

/**
 * Destroy the given EcsMeta structure.
 */
void ecs_meta_destroy(EcsMeta*);

/**
 * Create a new component id.
 * Note: Usually the 'ecs_comp_register_t' convenience macro can be used instead of calling this
 * directly.
 */
EcsCompId ecs_register_comp_id(EcsMeta*, String name, usize size, usize align);

/**
 * Retrieve the name of a component.
 */
String ecs_comp_name(EcsMeta*, EcsCompId);
