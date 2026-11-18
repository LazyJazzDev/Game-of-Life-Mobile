#include "DesktopGameSession.h"

#include <cstdlib>

#include "../../../mobile/AgentTools.h"
#include "demo/gol/game_of_life_gui.h"

DesktopGameSession::DesktopGameSession(grassland::rhi::BackendAPI backend) {
  const char *size = std::getenv("LONGMARCH_SMOKE_GOL_SIZE");
  const int extent = size ? std::clamp(std::atoi(size), grid_size::kMin, grid_size::kMax) : 0;
  life_ = std::make_unique<life_demo::GameOfLife>("Game of Life", 1280, 720, extent ? extent : grid_size::kDefault,
                                                  extent ? extent : grid_size::kDefault, backend, true);
  life_->EnableTiledSimulation();
  life_->EnableNativeConfirmation();
  if (extent) {
    life_->SetRandomInitialCells(.3f, 42);
    life_->SetInitialPlaying(true);
  }
  if (!life_->InitializeHosted())
    throw std::runtime_error("Cannot initialize RHI renderer");
}

DesktopGameSession::~DesktopGameSession() {
  life_->CloseHosted();
}

void DesktopGameSession::Render() {
  if (!life_->RenderHostedFrame())
    throw std::runtime_error("RHI rendering failed");
}

void DesktopGameSession::Resize(int width, int height) {
  Window()->UpdateHostedSize({width, height}, {width, height});
}

grassland::rhi::IDevice *DesktopGameSession::Device() const {
  return life_->Device();
}

grassland::rhi::IImage *DesktopGameSession::Image() const {
  return life_->PresentedImage();
}

life_demo::HostedWindow *DesktopGameSession::Window() const {
  return life_->HostedInput();
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

void DesktopGameSession::SetControlPixelSize(float pixels) {
  life_->SetControlPixelSize(pixels);
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

bool DesktopGameSession::WaitGPU() {
  return life_->WaitGPU();
}

grassland::rhi::IQueue *DesktopGameSession::Queue() const {
  return life_->Queue();
}

int DesktopGameSession::ConfirmationRequest() const {
  return life_->ConfirmationRequest();
}

void DesktopGameSession::CompleteConfirmation(bool accepted) {
  life_->CompleteConfirmation(accepted);
}

std::string DesktopGameSession::SaveSession(const std::string &path) {
  return life_->SaveSession(path);
}

std::string DesktopGameSession::RestoreSession(const std::string &path) {
  agent_templates_.SetDirectory(std::filesystem::path(path).parent_path() / "AgentTemplatesBasic");
  return life_->RestoreSession(path);
}

std::string DesktopGameSession::AgentCommand(const std::string &json) {
  return mobile_agent::Execute(*life_, json, &agent_templates_, false, agent_patterns_);
}
