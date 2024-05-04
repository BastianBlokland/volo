#pragma once
#include "core_string.h"
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Opaque identifier to a task in a job.
 * NOTE: Are assigned starting from 0.
 * Iteration from 0 to 'jobs_graph_task_count()' is a valid way to lookup tasks.
 */
typedef u16 JobTaskId;

typedef enum {
  JobTaskFlags_None = 0,

  /**
   * The task should always be run on the same thread.
   * NOTE: Incurs an additional scheduling overhead.
   */
  JobTaskFlags_ThreadAffinity = 1 << 0,

  /**
   * Do not create an owning copy of the task name.
   * NOTE: Care must be taken that the name has a longer lifetime then the graph.
   */
  JobTaskFlags_BorrowName = 1 << 1,

} JobTaskFlags;

/**
 * Iterator for iterating task children.
 */
typedef struct {
  JobTaskId task; // 'sentinel_u16' indicates that no child was found.
  u16       next;
} JobTaskChildItr;

/**
 * Routine to invoke to run the task.
 * 'context' is a pointer to the memory that was given when adding the task to the graph.
 */
typedef void (*JobTaskRoutine)(const void* context);

/**
 * Routine to estimate the cost of a single task.
 * NOTE: Units of the cost are up to caller (could be used as nanoseconds for example).
 */
typedef u64 (*JobsCostEstimator)(const void* userCtx, JobTaskId);

/**
 * JobGraph data structure.
 * Contains all tasks and dependencies between them.
 * Can be scheduled one or multiple times on the job system.
 * NOTE: JobGraph should not be modified or destroyed while its running on the job system.
 */
typedef struct sJobGraph JobGraph;

/**
 * Iterate over all tasks in the JobGraph.
 */
#define jobs_graph_for_task(_GRAPH_, _VAR_)                                                        \
  for (JobTaskId _VAR_ = 0; _VAR_ != jobs_graph_task_count(_GRAPH_); ++_VAR_)

/**
 * Iterate over all child tasks for a task in the given JobGraph.
 */
#define jobs_graph_for_task_child(_GRAPH_, _TASK_, _VAR_)                                          \
  for (JobTaskChildItr _VAR_ = jobs_graph_task_child_begin(_GRAPH_, _TASK_);                       \
       !sentinel_check(_VAR_.task);                                                                \
       _VAR_ = jobs_graph_task_child_next(_GRAPH_, _VAR_))

/**
 * Create a new JobGraph.
 * NOTE: 'taskCapacity' is only the initial capacity, more space is automatically allocated when
 * required. Capacity of 0 is legal and will allocate memory when the first task is added.
 * Should be destroyed using 'jobgraph_destroy()'.
 */
JobGraph* jobs_graph_create(Allocator*, String name, u32 taskCapacity);

/**
 * Destroy a JobGraph.
 * Pre-condition: JobGraph is not running at the moment.
 */
void jobs_graph_destroy(JobGraph*);

/**
 * Clear all registered tasks from a JobGraph.
 * Pre-condition: JobGraph is not running at the moment.
 */
void jobs_graph_clear(JobGraph*);

/**
 * Copy the tasks and dependencies from the source graph into the destination graph.
 * NOTE: The allocator and name are preserved from the original graph.
 * Pre-condition: dst JobGraph is not running at the moment.
 */
void jobs_graph_copy(JobGraph* dst, JobGraph* src);

/**
 * Add a new task to the graph.
 * 'ctx' is provided to the 'JobTaskRoutine' when the task is executed.
 * NOTE: 'ctx' is copied into the graph and has the same lifetime as the graph.
 * NOTE: 'ctx' memory will always be 16 byte aligned.
 * NOTE: Task id's are allocated linearly, sequential calls to add_task get sequential ids.
 *
 * Pre-condition: JobGraph is not running at the moment.
 * Pre-condition: ctx.size <= 32.
 */
JobTaskId jobs_graph_add_task(JobGraph*, String name, JobTaskRoutine, Mem ctx, JobTaskFlags);

/**
 * Register a dependency between two tasks. The child task will only be started after the parent
 * task has finished.
 * Pre-condition: JobGraph is not running at the moment.
 * Pre-condition: parent != child.
 */
void jobs_graph_task_depend(JobGraph*, JobTaskId parent, JobTaskId child);

/**
 * Remove a dependency between two tasks if it exists.
 * Returns true if a dependency was found (and removed) between parent and child.
 *
 * Pre-condition: JobGraph is not running at the moment.
 * Pre-condition: parent != child.
 */
bool jobs_graph_task_undepend(JobGraph*, JobTaskId parent, JobTaskId child);

/**
 * Remove all unnecessary dependencies
 * This performs a 'Transitive Reduction' to remove dependencies while still keeping an equivalent
 * graph. More info: https://en.wikipedia.org/wiki/Transitive_reduction
 * Returns the amount of dependencies removed.
 *
 * NOTE: This is a relatively expensive operation (at least O(tasks * dependencies)).
 *
 * Pre-condition: JobGraph is not running at the moment.
 */
u32 jobs_graph_reduce_dependencies(JobGraph*);

/**
 * Validate the given JobGraph.
 * Checks:
 * - Graph does not contain cycles.
 */
bool jobs_graph_validate(const JobGraph*);

/**
 * Return the number of tasks registered to the given graph.
 */
u32 jobs_graph_task_count(const JobGraph*);

/**
 * Return the number of root tasks registered to the given graph.
 */
u32 jobs_graph_task_root_count(const JobGraph*);

/**
 * Return the number of leaf tasks registered to the given graph.
 */
u32 jobs_graph_task_leaf_count(const JobGraph*);

/**
 * Retrieve the name of a graph.
 */
String jobs_graph_name(const JobGraph*);

/**
 * Retrieve the name of a task in the graph.
 */
String jobs_graph_task_name(const JobGraph*, JobTaskId);

/**
 * Retrieve the user context associated with the given task.
 */
Mem jobs_graph_task_ctx(const JobGraph*, JobTaskId);

/**
 * Check if the task has a parent dependency.
 */
bool jobs_graph_task_has_parent(const JobGraph*, JobTaskId);

/**
 * Check if the task has a child depending on it.
 */
bool jobs_graph_task_has_child(const JobGraph*, JobTaskId);

/**
 * Count how many parents (dependencies) a job has.
 */
u32 jobs_graph_task_parent_count(const JobGraph*, JobTaskId);

/**
 * Create an iterator for iterating over the children of the given task.
 * NOTE: Returns an interator with 'task' set to 'sentinel_u16' when the given task has no children.
 */
JobTaskChildItr jobs_graph_task_child_begin(const JobGraph*, JobTaskId);

/**
 * Advance the task child iterator.
 * NOTE: Returns an interator with 'task' set to 'sentinel_u16' when there is no next child.
 */
JobTaskChildItr jobs_graph_task_child_next(const JobGraph*, JobTaskChildItr);

/**
 * Calculate the job span (longest serial path through the graph).
 */
u64 jobs_graph_task_span(const JobGraph*);
u64 jobs_graph_task_span_cost(const JobGraph*, JobsCostEstimator, const void* userCtx);
