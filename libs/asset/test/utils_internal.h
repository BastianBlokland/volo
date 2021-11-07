#pragma once
#include "ecs_runner.h"

/**
 * Due to the asynchronous nature of asset-loading we give the asset-system a set number of ticks to
 * process requests.
 */
void asset_test_wait(EcsRunner*);
