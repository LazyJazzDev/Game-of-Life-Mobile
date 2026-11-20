#pragma once
#include <filesystem>
#include <memory>

#include "games/DesktopGameSession.h"
#include "grassland/rhi/rhi.h"
#include "grassland/rhi/shader_cache.h"

// Configures the prepared RHI shader bundle and hosts the Game of Life.
class DemoSession {
 public:
  DemoSession(const std::filesystem::path &resources,
              bool prepare = false,
              grassland::rhi::BackendAPI backend = grassland::rhi::BACKEND_API_DEFAULT);
  ~DemoSession();

  std::string AgentCommand(const std::string &json) {
    // A serial GCD queue can migrate between OS threads. Rebind the thread-local
    // offline cache before lazily creating research-canvas GPU resources.
    if (!grassland::rhi::ConfigureShaderCache(shader_cache_))
      return R"({"ok":false,"error":"Cannot configure shader cache"})";
    return game_->AgentCommand(json);
  }

  void Resize(int width, int height) {
    game_->Resize(width, height);
  }

  void Render() {
    game_->Render();
  }

  grassland::rhi::IDevice *Device() const {
    return game_->Device();
  }

  grassland::rhi::IImage *Image() const {
    return game_->Image();
  }

  DesktopGameSession *Game() const {
    return game_.get();
  }

 private:
  grassland::rhi::ShaderCacheSettings shader_cache_;
  std::unique_ptr<DesktopGameSession> game_;
};
