#pragma once
#include <filesystem>
#include <memory>

#include "games/DesktopGameSession.h"
#include "grassland/rhi/rhi.h"

// Configures the prepared RHI shader bundle and hosts the Game of Life.
class DemoSession {
 public:
  DemoSession(const std::filesystem::path &resources,
              bool prepare = false,
              grassland::rhi::BackendAPI backend = grassland::rhi::BACKEND_API_DEFAULT);
  ~DemoSession();

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
  std::unique_ptr<DesktopGameSession> game_;
};
