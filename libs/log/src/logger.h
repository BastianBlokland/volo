#pragma once
#include "log/logger.h"

#define log_params_max 10

void log_global_logger_init(void);
void log_global_logger_teardown(void);

String log_level_str(LogLevel);

bool log_mask_enabled(LogMask, LogLevel);
