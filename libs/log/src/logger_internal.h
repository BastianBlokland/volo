#pragma once
#include "log_logger.h"

#define log_params_max 10

void log_global_logger_init();
void log_global_logger_teardown();

String log_level_str(LogLevel);

bool log_mask_enabled(LogMask, LogLevel);
