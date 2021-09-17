#pragma once
#include "core_dynstring.h"
#include "jobs_graph.h"

/**
 * DOT (Graph Description Language) utilities.
 * More info: https://en.wikipedia.org/wiki/DOT_(graph_description_language)
 *
 * Usefull for visualizing the job graphs for debug purposes.
 * Can be easily converted to an svg image using the GraphViz package (or various other tools):
 * 'dot -Tsvg -O graph.dot'
 */

// Forward declare from 'core_file.h'.
typedef struct sFile File;

/**
 * Write a DOT (Graph Description Language) digraph for the given JobGraph.
 */
void jobs_dot_write_graph(DynString*, JobGraph*);

/**
 * Dump a DOT (Graph Description Language) digraph for the given JobGraph to a file.
 */
void jobs_dot_dump_graph(File*, JobGraph*);
