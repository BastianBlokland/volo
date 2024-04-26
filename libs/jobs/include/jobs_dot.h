#pragma once
#include "core_dynstring.h"
#include "core_file.h"
#include "jobs_graph.h"

/**
 * DOT (Graph Description Language) utilities.
 * More info: https://en.wikipedia.org/wiki/DOT_(graph_description_language)
 *
 * Useful for visualizing the job graphs for debug purposes.
 * Can be easily converted to an svg image using the GraphViz package (or various other tools):
 * 'dot -Tsvg -O graph.dot'
 */

/**
 * Write a DOT (Graph Description Language) digraph for the given JobGraph.
 */
void jobs_dot_write_graph(DynString*, const JobGraph*);

/**
 * Dump a DOT (Graph Description Language) digraph for the given JobGraph to a file.
 */
FileResult jobs_dot_dump_graph(File*, const JobGraph*);

/**
 * Dump a DOT (Graph Description Language) digraph for the given JobGraph to a file at the given
 * path.
 */
FileResult jobs_dot_dump_graph_to_path(String path, const JobGraph*);
FileResult jobs_dot_dump_graph_to_path_default(const JobGraph*);
