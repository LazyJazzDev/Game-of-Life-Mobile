#include <fstream>
#include <iostream>

#include "game/gpu/draw_shader_recipes.h"
#include "game/gpu/shader_recipes.h"
#include "grassland/rhi/backend/backend_shader.h"
#include "grassland/rhi/backend/backend_shader_compiler.h"
#include "grassland/rhi/shader_cache.h"
#include "grassland/util/virtual_file_system.h"

using namespace grassland;
using namespace grassland::rhi;

// No GPU is needed: use the same preprocessing, binding maps and compiler as RHI.
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: gol_vulkan_shaders <game-dir> <cache-dir> prepare|replay\n";
    return 1;
  }
  const bool replay = std::string(argv[3]) == "replay";
  if (!replay && std::string(argv[3]) != "prepare")
    return 1;
  if (!ConfigureShaderCache({argv[2], replay, false, ShaderTargetPlatform::OHOS}))
    return 1;
  auto compile = [&](const char *file, const char *entry, ShaderStage stage, const PipelineLayoutDesc &layout,
                     const std::vector<InputAttributeDesc> &attributes,
                     const std::vector<std::pair<std::string, std::string>> &defines) {
    std::ifstream input(std::filesystem::path(argv[1]) / "shaders" / file);
    std::string source{std::istreambuf_iterator<char>(input), {}};
    if (source.empty())
      return false;
    VirtualFileSystem fs;
    if (fs.WriteFile("main.slang", source))
      return false;
    ShaderDesc desc;
    desc.entry_point = entry;
    desc.stage = stage;
    desc.file_system = &fs;
    desc.defines = defines;
    ObjPtr<IShader> shader = backend::CreateShader(desc, BACKEND_API_VULKAN);
    if (!shader)
      return false;
    std::vector<uint8_t> spirv;
    std::array<uint32_t, 3> threads{};
    if (!backend::CompileSlang(shader.Get(), backend::ShaderMacroHeader(layout, BACKEND_API_VULKAN, attributes),
                               BACKEND_API_VULKAN, spirv, &layout, stage == SHADER_STAGE_COMPUTE ? &threads : nullptr))
      return false;
    return spirv.size() >= 4 && spirv[0] == 3 && spirv[1] == 2 && spirv[2] == 35 && spirv[3] == 7;
  };
  using namespace life_demo::shaders;
  for (bool resolve : {false, true}) {
    PipelineLayoutDesc layout;
    layout.groups = {resolve ? ResolveLayout() : DrawLayout()};
    for (auto stage : {SHADER_STAGE_VERTEX, SHADER_STAGE_FRAGMENT})
      if (!compile(resolve ? "resolve.slang" : "super.slang", stage == SHADER_STAGE_VERTEX ? "VSMain" : "PSMain", stage,
                   layout, resolve ? std::vector<InputAttributeDesc>{} : DrawAttributes(), {}))
        return 1;
  }
  uint64_t words = 0;
  for (uint32_t level = 1; level <= 6; ++level) {
    const uint32_t side = 64 >> level;
    const uint32_t bits = level == 1 ? 4 : level <= 3 ? 8 : 16;
    words += (side * side * bits + 31) / 32;
  }
  for (bool vote : {false, true})
    for (uint32_t entry = 0; entry < 9; ++entry)
      if (!compile("tiled_life.slang", TiledEntry(entry, false), SHADER_STAGE_COMPUTE, TiledLayout(false), {},
                   TiledDefines(words, 6, 14, vote, true, false, 256, 64, 64)))
        return 1;
  std::cout << "PASS OHOS Vulkan " << (replay ? "replay" : "prepare") << " (22 shader requests)\n";
}
