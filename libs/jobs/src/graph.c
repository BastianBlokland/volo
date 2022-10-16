#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "jobs_graph.h"

#include "graph_internal.h"

static JobTaskLink* jobs_graph_task_link(const JobGraph* graph, JobTaskLinkId id) {
  return dynarray_at_t(&graph->childLinks, id, JobTaskLink);
}

/**
 * Add a new task to the end of the linked list of task children that starts at 'linkHead'.
 * Pass a pointer to 'sentinel_u32' as 'linkHead' to create a new list.
 */
static void jobs_graph_add_task_child_link(
    JobGraph* graph, const JobTaskId childTask, JobTaskLinkId* linkHead) {

  // Create a new link.
  const JobTaskLinkId newLinkId                     = (JobTaskLinkId)graph->childLinks.size;
  *dynarray_push_t(&graph->childLinks, JobTaskLink) = (JobTaskLink){
      .task = childTask,
      .next = sentinel_u32,
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
 * indices of all registred dependencies.
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
static usize jobs_graph_task_transitive_reduce(
    JobGraph* graph, const JobTaskId rootTask, const JobTaskId task, BitSet processed) {
  /**
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains.
   */
  usize depsRemoved = 0;
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
static usize jobs_graph_task_reduce_dependencies(JobGraph* graph, const JobTaskId task) {
  // Using scratch memory here limits us to 65536 tasks (with the current scratch budgets).
  BitSet processed = alloc_alloc(g_alloc_scratch, bits_to_bytes(graph->tasks.size) + 1, 1);
  mem_set(processed, 0);

  usize depsRemoved = 0;
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

  // Using scratch memory here limits us to 65536 tasks (with the current scratch budgets).
  BitSet processed  = alloc_alloc(g_alloc_scratch, bits_to_bytes(graph->tasks.size) + 1, 1);
  BitSet processing = alloc_alloc(g_alloc_scratch, bits_to_bytes(graph->tasks.size) + 1, 1);

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
 * Insert the task (and all its children) in a topologically sorted fashion in the output array.
 * This has the effect to 'Flattening' the graph to a linear sequence that satisfies the dependency
 * constraints.
 * More info: https://en.wikipedia.org/wiki/Topological_sorting
 */
static void jobs_graph_topologically_insert(
    const JobGraph* graph, const JobTaskId task, BitSet processed, DynArray* sortedIndices) {
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
    jobs_graph_topologically_insert(graph, child.task, processed, sortedIndices);
  }

  *dynarray_push_t(sortedIndices, JobTaskId) = task;
}

/**
 * Calculate the longest (aka 'Critical') graph the graph.
 */
static usize jobs_graph_longestpath(const JobGraph* graph) {
  /**
   * First flatten the graph into a topologically sorted set of tasks, then starting from the leaves
   * start summing all the distances.
   * More Info:
   * http://www.mathcs.emory.edu/~cheung/Courses/171/Syllabus/11-Graph/Docs/longest-path-in-dag.pdf
   */

  // Using scratch memory here limits us to 65536 tasks (with the current scratch budgets).
  BitSet processed = alloc_alloc(g_alloc_scratch, bits_to_bytes(graph->tasks.size) + 1, 1);
  mem_set(processed, 0);

  // Note: Using heap memory here is unfortunate, but under current scratch budgets we would only
  // support 2048 tasks. In the future we can reconsider those budgets.
  DynArray sortedTasks = dynarray_create_t(g_alloc_heap, JobTaskId, graph->tasks.size);

  // Created a topologically sorted set of tasks.
  jobs_graph_for_task(graph, taskId) {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    jobs_graph_topologically_insert(graph, taskId, processed, &sortedTasks);
  }

  /**
   * Keep a distance per task in the graph.
   * Initialize to 'sentinel_usize' when the task has a parent or 1 when its a root task.
   */

  // Note: Unfortunate heap memory usage, but current scratch memory budgets would be too limiting.
  DynArray distances = dynarray_create_t(g_alloc_heap, usize, graph->tasks.size);
  dynarray_resize(&distances, graph->tasks.size);
  for (JobTaskId taskId = 0; taskId != graph->tasks.size; ++taskId) {
    usize* dist = dynarray_at_t(&distances, taskId, usize);
    *dist       = jobs_graph_task_has_parent(graph, taskId) ? sentinel_usize : 1;
  }

  usize maxDist = 1;
  for (usize i = sortedTasks.size; i-- != 0;) {
    const JobTaskId taskId      = *dynarray_at_t(&sortedTasks, i, JobTaskId);
    const usize     currentDist = *dynarray_at_t(&distances, taskId, usize);

    if (!sentinel_check(currentDist)) {
      jobs_graph_for_task_child(graph, taskId, child) {
        usize* childDist = dynarray_at_t(&distances, child.task, usize);
        if (sentinel_check(*childDist) || *childDist < (currentDist + 1)) {
          *childDist = currentDist + 1;
        }
        maxDist = math_max(maxDist, *childDist);
      }
    }
  }

  dynarray_destroy(&sortedTasks);
  dynarray_destroy(&distances);
  return maxDist;
}

JobGraph* jobs_graph_create(Allocator* alloc, const String name, const usize taskCapacity) {
  JobGraph* graph = alloc_alloc_t(alloc, JobGraph);
  *graph          = (JobGraph){
      .tasks         = dynarray_create(alloc, 64, alignof(JobTask), taskCapacity),
      .parentCounts  = dynarray_create_t(alloc, u32, taskCapacity),
      .childSetHeads = dynarray_create_t(alloc, JobTaskLinkId, taskCapacity),
      .childLinks    = dynarray_create_t(alloc, JobTaskLink, taskCapacity),
      .name          = string_dup(alloc, name),
      .alloc         = alloc,
  };
  return graph;
}

void jobs_graph_destroy(JobGraph* graph) {
  for (usize i = 0; i != graph->tasks.size; ++i) {
    JobTask* task = dynarray_at_t(&graph->tasks, i, JobTask);
    string_free(graph->alloc, task->name);
  }
  dynarray_destroy(&graph->tasks);

  dynarray_destroy(&graph->parentCounts);
  dynarray_destroy(&graph->childSetHeads);
  dynarray_destroy(&graph->childLinks);

  string_free(graph->alloc, graph->name);
  alloc_free_t(graph->alloc, graph);
}

void jobs_graph_clear(JobGraph* graph) {
  for (usize i = 0; i != graph->tasks.size; ++i) {
    JobTask* task = dynarray_at_t(&graph->tasks, i, JobTask);
    string_free(graph->alloc, task->name);
  }
  dynarray_clear(&graph->tasks);
  dynarray_clear(&graph->parentCounts);
  dynarray_clear(&graph->childSetHeads);
  dynarray_clear(&graph->childLinks);
}

JobTaskId jobs_graph_add_task(
    JobGraph*            graph,
    const String         name,
    const JobTaskRoutine routine,
    Mem                  ctx,
    const JobTaskFlags   flags) {
  const JobTaskId id = (JobTaskId)graph->tasks.size;

  Mem taskStorage              = dynarray_push(&graph->tasks, 1);
  *((JobTask*)taskStorage.ptr) = (JobTask){
      .routine = routine,
      .name    = string_dup(graph->alloc, name),
      .flags   = flags,
  };
  mem_cpy(mem_consume(taskStorage, sizeof(JobTask)), ctx);

  *dynarray_push_t(&graph->parentCounts, u32)            = 0;
  *dynarray_push_t(&graph->childSetHeads, JobTaskLinkId) = sentinel_u32;
  return id;
}

void jobs_graph_task_depend(JobGraph* graph, const JobTaskId parent, const JobTaskId child) {
  diag_assert(parent != child);

  // Increment the parent count of the child.
  ++(*dynarray_at_t(&graph->parentCounts, child, u32));

  // Add the child to the 'childSet' of the parent.
  JobTaskLinkId* parentChildSetHead = dynarray_at_t(&graph->childSetHeads, parent, JobTaskLinkId);
  jobs_graph_add_task_child_link(graph, child, parentChildSetHead);
}

bool jobs_graph_task_undepend(JobGraph* graph, JobTaskId parent, JobTaskId child) {
  diag_assert(parent != child);

  // Try to remove the child from the 'childSet' of the parent.
  JobTaskLinkId* parentChildSetHead = dynarray_at_t(&graph->childSetHeads, parent, JobTaskLinkId);
  if (jobs_graph_remove_task_child_link(graph, child, parentChildSetHead)) {

    // Decrement the parent count of the child.
    --(*dynarray_at_t(&graph->parentCounts, child, u32));

    return true;
  }
  return false; // No dependency existed between parent and child.
}

usize jobs_graph_reduce_dependencies(JobGraph* graph) {
  usize depsRemoved = 0;
  jobs_graph_for_task(graph, taskId) {
    depsRemoved += jobs_graph_task_reduce_dependencies(graph, taskId);
  }
  return depsRemoved;
}

bool jobs_graph_validate(const JobGraph* graph) { return !jobs_graph_has_cycle(graph); }

usize jobs_graph_task_count(const JobGraph* graph) { return graph->tasks.size; }

usize jobs_graph_task_root_count(const JobGraph* graph) {
  usize count = 0;
  jobs_graph_for_task(graph, taskId) { count += !jobs_graph_task_has_parent(graph, taskId); }
  return count;
}

usize jobs_graph_task_leaf_count(const JobGraph* graph) {
  usize count = 0;
  jobs_graph_for_task(graph, taskId) { count += !jobs_graph_task_has_child(graph, taskId); }
  return count;
}

String jobs_graph_name(const JobGraph* graph) { return graph->name; }

String jobs_graph_task_name(const JobGraph* graph, JobTaskId id) {
  return dynarray_at_t(&graph->tasks, id, JobTask)->name;
}

bool jobs_graph_task_has_parent(const JobGraph* graph, const JobTaskId task) {
  return jobs_graph_task_parent_count(graph, task) != 0;
}

bool jobs_graph_task_has_child(const JobGraph* graph, const JobTaskId task) {
  const JobTaskLinkId childSetHead = *dynarray_at_t(&graph->childSetHeads, task, JobTaskLinkId);
  return !sentinel_check(childSetHead);
}

usize jobs_graph_task_parent_count(const JobGraph* graph, const JobTaskId task) {
  return *dynarray_at_t(&graph->parentCounts, task, u32);
}

JobTaskChildItr jobs_graph_task_child_begin(const JobGraph* graph, const JobTaskId task) {
  const JobTaskLinkId childSetHead = *dynarray_at_t(&graph->childSetHeads, task, JobTaskLinkId);
  return jobs_graph_task_child_next(graph, (JobTaskChildItr){.next = childSetHead});
}

JobTaskChildItr jobs_graph_task_child_next(const JobGraph* graph, const JobTaskChildItr itr) {
  if (sentinel_check(itr.next)) {
    return (JobTaskChildItr){.task = sentinel_u32, .next = sentinel_u32};
  }
  const JobTaskLink link = *jobs_graph_task_link(graph, itr.next);
  return (JobTaskChildItr){.task = link.task, .next = link.next};
}

usize jobs_graph_task_span(const JobGraph* graph) { return jobs_graph_longestpath(graph); }

f32 jobs_graph_task_parallelism(const JobGraph* graph) {
  return (f32)jobs_graph_task_count(graph) / (f32)jobs_graph_task_span(graph);
}
