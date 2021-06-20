#include "core_alloc.h"
#include "core_diag.h"
#include "core_sentinel.h"
#include "jobdef_internal.h"

static JobTaskLink* jobdef_task_link(JobDef* jobDef, JobTaskLinkId id) {
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

usize jobdef_task_count(JobDef* jobDef) { return jobDef->tasks.size; }

String jobdef_job_name(JobDef* jobDef) { return jobDef->name; }

String jobdef_task_name(JobDef* jobDef, JobTaskId id) {
  return dynarray_at_t(&jobDef->tasks, id, JobTask)->name;
}

bool jobdef_task_has_parent(JobDef* jobDef, const JobTaskId task) {
  const u32 parentCount = *dynarray_at_t(&jobDef->parentCounts, task, u32);
  return parentCount != 0;
}

bool jobdef_task_has_child(JobDef* jobDef, const JobTaskId task) {
  const JobTaskLinkId childSetHead = *dynarray_at_t(&jobDef->childSetHeads, task, JobTaskLinkId);
  return !sentinel_check(childSetHead);
}

JobTaskChildItr jobdef_task_child_begin(JobDef* jobDef, const JobTaskId task) {
  const JobTaskLinkId childSetHead = *dynarray_at_t(&jobDef->childSetHeads, task, JobTaskLinkId);
  return jobdef_task_child_next(jobDef, (JobTaskChildItr){.next = childSetHead});
}

JobTaskChildItr jobdef_task_child_next(JobDef* jobDef, const JobTaskChildItr itr) {
  if (sentinel_check(itr.next)) {
    return (JobTaskChildItr){.task = sentinel_u32, .next = sentinel_u32};
  }
  const JobTaskLink link = *jobdef_task_link(jobDef, itr.next);
  return (JobTaskChildItr){.task = link.task, .next = link.next};
}
