#pragma once

#include "job_internal.h"

typedef struct {
  Job*      job;
  JobTaskId task;
} WorkItem;

#define workitem_valid(_WORKITEM_) ((_WORKITEM_).job != null)
