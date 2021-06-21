#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "jobdef_internal.h"
#include "jobs_jobdef.h"

static JobTaskLink* jobdef_task_link(const JobDef* jobDef, JobTaskLinkId id) {
  return dynarray_at_t(&jobDef->childLinks, id, JobTaskLink);
}

/**
 * Add a new task to the end of the linked list of task children that starts at 'linkHead'.
 * Pass 'sentinel_u32' as 'linkHead' to create a new list.
 * Returns an identifier to the newly created node.
 */
static JobTaskLinkId
jobdef_add_task_child_link(JobDef* jobDef, const JobTaskId childTask, JobTaskLinkId linkHead) {
  // Walk to the end of the sibling chain.
  // TODO: Consider storing an end link to avoid having to walk this each time.
  JobTaskLinkId lastLink = sentinel_u32;
  while (!sentinel_check(linkHead)) {
    lastLink = linkHead;
    linkHead = jobdef_task_link(jobDef, linkHead)->next;
  }
  // Create a new link.
  const JobTaskLinkId newLinkIdx                     = jobDef->childLinks.size;
  *dynarray_push_t(&jobDef->childLinks, JobTaskLink) = (JobTaskLink){
      .task = childTask,
      .next = sentinel_u32,
  };
  // Add the new link to the last sibling.
  if (!sentinel_check(lastLink)) {
    jobdef_task_link(jobDef, lastLink)->next = newLinkIdx;
  }
  return newLinkIdx;
}

static bool jobdef_has_task_cycle(
    const JobDef* jobDef, const JobTaskId task, BitSet processed, BitSet processing) {
  if (bitset_test(processed, task)) {
    return false; // Already processed; no cycle.
  }
  if (bitset_test(processing, task)) {
    return true; // Currently processing this task; cycle.
  }
  bitset_set(processing, task); // Mark the task as currently being processed.

  jobdef_for_task_child(jobDef, task, child, {
    if (jobdef_has_task_cycle(jobDef, child.task, processed, processing)) {
      return true;
    }
  });

  bitset_clear(processing, task);
  bitset_set(processed, task);
  return false;
}

static bool jobdef_has_cycle(const JobDef* jobDef) {
  /**
   * Do a 'Depth First Search' to find cycles.
   * More info: https://en.wikipedia.org/wiki/Depth-first_search
   *
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains. We might have to refactor this to avoid recursion in the future, however
   * my current assumption is that we won't have that long task chains (which would be bad for
   * performance anyway).
   */

  // Using scratch memory here limits us to 65536 tasks (with the current scratch budgets).
  BitSet processed  = alloc_alloc(g_alloc_scratch, bits_to_bytes(jobDef->tasks.size) + 1, 1);
  BitSet processing = alloc_alloc(g_alloc_scratch, bits_to_bytes(jobDef->tasks.size) + 1, 1);

  mem_set(processed, 0);
  mem_set(processing, 0);

  jobdef_for_task(jobDef, taskId, {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    if (jobdef_has_task_cycle(jobDef, taskId, processed, processing)) {
      return true;
    }
  });
  return false;
}

/**
 * Insert the task (and all its children) in a topologically sorted fashion in the output array.
 * This has the effect to 'Flattening' the graph to a linear sequence that satisfies the dependency
 * constraints.
 * More info: https://en.wikipedia.org/wiki/Topological_sorting
 */
static void jobdef_topologically_insert(
    const JobDef* jobDef, const JobTaskId task, BitSet processed, DynArray* sortedIndices) {
  /**
   * Do a 'Depth First Search' to insert the task and its children.
   *
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains.
   */
  bitset_set(processed, task); // Mark the task as processed.

  jobdef_for_task_child(jobDef, task, child, {
    if (bitset_test(processed, child.task)) {
      continue; // Already processed.
    }
    jobdef_topologically_insert(jobDef, child.task, processed, sortedIndices);
  });

  *dynarray_push_t(sortedIndices, JobTaskId) = task;
}

/**
 * Calculate the longest (aka 'Critical') graph the graph.
 */
static usize jobdef_longestpath(const JobDef* jobDef) {
  /**
   * First flatten the graph into a topologically sorted set of tasks, then starting from the leaves
   * start summing all the distances.
   * More Info:
   * http://www.mathcs.emory.edu/~cheung/Courses/171/Syllabus/11-Graph/Docs/longest-path-in-dag.pdf
   */

  // Using scratch memory here limits us to 65536 tasks (with the current scratch budgets).
  BitSet processed = alloc_alloc(g_alloc_scratch, bits_to_bytes(jobDef->tasks.size) + 1, 1);
  mem_set(processed, 0);

  // Note: Using heap memory here is unfortunate, but under current scratch budgets we would only
  // support 2048 tasks. In the future we can reconsider those budgets.
  DynArray sortedTasks = dynarray_create_t(g_alloc_heap, JobTaskId, jobDef->tasks.size);

  // Created a topologically sorted set of tasks.
  jobdef_for_task(jobDef, taskId, {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    jobdef_topologically_insert(jobDef, taskId, processed, &sortedTasks);
  });

  /**
   * Keep a distance per task in the graph.
   * Initialize to 'sentinel_usize' when the task has a parent or 1 when its a root task.
   */

  // Note: Unfortunate heap memory usage, but current scratch memory budgets would be too limiting.
  DynArray distances = dynarray_create_t(g_alloc_heap, usize, jobDef->tasks.size);
  dynarray_resize(&distances, jobDef->tasks.size);
  dynarray_for_t(&distances, usize, itr, {
    const JobTaskId taskId = itr_i;
    *itr                   = jobdef_task_has_parent(jobDef, taskId) ? sentinel_usize : 1;
  });

  usize maxDist = 1;
  for (usize i = sortedTasks.size; i-- != 0;) {
    const JobTaskId taskId      = *dynarray_at_t(&sortedTasks, i, JobTaskId);
    const usize     currentDist = *dynarray_at_t(&distances, taskId, usize);

    if (!sentinel_check(currentDist)) {
      jobdef_for_task_child(jobDef, taskId, child, {
        usize* childDist = dynarray_at_t(&distances, child.task, usize);
        if (sentinel_check(*childDist) || *childDist < (currentDist + 1)) {
          *childDist = currentDist + 1;
        }
        maxDist = math_max(maxDist, *childDist);
      });
    }
  }

  dynarray_destroy(&sortedTasks);
  dynarray_destroy(&distances);
  return maxDist;
}

JobDef* jobdef_create(Allocator* alloc, const String name, const usize taskCapacity) {
  JobDef* jobDef = alloc_alloc_t(alloc, JobDef);
  *jobDef        = (JobDef){
      .tasks         = dynarray_create_t(alloc, JobTask, taskCapacity),
      .parentCounts  = dynarray_create_t(alloc, u32, taskCapacity),
      .childSetHeads = dynarray_create_t(alloc, JobTaskLinkId, taskCapacity),
      .childLinks    = dynarray_create_t(alloc, JobTaskLink, taskCapacity),
      .name          = string_dup(alloc, name),
      .alloc         = alloc,
  };
  return jobDef;
}

void jobdef_destroy(JobDef* jobDef) {
  dynarray_for_t(&jobDef->tasks, JobTask, t, { string_free(jobDef->alloc, t->name); });
  dynarray_destroy(&jobDef->tasks);

  dynarray_destroy(&jobDef->parentCounts);
  dynarray_destroy(&jobDef->childSetHeads);
  dynarray_destroy(&jobDef->childLinks);

  string_free(jobDef->alloc, jobDef->name);
  alloc_free_t(jobDef->alloc, jobDef);
}

JobTaskId
jobdef_add_task(JobDef* jobDef, const String name, const JobTaskRoutine routine, void* context) {
  const JobTaskId id                        = (JobTaskId)jobDef->tasks.size;
  *dynarray_push_t(&jobDef->tasks, JobTask) = (JobTask){
      .name    = string_dup(jobDef->alloc, name),
      .routine = routine,
      .context = context,
  };
  *dynarray_push_t(&jobDef->parentCounts, u32)            = 0;
  *dynarray_push_t(&jobDef->childSetHeads, JobTaskLinkId) = sentinel_u32;
  return id;
}

void jobdef_task_depend(JobDef* jobDef, const JobTaskId parent, const JobTaskId child) {
  diag_assert(parent != child);

  // Increment the parent count of the child.
  ++(*dynarray_at_t(&jobDef->parentCounts, child, u32));

  // Add the child to the 'childSet' of the parent.
  JobTaskLinkId* parentChildSetHead = dynarray_at_t(&jobDef->childSetHeads, parent, JobTaskLinkId);
  if (sentinel_check(*parentChildSetHead)) {
    *parentChildSetHead = jobdef_add_task_child_link(jobDef, child, sentinel_u32);
  } else {
    jobdef_add_task_child_link(jobDef, child, *parentChildSetHead);
  }
}

bool jobdef_validate(const JobDef* jobDef) { return !jobdef_has_cycle(jobDef); }

usize jobdef_task_count(const JobDef* jobDef) { return jobDef->tasks.size; }

usize jobdef_task_root_count(const JobDef* jobDef) {
  usize count = 0;
  jobdef_for_task(jobDef, taskId, { count += !jobdef_task_has_parent(jobDef, taskId); });
  return count;
}

usize jobdef_task_leaf_count(const JobDef* jobDef) {
  usize count = 0;
  jobdef_for_task(jobDef, taskId, { count += !jobdef_task_has_child(jobDef, taskId); });
  return count;
}

String jobdef_job_name(const JobDef* jobDef) { return jobDef->name; }

String jobdef_task_name(const JobDef* jobDef, JobTaskId id) {
  return dynarray_at_t(&jobDef->tasks, id, JobTask)->name;
}

bool jobdef_task_has_parent(const JobDef* jobDef, const JobTaskId task) {
  return jobdef_task_parent_count(jobDef, task) != 0;
}

bool jobdef_task_has_child(const JobDef* jobDef, const JobTaskId task) {
  const JobTaskLinkId childSetHead = *dynarray_at_t(&jobDef->childSetHeads, task, JobTaskLinkId);
  return !sentinel_check(childSetHead);
}

usize jobdef_task_parent_count(const JobDef* jobDef, const JobTaskId task) {
  return *dynarray_at_t(&jobDef->parentCounts, task, u32);
}

JobTaskChildItr jobdef_task_child_begin(const JobDef* jobDef, const JobTaskId task) {
  const JobTaskLinkId childSetHead = *dynarray_at_t(&jobDef->childSetHeads, task, JobTaskLinkId);
  return jobdef_task_child_next(jobDef, (JobTaskChildItr){.next = childSetHead});
}

JobTaskChildItr jobdef_task_child_next(const JobDef* jobDef, const JobTaskChildItr itr) {
  if (sentinel_check(itr.next)) {
    return (JobTaskChildItr){.task = sentinel_u32, .next = sentinel_u32};
  }
  const JobTaskLink link = *jobdef_task_link(jobDef, itr.next);
  return (JobTaskChildItr){.task = link.task, .next = link.next};
}

usize jobdef_task_span(const JobDef* jobDef) { return jobdef_longestpath(jobDef); }

f32 jobdef_task_parallelism(const JobDef* jobDef) {
  return (f32)jobdef_task_count(jobDef) / (f32)jobdef_task_span(jobDef);
}
