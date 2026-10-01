#include "DemoSession.h"

#include "grassland/graphics/shader_cache.h"

using namespace grassland;
using namespace grassland::graphics;

DemoSession::DemoSession(const std::filesystem::path &resources, bool prepare, BackendAPI backend) {
  // A prepare build compiles Slang and writes the cache; apps only read it.
  ConfigureShaderCache({resources / "shaders", !prepare, backend == BACKEND_API_METAL});
  game_ = std::make_unique<DesktopGameSession>(backend);
}

DemoSession::~DemoSession() {
  try {
    Core()->WaitGPU();
  } catch (...) {
  }
}
