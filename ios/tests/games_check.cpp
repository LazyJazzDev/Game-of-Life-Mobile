#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <iostream>
#include <thread>

#include "demo/gol/application/listener.h"
#include "demo/gol/cells_pattern.h"
#include "demo/gol/game_of_life_gui.h"
#include "demo/gol/gpu/tiled_life_storage.h"
#include "demo/gol/simulation_clock.h"
#include "demos/DemoSession.h"
#include "grassland/rhi/shader_cache.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static void Check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

static void Settle(DemoSession &session) {
  for (int i = 0; i < 24; ++i) {
    session.Render();
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

static std::vector<uint8_t> Pixels(DemoSession &session) {
  auto extent = session.Image()->GetDesc();
  std::vector<uint8_t> pixels(extent.width * extent.height * 4);
  grassland::rhi::ObjPtr<grassland::rhi::ICommandContext> context = session.Game()->Queue()->CreateCommandContext();
  grassland::rhi::ObjPtr<grassland::rhi::IDownloadToken> download = context->CmdDownloadImage(session.Image());
  grassland::rhi::ObjPtr<grassland::rhi::ISubmissionToken> submission = context->Submit();
  Check(submission && submission->WaitHost() && download && download->CopyTo(pixels.data(), pixels.size()),
        "Readback failed");
  return pixels;
}

static void CheckListenerMutation() {
  life_demo::Application app("listener check", 32, 32, grassland::rhi::BACKEND_API_DEFAULT, true);

  Check(app.InitializeHosted(), "Listener host initialization failed");

  struct Probe : life_demo::Listener {
    explicit Probe(life_demo::Application *app) : Listener(app) {
    }

    std::function<void()> action;
    int calls{};

    void OnCursorPos(double, double) override {
      ++calls;
      if (action)
        action();
    }
  } a(&app), b(&app);

  auto first = std::less<life_demo::Listener *>{}(&a, &b) ? &a : &b;
  auto second = first == &a ? &b : &a;
  first->action = [&] { app.UnregisterListener(second); };
  app.HostedInput()->SendPointer(1, 1);
  Check(first->calls == 1 && second->calls == 0, "Removed listener received a stale snapshot event");
  first->action = {};
  app.RegisterListener(second);
  app.HostedInput()->SendPointer(2, 2);
  Check(first->calls == 2 && second->calls == 1, "Re-registered listener missed input");
  app.UnregisterListener(&a);
  app.UnregisterListener(&b);
}

int main(int argc, char **argv) {
  try {
    Check(argc == 2, "usage: gol_games_check <resources>");
    grassland::rhi::ConfigureShaderCache({std::filesystem::path(argv[1]) / "shaders", true, true});
    CheckListenerMutation();
    GridView zoom_limits;
    zoom_limits.Zoom(1.0e-6f, glm::vec2{0}, 64.0f / 16384);
    Check(zoom_limits.zoom == 64.0f / 16384, "Zoom does not reach the whole fixed world");
    zoom_limits.Zoom(1.0e6f, glm::vec2{0}, 64.0f / 16384);
    Check(zoom_limits.zoom == 12.0f, "Maximum magnification changed");
    for (int level = 0; level < 3; ++level) {
      const float unit = 1000.0f / life_demo::GridDetail::kSpans[level];
      auto begin = life_demo::GridDetail::At(unit * 0.5f, 1000);
      auto middle = life_demo::GridDetail::At(unit * 0.75f, 1000);
      auto end = life_demo::GridDetail::At(unit, 1000);
      Check(begin.major == level && begin.transition == 0, "Checker transition began before half-screen span");
      Check(middle.major == level && std::abs(middle.transition - 0.5f) < 1e-6f,
            "Checker transition is not linear in projected span");
      Check(end.major == level + 1 && end.transition == 0, "Parent checker remains at one-screen span");
    }
    Check(!life_demo::GridDetail::Heat(0.25f) && life_demo::GridDetail::Heat(0.249f),
          "Heat threshold is not strictly over 16 cells per pixel");
    life_demo::GridRulerTiming ruler;
    ruler.Zoom(10);
    Check(ruler.Opacity(10) == 0 && ruler.Opacity(10.15) > 0.999f, "Ruler did not fade in");
    ruler.Zoom(11);
    Check(ruler.Opacity(13) > 0.999f && ruler.Opacity(13.175) > 0.49f && ruler.Opacity(13.175) < 0.51f &&
              ruler.Opacity(13.35) == 0,
          "Ruler did not hold two seconds after the last zoom and fade out");
    SimulationClock clock;
    int steps = 0;
    clock.Advance(.2, true, 0, [&] { ++steps; });
    Check(std::abs(clock.NextStepDelay(0) - .3) < 1e-6, "Slow Life wake deadline drifted");
    clock.Advance(.3, true, 0, [&] { ++steps; });
    Check(steps == 1 && clock.NextStepDelay(0) == .5, "Timed Life wake missed a generation");
    clock.Advance(10, true, SimulationClock::kLightning, [&] { ++steps; });
    const double lightning_delay = clock.NextStepDelay(SimulationClock::kLightning);
    Check(steps == 2 && lightning_delay > 0 && lightning_delay <= SimulationClock::kLightningPeriod,
          "Lightning mode must advance once per frame at 60 generations per second");
    {
      DemoSession game(argv[1]);
      game.Resize(640, 800);
      Settle(game);
      Check(std::isinf(game.Game()->NextFrameDelay()), "Settled game still requests continuous frames");
      auto before = Pixels(game);
      Check(*std::max_element(before.begin(), before.end()) > 200, "Shared UI produced no visible geometry");
      auto *window = game.Game()->Window();
      Check(window->GLFWWindow() == nullptr && window->GetFramebufferSize() == glm::ivec2(640, 800),
            "Hosted resize failed");
      window->CursorEnterEvent().InvokeCallbacks(true);
      game.Game()->SetIconOrientation(glm::half_pi<float>());
      Check(game.Game()->NextFrameDelay() == 0, "Orientation failed to wake paused Life");
      Settle(game);
      const auto rotated = Pixels(game);
      Check(rotated != before, "Orientation did not rotate button icons");
      for (int y = 128; y < 672; ++y)
        for (int x = 0; x < 640; ++x)
          for (int c = 0; c < 4; ++c)
            Check(rotated[(y * 640 + x) * 4 + c] == before[(y * 640 + x) * 4 + c],
                  "Icon rotation changed the grid or its background");
      Check(std::isinf(game.Game()->NextFrameDelay()), "Orientation animation did not return to idle");
      game.Game()->SetIconOrientation(0);
      Settle(game);
      window->SendPointer(324, 420);
      window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
      window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
      game.Game()->ResetClock();
      game.Render();
      // GPU edits are visible immediately; there is no CPU per-cell animation pass.
      Settle(game);
      Check(Pixels(game) != before, "Desktop cell interaction did not change the rendered grid");
      auto edited = Pixels(game);
      window->MagnifyEvent().InvokeCallbacks(
          grassland::rhi::MagnifyGesture{1.5, 320, 400, grassland::rhi::MagnifyPhase::kUpdate});
      Settle(game);
      Check(Pixels(game) != edited, "Desktop grid magnification did not update rendering");
      // One finger (the left button) pans the enlarged grid; returning the same
      // distance restores the view exactly, so neither end cell toggled.
      auto leave = [&] {
        window->CursorEnterEvent().InvokeCallbacks(false);
        Settle(game);
      };
      auto drag = [&](double x0, double y0, double x1, double y1) {
        window->CursorEnterEvent().InvokeCallbacks(true);
        window->SendPointer(x0, y0);
        window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        for (int i = 1; i <= 6; ++i)
          window->SendPointer(x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6);
        window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        leave();
      };
      leave();
      const auto zoomed = Pixels(game);
      drag(320, 400, 380, 440);
      Check(Pixels(game) != zoomed, "One-finger drag did not pan the enlarged grid");
      drag(380, 440, 320, 400);
      Check(Pixels(game) == zoomed, "One-finger pan toggled a cell or did not return exactly");
      window->CursorEnterEvent().InvokeCallbacks(true);
      window->SendPointer(320, 400);
      window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
      window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
      leave();
      Check(Pixels(game) != zoomed, "A tap on the enlarged grid no longer toggles its cell");
      window->CursorEnterEvent().InvokeCallbacks(true);
      window->SendKey(GLFW_KEY_O, GLFW_PRESS, GLFW_MOD_CONTROL);
      window->SendKey(GLFW_KEY_O, GLFW_RELEASE, GLFW_MOD_CONTROL);
      Settle(game);
      Check(game.Game()->FileRequest() == 1, "Original open action did not reach the native host");
      Check(game.Game()
                ->CompleteFile((std::filesystem::path(argv[1]) / "Patterns/gosper-glider-gun.cells").string())
                .empty(),
            "Desktop pattern import failed");
      window->SendKey(GLFW_KEY_S, GLFW_PRESS, GLFW_MOD_CONTROL);
      window->SendKey(GLFW_KEY_S, GLFW_RELEASE, GLFW_MOD_CONTROL);
      Settle(game);
      Check(game.Game()->FileRequest() == 2, "Original save action did not reach the native host");
      auto path =
          std::filesystem::temp_directory_path() /
          ("longmarch-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cells");
      Check(game.Game()->CompleteFile(path.string()).empty(), "Hosted grid export failed");
      std::ifstream saved_file(path, std::ios::binary);
      std::vector<uint32_t> saved;
      std::string saved_error;
      Check(life_demo::ReadTiledState(saved_file, saved, saved_error), "Sparse grid export is invalid");
      saved_file.close();
      std::filesystem::remove(path);
      size_t live = 0;
      for (auto word : saved)
        while (word) {
          word &= word - 1;
          ++live;
        }
      Check(live == 36, "Import/export lost pattern cells");
      // Malformed imports must preserve the current grid and release the file UI.
      for (const auto &invalid : {std::string("not a saved pattern"), std::string("OO\nO\n"), std::string()}) {
        std::ofstream bad(path, std::ios::binary);
        bad << invalid;
        bad.close();
        window->SendKey(GLFW_KEY_O, GLFW_PRESS, GLFW_MOD_CONTROL);
        window->SendKey(GLFW_KEY_O, GLFW_RELEASE, GLFW_MOD_CONTROL);
        Settle(game);
        Check(game.Game()->FileRequest() == 1, "Open did not recover after a previous import");
        Check(!game.Game()->CompleteFile(path.string()).empty(), "Invalid file was accepted");
        Check(game.Game()->FileRequest() == 0, "Failed import left the file UI blocked");
        Settle(game);
        window->SendKey(GLFW_KEY_S, GLFW_PRESS, GLFW_MOD_CONTROL);
        window->SendKey(GLFW_KEY_S, GLFW_RELEASE, GLFW_MOD_CONTROL);
        Settle(game);
        Check(game.Game()->CompleteFile(path.string()).empty(), "Export failed after invalid import");
        std::ifstream preserved_file(path, std::ios::binary);
        std::vector<uint32_t> preserved;
        Check(life_demo::ReadTiledState(preserved_file, preserved, saved_error) && preserved == saved,
              "Invalid import modified the current simulation");
      }
      std::filesystem::remove(path);
      game.Game()->EnableNativeSizeControls();
      Check(game.Game()->SizeControlBounds(1) == glm::vec4(0) && game.Game()->SizeControlBounds(2) == glm::vec4(0),
            "Removed size controls still have hit regions");
      Check(game.Game()->TakeSizeControlRequest().x == 0, "Removed size controls requested a popup");
      // Clear the world and fit all 16384 cells along each axis into the viewport.
      auto tap = [&](double x, double y) {
        window->SendPointer(x, y);
        window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
        window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
        leave();
      };
      tap(64, 96);
      window->MagnifyEvent().InvokeCallbacks(
          grassland::rhi::MagnifyGesture{1.0e-6, 320, 400, grassland::rhi::MagnifyPhase::kUpdate});
      Settle(game);
      auto overview = Pixels(game);
      auto same_grid = [](const auto &a, const auto &b) {
        for (int y = 150; y < 650; ++y)
          for (int x = 8; x < 632; ++x)
            for (int c = 0; c < 4; ++c)
              if (a[(y * 640 + x) * 4 + c] != b[(y * 640 + x) * 4 + c])
                return false;
        return true;
      };
      auto centre = (400 * 640 + 320) * 4;
      Check((overview[centre] <= 2 || std::abs(int(overview[centre]) - 38) <= 2),
            "Empty heat view did not use a checker interval");
      window->MagnifyEvent().InvokeCallbacks(
          grassland::rhi::MagnifyGesture{0.5, 320, 400, grassland::rhi::MagnifyPhase::kUpdate});
      Settle(game);
      Check(same_grid(Pixels(game), overview), "Overview zoom went below the full-world fit limit");
      tap(576, 96);
      auto random_overview = Pixels(game);
      Check(std::abs(int(random_overview[centre]) - int(overview[centre])) <= 2,
            "Random fill incorrectly appeared as simulated change heat");
      window->MagnifyEvent().InvokeCallbacks(
          grassland::rhi::MagnifyGesture{1.0e6, 320, 400, grassland::rhi::MagnifyPhase::kUpdate});
      Settle(game);
      auto maximum = Pixels(game);
      window->MagnifyEvent().InvokeCallbacks(
          grassland::rhi::MagnifyGesture{2, 320, 400, grassland::rhi::MagnifyPhase::kUpdate});
      Settle(game);
      Check(same_grid(Pixels(game), maximum), "Maximum zoom did not retain its clamp");
      // File feedback can last over a second; all animations must eventually sleep.
      for (int i = 0; i < 150 && std::isfinite(game.Game()->NextFrameDelay()); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        game.Render();
      }
      Check(std::isinf(game.Game()->NextFrameDelay()), "Game did not sleep after interaction");
      window->SendFocus(false);
      Check(!window->IsFocused() && !window->IsMouseButtonDown(0), "Hosted focus did not release inputs");
      window->SendFocus(true);
      game.Resize(800, 480);
      Settle(game);
      Check(game.Image()->GetDesc().width == 800 && game.Image()->GetDesc().height == 480,
            "Shared UI orientation resize failed");
      std::cout << "PASS gol shared desktop renderer, input, animation, focus and orientation\n";
    }
    {
      // Foldable surfaces change size without changing their pixel density.
      life_demo::GameOfLife life("fold layout", 360, 780, 64, 64, grassland::rhi::BACKEND_API_METAL, true);
      life.InitializeHosted();
      life.EnableNativeSizeControls();
      for (float density : {1.0f, 2.0f, 3.0f}) {
        life.SetControlPixelSize(40.0f * density);
        for (auto size : {glm::ivec2{360, 780}, glm::ivec2{660, 780}, glm::ivec2{1000, 780}, glm::ivec2{780, 1000}}) {
          auto pixels = glm::ivec2(glm::vec2(size) * density);
          life.HostedInput()->UpdateHostedSize(pixels, pixels);
          life.RenderHostedFrame();
          const auto bounds = life.SizeControlBounds(1);
          const auto extent = glm::vec2(bounds.z - bounds.x, bounds.w - bounds.y) * glm::vec2(pixels);
          Check(std::abs(std::min(extent.x, extent.y) / density - 17.0f) < 0.01f,
                "Folding changed density-independent control thickness");
          Check(bounds.x >= 0 && bounds.y >= 0 && bounds.z <= 1 && bounds.w <= 1,
                "Fold layout placed controls outside the surface");
          const auto centre = glm::vec2(bounds.x + bounds.z, bounds.y + bounds.w) * 0.5f * glm::vec2(pixels);
          auto *window = life.HostedInput();
          window->SendPointer(centre.x, centre.y);
          window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS);
          window->SendMouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE);
          Check(life.TakeSizeControlRequest() == glm::ivec2(1, 64),
                "Fold/rotation broke control hit testing or grid state");
        }
      }
      life.CloseHosted();
      std::cout << "PASS fold layout: stable vp sizing and hit testing across extents and densities\n";
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
