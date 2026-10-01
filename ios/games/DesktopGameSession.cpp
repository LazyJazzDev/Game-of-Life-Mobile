#include "DesktopGameSession.h"

#include <cstdlib>

#include "demo/gol/game_of_life_gui.h"

DesktopGameSession::DesktopGameSession(grassland::graphics::BackendAPI backend) {
  const char *size = std::getenv("LONGMARCH_SMOKE_GOL_SIZE");
  const int extent = size ? std::clamp(std::atoi(size), grid_size::kMin, grid_size::kMax) : 0;
  life_ = std::make_unique<life_demo::GameOfLife>("Game of Life", 1280, 720, extent ? extent : grid_size::kDefault,
                                                  extent ? extent : grid_size::kDefault, backend, true);
  if (extent) {
    life_->SetRandomInitialCells(.3f, 42);
    life_->SetInitialPlaying(true);
  }
  life_->InitializeHosted();
}

DesktopGameSession::~DesktopGameSession() {
  life_->CloseHosted();
}

void DesktopGameSession::Render() {
  life_->RenderHostedFrame();
}

void DesktopGameSession::Resize(int width, int height) {
  Window()->UpdateHostedSize({width, height}, {width, height});
}

grassland::graphics::Core *DesktopGameSession::Core() const {
  return life_->Core();
}

grassland::graphics::Image *DesktopGameSession::Image() const {
  return life_->PresentedImage();
}

grassland::graphics::Window *DesktopGameSession::Window() const {
  return life_->GetWindow();
}

int DesktopGameSession::FileRequest() const {
  return life_->HostedFileRequest();
}

std::string DesktopGameSession::CompleteFile(const std::string &path) {
  return life_->CompleteHostedFile(path);
}

void DesktopGameSession::ResetClock() {
  life_->ResetFrameClock();
}

double DesktopGameSession::NextFrameDelay() const {
  return life_->NextFrameDelay();
}

void DesktopGameSession::PrepareInput(bool sleeping) {
  if (sleeping)
    ResetClock();
  else
    life_->ResetAnimationClock();
}

void DesktopGameSession::SetIconOrientation(float radians) {
  life_->SetIconOrientation(radians);
}

void DesktopGameSession::SetBottomControlInset(float height_fraction) {
  life_->SetBottomControlInset(height_fraction);
}

void DesktopGameSession::SetCutoutInsets(float left, float top, float right) {
  life_->SetCutoutInsets(left, top, right);
}

void DesktopGameSession::SetControlExtentLimit(float height_fraction) {
  life_->SetControlExtentLimit(height_fraction);
}

void DesktopGameSession::EnableNativeSizeControls() {
  life_->EnableNativeSizeControls();
}

glm::ivec2 DesktopGameSession::TakeSizeControlRequest() {
  return life_->TakeSizeControlRequest();
}

glm::vec4 DesktopGameSession::SizeControlBounds(int axis) const {
  return life_->SizeControlBounds(axis);
}

void DesktopGameSession::SetGridDimension(int axis, int value) {
  life_->SetGridDimension(axis, value);
}
