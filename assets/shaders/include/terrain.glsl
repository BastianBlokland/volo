#ifndef INCLUDE_TERRAIN
#define INCLUDE_TERRAIN

struct TerrainData {
  f32 size;
  f32 heightScale;
  f32 patchScale;
};

struct TerrainPatchData {
  f32 posX, posZ;
  f32 texU, texV;
};

const u32 c_terrainMaxPatches = 512;

#endif // INCLUDE_TERRAIN
