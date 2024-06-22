#include "core_diag.h"

#include "loader_internal.h"

#define asset_loader_name(_NAME_) asset_load_##_NAME_

AssetLoader asset_loader(const AssetFormat format) {
#define RET_LOADER(_NAME_)                                                                         \
  void asset_loader_name(_NAME_)(EcsWorld*, String, EcsEntityId, AssetSource*);                    \
  return &asset_loader_name(_NAME_)

  // clang-format off
  switch (format) {
  case AssetFormat_ArrayTex:  { RET_LOADER(arraytex);   }
  case AssetFormat_Atlas:     { RET_LOADER(atlas);      }
  case AssetFormat_Bin:       { RET_LOADER(raw);        }
  case AssetFormat_Cursor:    { RET_LOADER(cursor);     }
  case AssetFormat_Decal:     { RET_LOADER(decal);      }
  case AssetFormat_FontTex:   { RET_LOADER(fonttex);    }
  case AssetFormat_Gltf:      { RET_LOADER(gltf);       }
  case AssetFormat_Graphic:   { RET_LOADER(graphic);    }
  case AssetFormat_Inputs:    { RET_LOADER(inputs);     }
  case AssetFormat_Level:     { RET_LOADER(level);      }
  case AssetFormat_Obj:       { RET_LOADER(obj);        }
  case AssetFormat_Ppm:       { RET_LOADER(ppm);        }
  case AssetFormat_Prefabs:   { RET_LOADER(prefabs);    }
  case AssetFormat_ProcMesh:  { RET_LOADER(procmesh);   }
  case AssetFormat_ProcTex:   { RET_LOADER(proctex);    }
  case AssetFormat_Products:  { RET_LOADER(products);   }
  case AssetFormat_R16:       { RET_LOADER(r16);        }
  case AssetFormat_R32:       { RET_LOADER(r32);        }
  case AssetFormat_Raw:       { RET_LOADER(raw);        }
  case AssetFormat_Script:    { RET_LOADER(script);     }
  case AssetFormat_Spv:       { RET_LOADER(spv);        }
  case AssetFormat_Terrain:   { RET_LOADER(terrain);    }
  case AssetFormat_Tga:       { RET_LOADER(tga);        }
  case AssetFormat_Ttf:       { RET_LOADER(ttf);        }
  case AssetFormat_Vfx:       { RET_LOADER(vfx);        }
  case AssetFormat_Wav:       { RET_LOADER(wav);        }
  case AssetFormat_Weapons:   { RET_LOADER(weapons);    }
  case AssetFormat_Count:
    break;
  }
  // clang-format off

#undef RET_LOADER
  diag_crash_msg("No asset loader defined for format: {}", fmt_int(format));
}
