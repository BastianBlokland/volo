#include "core_annotation.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "jobs_graph.h"

#define jobtask_max_user_data (usize)(64 - sizeof(JobTask))

typedef u16 JobTaskLinkId;

typedef struct {
  ALIGNAS(16)
  JobTaskRoutine routine;
  String         name;
  JobTaskFlags   flags;
} JobTask;

ASSERT(sizeof(JobTask) == 32, "Unexpected JobTask size");

typedef struct {
  JobTaskId     task;
  JobTaskLinkId next;
} JobTaskLink;

struct sJobGraph {
  DynArray   tasks;         // JobTask[], NOTE: Stride is 64 not sizeof(JobTask).
  DynArray   parentCounts;  // u16[]
  DynArray   childSetHeads; // JobTaskLinkId[]
  DynArray   childLinks;    // JobTaskLink[]
  String     name;
  Allocator* allocTaskAux; // (chunked) bump allocator for axillary data (eg task names).
  Allocator* alloc;
};

MAYBE_UNUSED static const JobTask* job_graph_task_def(const JobGraph* graph, const JobTaskId task) {
  diag_assert(task < graph->tasks.size);
  return (JobTask*)bits_ptr_offset(graph->tasks.data.ptr, 64 * task);
}
