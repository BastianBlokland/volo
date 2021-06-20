#include "core_dynarray.h"
#include "jobs_jobdef.h"

typedef u32 JobTaskLinkId;

typedef struct {
  JobTaskRoutine routine;
  void*          context;
  String         name;
} JobTask;

typedef struct {
  JobTaskId     task;
  JobTaskLinkId next;
} JobTaskLink;

struct sJobDef {
  DynArray   tasks;         // JobTask[]
  DynArray   parentCounts;  // u32[]
  DynArray   childSetHeads; // JobTaskLinkId[]
  DynArray   childLinks;    // JobTaskLink[]
  String     name;
  Allocator* alloc;
};
