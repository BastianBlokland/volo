#include "core_alloc.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "jobs_graph.h"
#include "trace_tracer.h"

#include "graph_internal.h"

#define jobs_graph_aux_chunk_size (4 * usize_kibibyte)
#define jobs_graph_max_tasks 25000

ASSERT(jobs_graph_max_tasks < u16_max, "JobTasks have to be representable with 16 bits")

INLINE_HINT static JobTaskLink* jobs_graph_task_link(const JobGraph* graph, JobTaskLinkId id) {
  return &dynarray_begin_t(&graph->childLinks, JobTaskLink)[id];
}

/**
 * Add a new task to the end of the linked list of task children that starts at 'linkHead'.
 * Pass a pointer to 'sentinel_u16' as 'linkHead' to create a new list.
 */
static void jobs_graph_add_task_child_link(
    JobGraph* graph, const JobTaskId childTask, JobTaskLinkId* linkHead) {

  // Create a new link.
  const JobTaskLinkId newLinkId                     = (JobTaskLinkId)graph->childLinks.size;
  *dynarray_push_t(&graph->childLinks, JobTaskLink) = (JobTaskLink){
      .task = childTask,
      .next = sentinel_u16,
  };

  if (sentinel_check(*linkHead)) {
    // There was no head link yet; Make the new link the head-link.
    *linkHead = newLinkId;
    return;
  }

  // Find the link to attach it to by walking the sibling chain.
  // TODO: Consider storing an end link to avoid having to walk this each time.
  for (JobTaskLink* link = jobs_graph_task_link(graph, *linkHead);;
       link              = jobs_graph_task_link(graph, link->next)) {
    diag_assert_msg(
        link->task != childTask,
        "Duplicate dependency for task '{}' is not supported",
        fmt_int(childTask));

    if (sentinel_check(link->next)) {
      // Found the end of the sibling chain.
      link->next = newLinkId;
      return;
    }
  }
}

/**
 * Remove a task from the linked list of task children that starts at 'linkHead'.
 * Returns if the task existed in the linked-list (and thus was removed).
 *
 * NOTE: Does not free up space in the 'childLinks' array as that would require updating the
 * indices of all registered dependencies.
 */
static bool jobs_graph_remove_task_child_link(
    JobGraph* graph, const JobTaskId childTask, JobTaskLinkId* linkHead) {

  JobTaskLink*  prevLink = null;
  JobTaskLinkId itr      = *linkHead;
  while (!sentinel_check(itr)) {
    JobTaskLink* link = jobs_graph_task_link(graph, itr);
    if (link->task != childTask) {
      // Not the element we are looking for; keep walking the sibling chain.
      prevLink = link;
      itr      = link->next;
      continue;
    }

    // Found the link to remove.
    if (prevLink) {
      // link the previous to the next to skip this element.
      prevLink->next = link->next;
    } else {
      // This was the first link; set it as the new headLink.
      *linkHead = link->next;
    }
    return true;
  }

  // Child not found in the list.
  return false;
}

/**
 * Remove dependencies that are already inherited from a parent.
 * More info: https://en.wikipedia.org/wiki/Transitive_reduction
 * Returns the amount of removed dependencies.
 */
static u32 jobs_graph_task_transitive_reduce(
    JobGraph* graph, const JobTaskId rootTask, const JobTaskId task, BitSet processed) {
  /**
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains.
   */
  u32 depsRemoved = 0;
  if (bitset_test(processed, task)) {
    return depsRemoved; // Already processed.
  }
  jobs_graph_for_task_child(graph, task, child) {
    // Dependency from 'child' to 'root' can be removed as we already inherited that dependency
    // through 'task'.
    if (jobs_graph_task_undepend(graph, rootTask, child.task)) {
      ++depsRemoved;
    }
    // Recurse in a 'depth-first' manner.
    depsRemoved += jobs_graph_task_transitive_reduce(graph, rootTask, child.task, processed);
  }
  bitset_set(processed, task); // Mark the task as processed.
  return depsRemoved;
}

/**
 * Remove dependencies that are already inherited from a parent.
 * Returns the amount of removed dependencies.
 * Note is relatively expensive as it follows all dependencies in a 'depth-first' manner.
 */
static u32 jobs_graph_task_reduce_dependencies(JobGraph* graph, const JobTaskId task) {
  BitSet processed = mem_stack(bits_to_bytes(graph->tasks.size) + 1);
  mem_set(processed, 0);

  u32 depsRemoved = 0;
  jobs_graph_for_task_child(graph, task, child) {
    depsRemoved += jobs_graph_task_transitive_reduce(graph, task, child.task, processed);
  }
  return depsRemoved;
}

static bool jobs_graph_has_task_cycle(
    const JobGraph* graph, const JobTaskId task, BitSet processed, BitSet processing) {
  if (bitset_test(processed, task)) {
    return false; // Already processed; no cycle.
  }
  if (bitset_test(processing, task)) {
    return true; // Currently processing this task; cycle.
  }
  bitset_set(processing, task); // Mark the task as currently being processed.

  jobs_graph_for_task_child(graph, task, child) {
    if (jobs_graph_has_task_cycle(graph, child.task, processed, processing)) {
      return true;
    }
  }

  bitset_clear(processing, task);
  bitset_set(processed, task);
  return false;
}

static bool jobs_graph_has_cycle(const JobGraph* graph) {
  /**
   * Do a 'Depth First Search' to find cycles.
   * More info: https://en.wikipedia.org/wiki/Depth-first_search
   *
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains.
   */

  BitSet processed  = mem_stack(bits_to_bytes(graph->tasks.size) + 1);
  BitSet processing = mem_stack(bits_to_bytes(graph->tasks.size) + 1);

  mem_set(processed, 0);
  mem_set(processing, 0);

  jobs_graph_for_task(graph, taskId) {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    if (jobs_graph_has_task_cycle(graph, taskId, processed, processing)) {
      return true;
    }
  }
  return false;
}

/**
 * Insert the task (and all its (grand-)children) topologically sorted in the output array.
 * This has the effect to 'flattening' the graph to a linear sequence that satisfies the dependency
 * constraints.
 * More info: https://en.wikipedia.org/wiki/Topological_sorting
 * NOTE: 'sortedTasks' array needs to be big enough to contain all (grand-)child tasks.
 */
static void jobs_graph_topologically_insert(
    const JobGraph* graph,
    const JobTaskId task,
    BitSet          processed,
    JobTaskId*      sortedTasks,
    u32*            sortedTaskCount) {
  /**
   * Do a 'Depth First Search' to insert the task and its children.
   *
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains.
   */
  bitset_set(processed, task); // Mark the task as processed.

  jobs_graph_for_task_child(graph, task, child) {
    if (bitset_test(processed, child.task)) {
      continue; // Already processed.
    }
    jobs_graph_topologically_insert(graph, child.task, processed, sortedTasks, sortedTaskCount);
  }
  sortedTasks[(*sortedTaskCount)++] = task;
}

/**
 * Calculate the longest (aka 'critical') path through the graph.
 */
static u16 jobs_graph_longestpath(const JobGraph* graph) {
  /**
   * First flatten the graph into a topologically sorted set of tasks, then starting from the leaves
   * start summing all the distances.
   * More Info:
   * http://www.mathcs.emory.edu/~cheung/Courses/171/Syllabus/11-Graph/Docs/longest-path-in-dag.pdf
   */

  BitSet processed = mem_stack(bits_to_bytes(graph->tasks.size) + 1);
  mem_set(processed, 0);

  JobTaskId* sortedTasks      = mem_stack(sizeof(JobTaskId) * graph->tasks.size).ptr;
  u32        sortedTasksCount = 0;

  // Created a topologically sorted set of tasks.
  jobs_graph_for_task(graph, taskId) {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    jobs_graph_topologically_insert(graph, taskId, processed, sortedTasks, &sortedTasksCount);
  }

  /**
   * Keep a distance per task in the graph.
   * Initialize to 'sentinel_u16' when the task has a parent or 1 when its a root task.
   */

  u16* distances = mem_stack(sizeof(u16) * graph->tasks.size).ptr;
  for (JobTaskId taskId = 0; taskId != graph->tasks.size; ++taskId) {
    distances[taskId] = jobs_graph_task_has_parent(graph, taskId) ? sentinel_u16 : 1;
  }

  u16 maxDist = 1;
  for (u32 i = sortedTasksCount; i-- != 0;) {
    const JobTaskId taskId      = sortedTasks[i];
    const u16       currentDist = distances[taskId];

    if (!sentinel_check(currentDist)) {
      jobs_graph_for_task_child(graph, taskId, child) {
        u16* childDist = &distances[child.task];
        if (sentinel_check(*childDist) || *childDist < (currentDist + 1)) {
          *childDist = currentDist + 1;
        }
        maxDist = math_max(maxDist, *childDist);
      }
    }
  }

  return maxDist;
}

JobGraph* jobs_graph_create(Allocator* alloc, const String name, const u32 taskCapacity) {
  JobGraph* graph = alloc_alloc_t(alloc, JobGraph);

  *graph = (JobGraph){
      .tasks         = dynarray_create(alloc, 64, alignof(JobTask), taskCapacity),
      .parentCounts  = dynarray_create_t(alloc, u16, taskCapacity),
      .childSetHeads = dynarray_create_t(alloc, JobTaskLinkId, taskCapacity),
      .childLinks    = dynarray_create_t(alloc, JobTaskLink, taskCapacity),
      .name          = string_dup(alloc, name),
      .allocTaskAux  = alloc_chunked_create(alloc, alloc_bump_create, jobs_graph_aux_chunk_size),
      .alloc         = alloc,
  };

  return graph;
}

void jobs_graph_destroy(JobGraph* graph) {
  dynarray_destroy(&graph->tasks);
  dynarray_destroy(&graph->parentCounts);
  dynarray_destroy(&graph->childSetHeads);
  dynarray_destroy(&graph->childLinks);

  string_free(graph->alloc, graph->name);
  alloc_chunked_destroy(graph->allocTaskAux);
  alloc_free_t(graph->alloc, graph);
}

void jobs_graph_clear(JobGraph* graph) {
  alloc_reset(graph->allocTaskAux); // Free all auxillary data (eg task names).
  dynarray_clear(&graph->tasks);
  dynarray_clear(&graph->parentCounts);
  dynarray_clear(&graph->childSetHeads);
  dynarray_clear(&graph->childLinks);
}

void jobs_graph_copy(JobGraph* dst, JobGraph* src) {
  jobs_graph_clear(dst);

  // Insert all the tasks from the src graph.
  jobs_graph_for_task(src, srcTaskId) {
    const JobTask* srcTask    = job_graph_task_def(src, srcTaskId);
    const usize    srcCtxSize = 64 - sizeof(JobTask);
    const Mem      srcCtx     = mem_create(bits_ptr_offset(srcTask, sizeof(JobTask)), srcCtxSize);
    jobs_graph_add_task(dst, srcTask->name, srcTask->routine, srcCtx, srcTask->flags);
  }

  // Insert the dependencies from the src graph.
  jobs_graph_for_task(src, srcTaskId) {
    jobs_graph_for_task_child(src, srcTaskId, child) {
      jobs_graph_task_depend(dst, srcTaskId, child.task);
    }
  }
}

JobTaskId jobs_graph_add_task(
    JobGraph*            graph,
    const String         name,
    const JobTaskRoutine routine,
    Mem                  ctx,
    const JobTaskFlags   flags) {
  // NOTE: Api promises sequential task-ids for sequential calls to jobs_graph_add_task.
  const JobTaskId id = (JobTaskId)graph->tasks.size;

  if (UNLIKELY(id == jobs_graph_max_tasks)) {
    diag_crash_msg("Maximum job graph task count exceeded");
  }

  Mem taskStorage              = dynarray_push(&graph->tasks, 1);
  *((JobTask*)taskStorage.ptr) = (JobTask){
      .routine = routine,
      .name    = string_dup(graph->allocTaskAux, name),
      .flags   = flags,
  };
  const Mem taskStorageCtx = mem_consume(taskStorage, sizeof(JobTask));
  diag_assert(bits_aligned_ptr(taskStorageCtx.ptr, 16)); // We promise at least 16 byte alignment.
  mem_cpy(taskStorageCtx, ctx);

  *dynarray_push_t(&graph->parentCounts, u16)            = 0;
  *dynarray_push_t(&graph->childSetHeads, JobTaskLinkId) = sentinel_u16;
  return id;
}

void jobs_graph_task_depend(JobGraph* graph, const JobTaskId parent, const JobTaskId child) {
  diag_assert(parent != child);
  diag_assert(parent < graph->tasks.size);
  diag_assert(child < graph->tasks.size);

  // Increment the parent count of the child.
  ++dynarray_begin_t(&graph->parentCounts, u16)[child];

  // Add the child to the 'childSet' of the parent.
  JobTaskLinkId* parentChildSetHead =
      &dynarray_begin_t(&graph->childSetHeads, JobTaskLinkId)[parent];

  jobs_graph_add_task_child_link(graph, child, parentChildSetHead);
}

bool jobs_graph_task_undepend(JobGraph* graph, JobTaskId parent, JobTaskId child) {
  diag_assert(parent != child);
  diag_assert(parent < graph->tasks.size);
  diag_assert(child < graph->tasks.size);

  // Try to remove the child from the 'childSet' of the parent.
  JobTaskLinkId* parentChildSetHead =
      &dynarray_begin_t(&graph->childSetHeads, JobTaskLinkId)[parent];
  if (jobs_graph_remove_task_child_link(graph, child, parentChildSetHead)) {

    // Decrement the parent count of the child.
    --dynarray_begin_t(&graph->parentCounts, u16)[child];

    return true;
  }
  return false; // No dependency existed between parent and child.
}

u32 jobs_graph_reduce_dependencies(JobGraph* graph) {
  u32 depsRemoved = 0;
  jobs_graph_for_task(graph, taskId) {
    depsRemoved += jobs_graph_task_reduce_dependencies(graph, taskId);
  }
  return depsRemoved;
}

bool jobs_graph_validate(const JobGraph* graph) {
  trace_begin("job_validate", TraceColor_Red);
  const bool hasCycles = jobs_graph_has_cycle(graph);
  trace_end();
  return !hasCycles;
}

u32 jobs_graph_task_count(const JobGraph* graph) { return (u32)graph->tasks.size; }

u32 jobs_graph_task_root_count(const JobGraph* graph) {
  u32 count = 0;
  jobs_graph_for_task(graph, taskId) { count += !jobs_graph_task_has_parent(graph, taskId); }
  return count;
}

u32 jobs_graph_task_leaf_count(const JobGraph* graph) {
  u32 count = 0;
  jobs_graph_for_task(graph, taskId) { count += !jobs_graph_task_has_child(graph, taskId); }
  return count;
}

String jobs_graph_name(const JobGraph* graph) { return graph->name; }

String jobs_graph_task_name(const JobGraph* graph, JobTaskId id) {
  return job_graph_task_def(graph, id)->name;
}

bool jobs_graph_task_has_parent(const JobGraph* graph, const JobTaskId task) {
  return jobs_graph_task_parent_count(graph, task) != 0;
}

bool jobs_graph_task_has_child(const JobGraph* graph, const JobTaskId task) {
  diag_assert_msg(task < graph->childSetHeads.size, "Out of bounds job task");

  const JobTaskLinkId childSetHead = dynarray_begin_t(&graph->childSetHeads, JobTaskLinkId)[task];
  return !sentinel_check(childSetHead);
}

u32 jobs_graph_task_parent_count(const JobGraph* graph, const JobTaskId task) {
  diag_assert_msg(task < graph->parentCounts.size, "Out of bounds job task");

  return dynarray_begin_t(&graph->parentCounts, u16)[task];
}

JobTaskChildItr jobs_graph_task_child_begin(const JobGraph* graph, const JobTaskId task) {
  diag_assert_msg(task < graph->childSetHeads.size, "Out of bounds job task");

  const JobTaskLinkId childSetHead = dynarray_begin_t(&graph->childSetHeads, JobTaskLinkId)[task];
  return jobs_graph_task_child_next(graph, (JobTaskChildItr){.next = childSetHead});
}

JobTaskChildItr jobs_graph_task_child_next(const JobGraph* graph, const JobTaskChildItr itr) {
  if (sentinel_check(itr.next)) {
    return (JobTaskChildItr){.task = sentinel_u16, .next = sentinel_u16};
  }
  const JobTaskLink link = *jobs_graph_task_link(graph, itr.next);
  return (JobTaskChildItr){.task = link.task, .next = link.next};
}

u32 jobs_graph_task_span(const JobGraph* graph) { return jobs_graph_longestpath(graph); }

f32 jobs_graph_task_parallelism(const JobGraph* graph) {
  return (f32)jobs_graph_task_count(graph) / (f32)jobs_graph_task_span(graph);
}
