#include "asset_manager.h"

/**
 * Register a dependency between the two assets.
 * When 'dependency' is changed the 'asset' is also marked as changed.
 * NOTE: At the moment is not possible to unregister a dependency.
 */
void asset_register_dep(EcsWorld* world, EcsEntityId asset, EcsEntityId dependency);
