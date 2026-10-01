#pragma once
#include <rawfile/raw_file_manager.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

#include "Surface.h"
#include "game/DesktopGameSession.h"

namespace longmarch::harmony {
// Runs Game of Life on one render thread: ArkUI posts JSON commands and polls a
// JSON status; frames are scheduled on demand from the game's next deadline.
class Host {
 public:
  static Host &Get();
  void Initialize(NativeResourceManager *manager, std::string files_dir);
  void Command(std::string json);
  void Attach(OHNativeWindow *window, uint32_t width, uint32_t height);
  void Detach();
  std::string Status();
  ~Host();

 private:
  Host();
  void Post(std::function<void()> work);
  void Run();
  void Execute(const std::string &json);
  void Frame();
  void Publish();
  void Stop();
  void Resize();
  void Extract(NativeResourceManager *manager, const std::string &files_dir);

  std::mutex mutex_, status_mutex_;
  std::condition_variable changed_;
  std::deque<std::function<void()>> commands_;
  std::thread thread_;
  bool quit_ = false;
  std::string status_ = "{}";
  std::unique_ptr<DesktopGameSession> game_;
  std::unique_ptr<Surface> surface_;
  OHNativeWindow *window_ = nullptr;
  uint32_t width_ = 0, height_ = 0;
  std::filesystem::path resources_;
  std::string error_;
  bool ready_ = false, active_ = true, dirty_ = false;
  int size_axis_ = 0, size_value_ = 64, file_revision_ = 0;
  uint64_t frames_ = 0;
  // Fraction of the surface height kept clear of the system gesture area, as on iOS.
  float bottom_inset_ = 0;
  double fps_ = 0;
  double delay_ = 0;
  std::chrono::steady_clock::time_point deadline_{}, last_present_{};
};
}  // namespace longmarch::harmony
