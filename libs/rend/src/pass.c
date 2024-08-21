#include "core_annotation.h"
#include "core_array.h"
#include "rend_pass.h"

const String g_rendPassNames[] = {
    string_static("Geometry"),
    string_static("Decal"),
    string_static("Fog"),
    string_static("FogBlur"),
    string_static("Shadow"),
    string_static("AmbientOcclusion"),
    string_static("Forward"),
    string_static("Distortion"),
    string_static("Bloom"),
    string_static("Post"),
};
ASSERT(array_elems(g_rendPassNames) == RendPass_Count, "Incorrect number of names");

String rend_pass_name(const RendPass pass) { return g_rendPassNames[pass]; }
