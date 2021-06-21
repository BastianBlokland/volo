#pragma once
#include "core_string.h"
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Opaque identifier to a task in a job.
 * Note: Are assigned starting from 0.
 * Iteration from 0 to 'jobdef_task_count()' is a valid way to lookup tasks.
 */
typedef u32 JobTaskId;

/**
 * Iterator for iterating task children.
 */
typedef struct {
  JobTaskId task; // 'sentinel_u32' indicates that no child was found.
  u32       next;
} JobTaskChildItr;

/**
 * Routine to invoke to run the task.
 */
typedef void (*JobTaskRoutine)(void* context);

/**
 * Job Definition data structure.
 * Contains all tasks and dependencies between them.
 * Can be scheduled one or multiple times on the job system.
 * Note: JobDefintion should not be modified or destroyed while its running on the job system.
 */
typedef struct sJobDef JobDef;

/**
 * Iterate over all tasks in the Job Definition.
 */
#define jobdef_for_task(_JOBDEF_, _VAR_, ...)                                                      \
  {                                                                                                \
    for (JobTaskId _VAR_ = 0; _VAR_ != jobdef_task_count(_JOBDEF_); ++_VAR_) {                     \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Iterate over all child tasks for a task in the given Job Definition.
 */
#define jobdef_for_task_child(_JOBDEF_, _TASK_, _VAR_, ...)                                        \
  {                                                                                                \
    for (JobTaskChildItr _VAR_ = jobdef_task_child_begin(_JOBDEF_, _TASK_);                        \
         !sentinel_check(_VAR_.task);                                                              \
         _VAR_ = jobdef_task_child_next(_JOBDEF_, _VAR_)) {                                        \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Create a new job definition.
 * Note: 'taskCapacity' is only the initial capacity, more space is automatically allocated when
 * required. Capacity of 0 is legal and will allocate memory when the first task is added.
 * Should be destroyed using 'jobdef_destroy()'.
 */
JobDef* jobdef_create(Allocator*, String name, usize taskCapacity);

/**
 * Destroy a Job Definition.
 * Pre-condition: Job is not running at the moment.
 */
void jobdef_destroy(JobDef*);

/**
 * Add a new task to the job.
 * Context is provided to the 'JobTaskRoutine' when the task is executed.
 * Pre-condition: Job is not running at the moment.
 */
JobTaskId jobdef_add_task(JobDef*, String name, JobTaskRoutine, void* context);

/**
 * Register a dependency between two tasks. The child task will only be started after the parent
 * task has finished.
 * Pre-condition: Job is not running at the moment.
 * Pre-condition: parent != child.
 */
void jobdef_task_depend(JobDef*, JobTaskId parent, JobTaskId child);

/**
 * Validate the given job definition.
 * Checks:
 * - Definition does not contain cycles.
 */
bool jobdef_validate(const JobDef*);

/**
 * Return the number of tasks registered to the given job.
 */
usize jobdef_task_count(const JobDef*);

/**
 * Return the number of root tasks registered to the given job.
 */
usize jobdef_task_root_count(const JobDef*);

/**
 * Return the number of leaf tasks registered to the given job.
 */
usize jobdef_task_leaf_count(const JobDef*);

/**
 * Retrieve the name of a job-definition.
 */
String jobdef_job_name(const JobDef*);

/**
 * Retrieve the name of a task in a job-defintion.
 */
String jobdef_task_name(const JobDef*, JobTaskId);

/**
 * Check if the task has a parent dependency.
 */
bool jobdef_task_has_parent(const JobDef*, JobTaskId);

/**
 * Check if the task has a child depending on it.
 */
bool jobdef_task_has_child(const JobDef*, JobTaskId);

/**
 * Count how many parents (dependencies) a job has.
 */
usize jobdef_task_parent_count(const JobDef*, JobTaskId);

/**
 * Create an iterator for iterating over the children of the given task.
 * Note: Returns an interator with 'task' set to 'sentinel_u32' when the given task has no children.
 */
JobTaskChildItr jobdef_task_child_begin(const JobDef*, JobTaskId);

/**
 * Advance the task child iterator.
 * Note: Returns an interator with 'task' set to 'sentinel_u32' when there is no next child.
 */
JobTaskChildItr jobdef_task_child_next(const JobDef*, JobTaskChildItr);

/**
 * Calculate the job span (longest serial path through the graph).
 * aka 'Critical-Path Length' / 'Computational Depth'.
 */
usize jobdef_task_span(const JobDef*);

/**
 * Maximum theoretical speedup when using an infinite number of processors.
 * Defined as jobdef_task_count() / jobdef_task_span(). Each task is considered equal in this
 * calculation.
 */
f32 jobdef_task_parallelism(const JobDef*);
