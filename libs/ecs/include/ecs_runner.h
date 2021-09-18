#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * True while the current thread is running an ecs system.
 */
extern THREAD_LOCAL bool g_ecsRunningSystem;
