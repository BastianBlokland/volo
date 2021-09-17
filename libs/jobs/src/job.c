#include "core_alloc.h"
#include "jobs_graph.h"

#include "job_internal.h"

Job* job_create(Allocator* alloc, const JobId id, const JobGraph* graph) {
  const usize size = sizeof(Job) + sizeof(JobTaskData) * jobs_graph_task_count(graph);
  Job*        data = alloc_alloc(alloc, size, alignof(Job)).ptr;

  // Initialize per-job data.
  data->id           = id;
  data->graph        = graph;
  data->dependencies = (i64)jobs_graph_task_leaf_count(graph);

  // Initialize per-task data.
  jobs_graph_for_task(graph, taskId, {
    data->taskData[taskId].dependencies = (i64)jobs_graph_task_parent_count(graph, taskId);
  });

  return data;
}

void job_destroy(Allocator* alloc, Job* job) {
  const usize size = sizeof(Job) + sizeof(JobTaskData) * jobs_graph_task_count(job->graph);
  alloc_free(alloc, mem_create(job, size));
}
