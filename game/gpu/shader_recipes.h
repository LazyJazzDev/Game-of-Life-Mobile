#pragma once
#include "grassland/rhi/rhi_program.h"

// Shared by runtime and offline preparation; pipeline ABI has one source of truth.
namespace life_demo::shaders {
using namespace grassland::rhi;

inline PipelineLayoutDesc TiledLayout(bool lookup) {
  grassland::rhi::PipelineLayoutDesc layout;
  layout.groups = {{{{0, BINDING_RW_BYTE_ADDRESS_BUFFER, 1, SHADER_VISIBILITY_COMPUTE},
                     {1, BINDING_RW_BYTE_ADDRESS_BUFFER, 1, SHADER_VISIBILITY_COMPUTE},
                     {2, BINDING_UNIFORM_BUFFER, 1, SHADER_VISIBILITY_COMPUTE},
                     {3, BINDING_RW_BYTE_ADDRESS_BUFFER, 1, SHADER_VISIBILITY_COMPUTE},
                     {5, BINDING_RW_BYTE_ADDRESS_BUFFER, 1, SHADER_VISIBILITY_COMPUTE}}}};
  if (lookup)
    layout.groups[0].bindings.push_back({4, BINDING_BYTE_ADDRESS_BUFFER, 1, SHADER_VISIBILITY_COMPUTE});
  return layout;
}

inline const char *TiledEntry(uint32_t index, bool lookup) {
  const char *entries[] = {lookup ? "StepLookup" : "Step",
                           "RefreshHalo",
                           "Toggle",
                           "Fill",
                           "MipLocal",
                           "MipGlobal",
                           "ViewPeak",
                           "ViewPeakReduce",
                           "ViewGainSmooth"};
  return index < 9 ? entries[index] : nullptr;
}

inline std::vector<std::pair<std::string, std::string>> TiledDefines(uint64_t local_words,
                                                                     uint32_t tile_levels,
                                                                     uint32_t world_levels,
                                                                     bool subgroup,
                                                                     bool sparse,
                                                                     bool lookup,
                                                                     uint32_t compute_axis,
                                                                     uint32_t tiles_per_axis,
                                                                     uint32_t compute_tile_size) {
  return {
      {"MIP_LOCAL_WORDS", std::to_string(local_words)},
      {"TILE_LEVELS", std::to_string(tile_levels)},
      {"WORLD_LEVELS", std::to_string(world_levels)},
      {"USE_SUBGROUP_VOTE", subgroup ? "1" : "0"},
      {"USE_SPARSE", sparse ? "1" : "0"},
      {"USE_LOOKUP", lookup ? "1" : "0"},
      {"TILES_AXIS", std::to_string(compute_axis)},
      {"STORAGE_TILES_AXIS", std::to_string(tiles_per_axis)},
      {"TILE_SIZE", std::to_string(compute_tile_size)},
      {"GROUP_THREADS", std::to_string(std::min(256u, compute_tile_size * compute_tile_size / (lookup ? 64u : 32u)))}};
}
}  // namespace life_demo::shaders
