#pragma once
#include "../../mobile/AgentTemplates.h"
#include "demo/gol/application/hosted_window.h"
#include "grassland/rhi/rhi.h"

namespace life_demo {
class GameOfLife;
}

// The desktop Game of Life UI, hosted in a native view instead of a GLFW window.
class DesktopGameSession {
 public:
  explicit DesktopGameSession(grassland::rhi::BackendAPI backend = grassland::rhi::BACKEND_API_DEFAULT);
  ~DesktopGameSession();
  std::string AgentCommand(const std::string &json);

  void SetAgentBuiltinDirectory(const std::filesystem::path &path) {
    agent_templates_.SetBuiltinDirectory(path);
  }

  void Render();
  bool WaitGPU();
  grassland::rhi::IQueue *Queue() const;
  double NextFrameDelay() const;
  void ResetClock();
  void PrepareInput(bool sleeping);
  void Resize(int width, int height);
  void SetIconOrientation(float radians);
  void SetBottomControlInset(float height_fraction);
  void SetCutoutInsets(float left, float top, float right);
  void SetControlOcclusions(const std::vector<glm::vec4> &regions);
  void SetControlPixelSize(float pixels);
  void SetControlExtentLimit(float height_fraction);
  void EnableNativeSizeControls();
  glm::ivec2 TakeSizeControlRequest();
  glm::vec4 SizeControlBounds(int axis) const;
  void SetGridDimension(int axis, int value);
  grassland::rhi::IDevice *Device() const;
  grassland::rhi::IImage *Image() const;
  life_demo::HostedWindow *Window() const;
  int ConfirmationRequest() const;
  void CompleteConfirmation(bool accepted);
  std::string SaveSession(const std::string &path);
  std::string RestoreSession(const std::string &path);
  int FileRequest() const;
  std::string CompleteFile(const std::string &path);

 private:
  mobile_agent::TemplateStore agent_templates_;
  std::unique_ptr<life_demo::GameOfLife> life_;
};
