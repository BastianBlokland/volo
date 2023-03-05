#include "core_annotation.h"
#include "core_array.h"
#include "rend_pass.h"

const String g_rendPassNames[] = {
    string_static("geometry"),
    string_static("forward"),
    string_static("post"),
    string_static("shadow"),
    string_static("ambient-occlusion"),
    string_static("bloom"),
    string_static("distortion"),
};
ASSERT(array_elems(g_rendPassNames) == RendPass_Count, "Incorrect number of names");

String rend_pass_name(const RendPass pass) { return g_rendPassNames[pass]; }
