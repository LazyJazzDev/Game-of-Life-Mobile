#include "DemoSession.h"

#include "grassland/rhi/shader_cache.h"

using namespace grassland;
using namespace grassland::rhi;

DemoSession::DemoSession(const std::filesystem::path &resources, bool prepare, BackendAPI backend) {
  // A prepare build compiles Slang and writes the cache; apps only read it.
  ConfigureShaderCache({resources / "shaders", !prepare, backend == BACKEND_API_METAL});
  game_ = std::make_unique<DesktopGameSession>(backend);
}

DemoSession::~DemoSession() {
  try {
    game_->WaitGPU();
  } catch (...) {
  }
}
