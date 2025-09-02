#pragma once
#include "ecs/module.h"

/**
 * Global locale manager component.
 */
ecs_comp_extern(LocManagerComp);

/**
 * Initialize the locale manager.
 */
LocManagerComp* loc_manager_init(EcsWorld*, String preferredId);

/**
 * Has the locale manager finished initializing.
 */
bool loc_manager_ready(const LocManagerComp*);

/**
 * Query the available locales.
 * Pre-condition: loc_manager_ready().
 */
const String* loc_manager_locale_names(const LocManagerComp*); // String[loc_manager_locale_count];
u32           loc_manager_locale_count(const LocManagerComp*);

/**
 * Query / update the active locale.
 * Pre-condition: loc_manager_ready().
 * Pre-condition: localeIndex < loc_manager_locale_count().
 */
String loc_manager_active_id(const LocManagerComp*);
u32    loc_manager_active_get(const LocManagerComp*);
void   loc_manager_active_set(LocManagerComp*, u32 localeIndex);
