#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "graph_internal.h"
#include "jobs_graph.h"

static JobTaskLink* jobs_graph_task_link(const JobGraph* graph, JobTaskLinkId id) {
  return dynarray_at_t(&graph->childLinks, id, JobTaskLink);
}

/**
 * Add a new task to the end of the linked list of task children that starts at 'linkHead'.
 * Pass 'sentinel_u32' as 'linkHead' to create a new list.
 * Returns an identifier to the newly created node.
 */
static JobTaskLinkId
jobs_graph_add_task_child_link(JobGraph* graph, const JobTaskId childTask, JobTaskLinkId linkHead) {
  // Walk to the end of the sibling chain.
  // TODO: Consider storing an end link to avoid having to walk this each time.
  JobTaskLinkId lastLink = sentinel_u32;
  while (!sentinel_check(linkHead)) {
    lastLink = linkHead;
    linkHead = jobs_graph_task_link(graph, linkHead)->next;
  }
  // Create a new link.
  const JobTaskLinkId newLinkIdx                    = (JobTaskLinkId)graph->childLinks.size;
  *dynarray_push_t(&graph->childLinks, JobTaskLink) = (JobTaskLink){
      .task = childTask,
      .next = sentinel_u32,
  };
  // Add the new link to the last sibling.
  if (!sentinel_check(lastLink)) {
    jobs_graph_task_link(graph, lastLink)->next = newLinkIdx;
  }
  return newLinkIdx;
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

  jobs_graph_for_task_child(graph, task, child, {
    if (jobs_graph_has_task_cycle(graph, child.task, processed, processing)) {
      return true;
    }
  });

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
   * very long task chains. We might have to refactor this to avoid recursion in the future, however
   * my current assumption is that we won't have that long task chains (which would be bad for
   * performance anyway).
   */

  // Using scratch memory here limits us to 65536 tasks (with the current scratch budgets).
  BitSet processed  = alloc_alloc(g_alloc_scratch, bits_to_bytes(graph->tasks.size) + 1, 1);
  BitSet processing = alloc_alloc(g_alloc_scratch, bits_to_bytes(graph->tasks.size) + 1, 1);

  mem_set(processed, 0);
  mem_set(processing, 0);

  jobs_graph_for_task(graph, taskId, {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    if (jobs_graph_has_task_cycle(graph, taskId, processed, processing)) {
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
static void jobs_graph_topologically_insert(
    const JobGraph* graph, const JobTaskId task, BitSet processed, DynArray* sortedIndices) {
  /**
   * Do a 'Depth First Search' to insert the task and its children.
   *
   * Current implementation uses recursion to go down the branches, meaning its not stack safe for
   * very long task chains.
   */
  bitset_set(processed, task); // Mark the task as processed.

  jobs_graph_for_task_child(graph, task, child, {
    if (bitset_test(processed, child.task)) {
      continue; // Already processed.
    }
    jobs_graph_topologically_insert(graph, child.task, processed, sortedIndices);
  });

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
  jobs_graph_for_task(graph, taskId, {
    if (bitset_test(processed, taskId)) {
      continue; // Already processed.
    }
    jobs_graph_topologically_insert(graph, taskId, processed, &sortedTasks);
  });

  /**
   * Keep a distance per task in the graph.
   * Initialize to 'sentinel_usize' when the task has a parent or 1 when its a root task.
   */

  // Note: Unfortunate heap memory usage, but current scratch memory budgets would be too limiting.
  DynArray distances = dynarray_create_t(g_alloc_heap, usize, graph->tasks.size);
  dynarray_resize(&distances, graph->tasks.size);
  dynarray_for_t(&distances, usize, itr, {
    const JobTaskId taskId = (JobTaskId)itr_i;
    *itr                   = jobs_graph_task_has_parent(graph, taskId) ? sentinel_usize : 1;
  });

  usize maxDist = 1;
  for (usize i = sortedTasks.size; i-- != 0;) {
    const JobTaskId taskId      = *dynarray_at_t(&sortedTasks, i, JobTaskId);
    const usize     currentDist = *dynarray_at_t(&distances, taskId, usize);

    if (!sentinel_check(currentDist)) {
      jobs_graph_for_task_child(graph, taskId, child, {
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

JobGraph* jobs_graph_create(Allocator* alloc, const String name, const usize taskCapacity) {
  JobGraph* graph = alloc_alloc_t(alloc, JobGraph);
  *graph          = (JobGraph){
      .tasks         = dynarray_create_t(alloc, JobTask, taskCapacity),
      .parentCounts  = dynarray_create_t(alloc, u32, taskCapacity),
      .childSetHeads = dynarray_create_t(alloc, JobTaskLinkId, taskCapacity),
      .childLinks    = dynarray_create_t(alloc, JobTaskLink, taskCapacity),
      .name          = string_dup(alloc, name),
      .alloc         = alloc,
  };
  return graph;
}

void jobs_graph_destroy(JobGraph* graph) {
  dynarray_for_t(&graph->tasks, JobTask, t, { string_free(graph->alloc, t->name); });
  dynarray_destroy(&graph->tasks);

  dynarray_destroy(&graph->parentCounts);
  dynarray_destroy(&graph->childSetHeads);
  dynarray_destroy(&graph->childLinks);

  string_free(graph->alloc, graph->name);
  alloc_free_t(graph->alloc, graph);
}

JobTaskId jobs_graph_add_task(
    JobGraph* graph, const String name, const JobTaskRoutine routine, void* context) {
  const JobTaskId id                       = (JobTaskId)graph->tasks.size;
  *dynarray_push_t(&graph->tasks, JobTask) = (JobTask){
      .name    = string_dup(graph->alloc, name),
      .routine = routine,
      .context = context,
  };
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
  if (sentinel_check(*parentChildSetHead)) {
    *parentChildSetHead = jobs_graph_add_task_child_link(graph, child, sentinel_u32);
  } else {
    jobs_graph_add_task_child_link(graph, child, *parentChildSetHead);
  }
}

bool jobs_graph_validate(const JobGraph* graph) { return !jobs_graph_has_cycle(graph); }

usize jobs_graph_task_count(const JobGraph* graph) { return graph->tasks.size; }

usize jobs_graph_task_root_count(const JobGraph* graph) {
  usize count = 0;
  jobs_graph_for_task(graph, taskId, { count += !jobs_graph_task_has_parent(graph, taskId); });
  return count;
}

usize jobs_graph_task_leaf_count(const JobGraph* graph) {
  usize count = 0;
  jobs_graph_for_task(graph, taskId, { count += !jobs_graph_task_has_child(graph, taskId); });
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
