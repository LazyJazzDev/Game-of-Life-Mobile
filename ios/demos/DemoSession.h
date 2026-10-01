#pragma once
#include <filesystem>
#include <memory>

#include "games/DesktopGameSession.h"
#include "grassland/graphics/graphics.h"

// Configures the prepared shader bundle and hosts the Game of Life. LongMarch's
// DemoSession also runs the graphics and N-body demos; this app has only the game.
class DemoSession {
 public:
  DemoSession(const std::filesystem::path &resources,
              bool prepare = false,
              grassland::graphics::BackendAPI backend = grassland::graphics::BACKEND_API_DEFAULT);
  ~DemoSession();

  void Resize(int width, int height) {
    game_->Resize(width, height);
  }

  void Render() {
    game_->Render();
  }

  grassland::graphics::Core *Core() const {
    return game_->Core();
  }

  grassland::graphics::Image *Image() const {
    return game_->Image();
  }

  DesktopGameSession *Game() const {
    return game_.get();
  }

 private:
  std::unique_ptr<DesktopGameSession> game_;
};
