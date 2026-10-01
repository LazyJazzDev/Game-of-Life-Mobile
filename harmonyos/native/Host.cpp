#include "Host.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rawfile/raw_file.h>

#include <cmath>
#include <fstream>
#include <future>

#include "grassland/graphics/sha256.h"

namespace longmarch::harmony {
namespace {
std::string Text(const rapidjson::Value &v, const char *key, const char *fallback = "") {
  return v.IsObject() && v.HasMember(key) && v[key].IsString() ? v[key].GetString() : fallback;
}

double Number(const rapidjson::Value &v, const char *key, double fallback = 0) {
  double result = v.IsObject() && v.HasMember(key) && v[key].IsNumber() ? v[key].GetDouble() : fallback;
  return std::isfinite(result) ? result : fallback;
}

bool Boolean(const rapidjson::Value &v, const char *key, bool fallback = false) {
  return v.IsObject() && v.HasMember(key) && v[key].IsBool() ? v[key].GetBool() : fallback;
}

std::string Raw(NativeResourceManager *manager, const std::string &name) {
  auto *file = OH_ResourceManager_OpenRawFile(manager, name.c_str());
  if (!file)
    throw std::runtime_error("Missing bundled resource " + name);
  std::unique_ptr<RawFile, decltype(&OH_ResourceManager_CloseRawFile)> owner(file, OH_ResourceManager_CloseRawFile);
  const long length = OH_ResourceManager_GetRawFileSize(file);
  if (length < 0 || length > 256 * 1024 * 1024)
    throw std::runtime_error("Invalid resource size " + name);
  std::string data(static_cast<size_t>(length), '\0');
  size_t offset = 0;
  while (offset < data.size()) {
    int count =
        OH_ResourceManager_ReadRawFile(file, data.data() + offset, std::min(size_t(1 << 20), data.size() - offset));
    if (count <= 0)
      throw std::runtime_error("Cannot read bundled resource " + name);
    offset += count;
  }
  return data;
}

std::string Digest(const std::string &data) {
  grassland::graphics::detail::SHA256 hash;
  hash.Update(data);
  return hash.Finish();
}
}  // namespace

Host &Host::Get() {
  static Host host;
  return host;
}

Host::Host() {
  thread_ = std::thread([this] { Run(); });
}

Host::~Host() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
  }
  changed_.notify_one();
  thread_.join();
}

void Host::Post(std::function<void()> work) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push_back(std::move(work));
  }
  changed_.notify_one();
}

void Host::Initialize(NativeResourceManager *manager, std::string directory) {
  Post([this, manager, directory] {
    std::unique_ptr<NativeResourceManager, decltype(&OH_ResourceManager_ReleaseNativeResourceManager)> owner(
        manager, OH_ResourceManager_ReleaseNativeResourceManager);
    Extract(manager, directory);
  });
}

void Host::Extract(NativeResourceManager *manager, const std::string &directory) {
  if (!manager)
    throw std::runtime_error("Resource manager is unavailable");
  auto manifest = Raw(manager, "Resources/manifest.json");
  rapidjson::Document document;
  document.Parse(manifest.c_str());
  if (document.HasParseError() || !document.IsObject() || !document.HasMember("files") || !document["files"].IsArray())
    throw std::runtime_error("Invalid resource manifest");
  auto root = std::filesystem::path(directory) / ("resources-" + Digest(manifest).substr(0, 20));
  if (!std::filesystem::exists(root / ".complete")) {
    for (auto &entry : document["files"].GetArray()) {
      auto path = std::filesystem::path(Text(entry, "path"));
      if (path.empty() || path.is_absolute())
        throw std::runtime_error("Invalid bundled path");
      for (const auto &part : path)
        if (part == "..")
          throw std::runtime_error("Invalid bundled path");
      auto data = Raw(manager, "Resources/" + path.generic_string());
      if (Digest(data) != Text(entry, "sha256"))
        throw std::runtime_error("Resource digest mismatch: " + path.string());
      std::filesystem::create_directories((root / path).parent_path());
      std::ofstream file(root / path, std::ios::binary | std::ios::trunc);
      file.write(data.data(), data.size());
      file.close();
      if (!file)
        throw std::runtime_error("Cannot extract " + path.string());
    }
    std::ofstream complete(root / ".complete");
    complete << Digest(manifest);
    complete.close();
    if (!complete)
      throw std::runtime_error("Cannot finish resource extraction");
  }
  resources_ = root;
  ready_ = true;
  error_.clear();
}

void Host::Command(std::string json) {
  Post([this, json = std::move(json)] { Execute(json); });
}

void Host::Attach(OHNativeWindow *window, uint32_t width, uint32_t height) {
  OH_NativeWindow_NativeObjectReference(window);
  Post([this, window, width, height] {
    surface_.reset();
    if (window_)
      OH_NativeWindow_NativeObjectUnreference(window_);
    window_ = window;
    width_ = width;
    height_ = height;
    Resize();
    dirty_ = true;
  });
}

void Host::Detach() {
  auto done = std::make_shared<std::promise<void>>();
  auto wait = done->get_future();
  Post([this, done] {
    surface_.reset();
    if (window_)
      OH_NativeWindow_NativeObjectUnreference(window_);
    window_ = nullptr;
    done->set_value();
  });
  wait.get();
}

void Host::Stop() {
  surface_.reset();
  game_.reset();
  dirty_ = false;
  frames_ = 0;
  size_axis_ = 0;
  fps_ = 0;
  last_present_ = {};
}

void Host::Resize() {
  if (game_ && width_ && height_) {
    game_->Resize(width_, height_);
    game_->SetBottomControlInset(bottom_inset_);
  }
}

void Host::Execute(const std::string &json) {
  rapidjson::Document c;
  c.Parse(json.c_str());
  if (c.HasParseError() || !c.IsObject())
    throw std::runtime_error("Invalid native command");
  const auto type = Text(c, "type");
  if (type == "active") {
    active_ = Boolean(c, "value");
    last_present_ = {};
    if (game_) {
      game_->Window()->SendFocus(active_);
      game_->ResetClock();
    }
    dirty_ = true;
  } else if (type == "stop") {
    Stop();
    error_.clear();
  } else if (type == "open") {
    Stop();
    error_.clear();
    if (!ready_)
      throw std::runtime_error("Resources are still being prepared");
    game_ = std::make_unique<DesktopGameSession>(grassland::graphics::BACKEND_API_VULKAN);
    game_->EnableNativeSizeControls();
    Resize();
    dirty_ = true;
  } else if (type == "size" && game_) {
    size_value_ = int(std::clamp(Number(c, "value", 64), 2.0, 256.0));
    game_->SetGridDimension(int(Number(c, "axis")), size_value_);
    dirty_ = true;
  } else if (type == "bottomInset") {
    bottom_inset_ = float(std::clamp(Number(c, "value"), 0.0, 0.25));
    Resize();
    dirty_ = true;
  } else if (type == "dismissSize") {
    size_axis_ = 0;
  } else if (type == "file" && game_) {
    file_revision_ = int(Number(c, "request"));
    auto error = game_->CompleteFile(Text(c, "path"));
    if (!error.empty())
      throw std::runtime_error(error);
    dirty_ = true;
  } else if (type == "icons" && game_) {
    game_->SetIconOrientation(Number(c, "angle"));
    dirty_ = true;
  } else if (type == "input" && game_) {
    auto *game = game_.get();
    game->PrepareInput(delay_ < 0);
    auto *window = game->Window();
    const int kind = int(Number(c, "kind")), value = int(Number(c, "value"));
    double x = Number(c, "x") * width_, y = Number(c, "y") * height_;
    window->SendPointer(x, y);
    if (kind == 1) {
      window->CursorEnterEvent().InvokeCallbacks(true);
      window->SendMouseButton(value, 1);
    }
    if (kind == 2) {
      window->SendMouseButton(value, 0);
      window->CursorEnterEvent().InvokeCallbacks(false);
    }
    if (kind == 5) {
      window->SendFocus(value != 0);
      if (value)
        game->ResetClock();
    }
    if (kind == 6)
      window->MagnifyEvent().InvokeCallbacks(grassland::graphics::MagnifyGesture{
          std::clamp(Number(c, "value", 1), .1, 10.0), x, y, grassland::graphics::MagnifyPhase::kUpdate});
    dirty_ = true;
  }
}

void Host::Frame() {
  auto start = std::chrono::steady_clock::now();
  if (!surface_)
    surface_ = std::make_unique<Surface>(game_->Core(), window_);
  surface_->Resize(width_, height_);
  game_->Render();
  auto size = game_->TakeSizeControlRequest();
  if (size.x) {
    size_axis_ = size.x;
    size_value_ = size.y;
  }
  const bool presented = surface_->Present(game_->Image());
  const auto presented_at = std::chrono::steady_clock::now();
  if (presented) {
    const double interval =
        std::chrono::duration<double>(presented_at - (last_present_.time_since_epoch().count() ? last_present_ : start))
            .count();
    fps_ = interval > 0 ? 1 / interval : 0;
    last_present_ = presented_at;
  }
  ++frames_;
  dirty_ = false;
  // The game reports infinity, not a negative number, when idle.
  delay_ = game_->NextFrameDelay();
  if (!std::isfinite(delay_) || delay_ < 0)
    delay_ = -1;
  // A replaced swapchain needs another presentation even when the game is idle.
  if (!presented)
    delay_ = 1.0 / 60;
  deadline_ = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(std::max(1.0 / 60, delay_)));
}

void Host::Publish() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  auto text = [&](const char *key, const std::string &value) {
    w.Key(key);
    w.String(value.c_str());
  };
  auto number = [&](const char *key, double value) {
    w.Key(key);
    w.Double(std::isfinite(value) ? value : 0);
  };
  w.Key("ready");
  w.Bool(ready_);
  text("error", error_);
  text("resources", resources_.string());
  number("fps", fps_);
  number("frames", frames_);
  number("fileRequest", game_ ? game_->FileRequest() : 0);
  number("fileRevision", file_revision_);
  number("sizeAxis", size_axis_);
  number("sizeValue", size_value_);
  text("device", game_ ? game_->Core()->DeviceName() : "");
  w.EndObject();
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_ = buffer.GetString();
}

std::string Host::Status() {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void Host::Run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!quit_) {
    bool renderable = active_ && window_ && width_ && height_ && game_ && error_.empty();
    if (commands_.empty() &&
        !(renderable && (dirty_ || (delay_ >= 0 && std::chrono::steady_clock::now() >= deadline_)))) {
      if (renderable && delay_ >= 0)
        changed_.wait_until(lock, deadline_);
      else
        changed_.wait(lock);
      continue;
    }
    std::function<void()> command;
    if (!commands_.empty()) {
      command = std::move(commands_.front());
      commands_.pop_front();
    }
    lock.unlock();
    try {
      if (command)
        command();
      else
        Frame();
    } catch (const std::exception &error) {
      error_ = error.what();
      dirty_ = false;
    }
    Publish();
    lock.lock();
  }
  lock.unlock();
  Stop();
  if (window_)
    OH_NativeWindow_NativeObjectUnreference(window_);
}
}  // namespace longmarch::harmony
