#include <rapidjson/document.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "demos/DemoSession.h"
#include "grassland/rhi/shader_cache.h"

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  grassland::rhi::ConfigureShaderCache({std::filesystem::path(argv[1]) / "shaders", true, true});
  DemoSession session(argv[1]);
  session.Resize(640, 800);
  session.Render();
  auto run = [&](const std::string &json, bool expected = true) {
    auto result = session.AgentCommand(json);
    rapidjson::Document d;
    d.Parse(result.c_str());
    if (d.HasParseError() || !d.HasMember("ok") || d["ok"].GetBool() != expected) {
      std::cerr << "Agent request failed: " << json << " -> " << result << '\n';
      std::exit(1);
    }
    return result;
  };
  run(R"({"action":"play","value":0})");
  run(R"({"action":"speed","value":3})");
  run(R"({"action":"speed","value":9})", false);
  run(R"({"action":"read","x":0,"y":0,"width":65,"height":1})", false);
  run(R"({"action":"edit","cells":[[8192,0,1]]})", false);
  run(R"({"action":"edit","cells":[[1000,1000,0],[1001,1000,0],[1002,1000,0],[1000,1001,1],[1001,1001,1],[1002,1001,1],[1000,1002,0],[1001,1002,0],[1002,1002,0]]})");
  // Explicit set is idempotent, unlike Toggle.
  run(R"({"action":"edit","cells":[[1001,1001,1]]})");
  run(R"({"action":"step","value":1})");
  auto result = run(R"({"action":"read","x":1000,"y":1000,"width":3,"height":3})");
  rapidjson::Document d;
  d.Parse(result.c_str());
  if (d["alive"].Size() != 3)
    return 3;
  for (int i = 0; i < 3; ++i)
    if (d["alive"][i][0].GetInt() != 1001 || d["alive"][i][1].GetInt() != 1000 + i)
      return 4;
  const auto preview_dir =
      std::filesystem::temp_directory_path() /
      ("life-preview-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(preview_dir);
  const auto before_file = preview_dir / "before.gol", during_file = preview_dir / "during.gol";
  if (!session.Game()->SaveSession(before_file.string()).empty())
    return 46;
  // GCD serial queues can migrate threads after initialization.
  std::thread([&] { run(R"({"action":"preview","rle":"x=3,y=1,rule=B3/S23\n3o!"})"); }).join();
  session.Render();
  run(R"({"action":"preview","rle":"x=3,y=3,rule=B3/S23\nbo$2bo$3o!"})");
  session.Render();
  if (!session.Game()->SaveSession(during_file.string()).empty())
    return 47;
  auto bytes = [](const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
  };
  if (bytes(before_file) != bytes(during_file))
    return 48;
  run(R"({"action":"preview_end"})");
  session.Render();
  if (run(R"({"action":"read","x":1000,"y":1000,"width":3,"height":3})") != result)
    return 49;
  session.Game()->SetAgentPatternDirectory(preview_dir / "Patterns");
  run(R"({"action":"canvas_save","name":"测试布局"})");
  const auto saved_path = preview_dir / "Patterns" / "测试布局.cells";
  const auto saved_bytes = bytes(saved_path);
  if (saved_bytes.empty())
    return 50;
  run(R"({"action":"edit","cells":[[1000,1000,1]]})");
  const auto collision = run(R"({"action":"canvas_save","name":"测试布局"})", false);
  if (collision.find("name_exists") == std::string::npos || bytes(saved_path) != saved_bytes)
    return 51;
  run(R"({"action":"canvas_save","name":"../outside"})", false);
  run(R"({"action":"canvas_save","name":"测试布局二"})");
  if (!session.Game()->RestoreSession(saved_path.string()).empty() ||
      run(R"({"action":"read","x":1000,"y":1000,"width":3,"height":3})") != result)
    return 52;
  std::cout << "PASS user-library save, Unicode names, collision protection, path rejection and reload\n";
  std::filesystem::remove_all(preview_dir);
  std::cout << "PASS native research buffer isolation, main-canvas save during preview, and restoration\n";
  run(R"({"action":"view_stop","value":0})");
  auto zoom = [&] {
    rapidjson::Document state;
    state.Parse(run(R"({"action":"state"})").c_str());
    return state["zoom"].GetDouble();
  };
  const double start_zoom = zoom();
  run(R"({"action":"view","value":2,"x":1001,"y":1001})");
  if (zoom() != start_zoom)
    return 43;  // Scheduling a camera move must not jump immediately.
  bool intermediate = false;
  const auto begin = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(450)) {
    session.Render();
    const auto current = zoom();
    intermediate |= current != start_zoom && std::abs(current - 2.0) > 1.0e-5;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!intermediate || std::abs(zoom() - 2.0) > 1.0e-5)
    return 44;
  run(R"({"action":"view","value":4,"x":0,"y":0})");
  run(R"({"action":"view_stop","value":0})");
  session.Render();
  if (std::abs(zoom() - 2.0) > 1.0e-5)
    return 45;
  std::cout << "PASS smooth camera transition and cancellation without a zoom jump\n";
  run(R"({"action":"reset"})");
  if (session.Game()->ConfirmationRequest() != 1)
    return 5;
  session.Game()->CompleteConfirmation(false);
  if (run(R"({"action":"read","x":1000,"y":1000,"width":3,"height":3})") != result)
    return 6;
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("life-agent-gpu-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  session.Game()->RestoreSession((directory / "LastCanvas.gol").string());
  run(R"({"action":"template_save","name":"block","rle":"x=2,y=2,rule=B3/S23\n2o$2o!"})");
  run(R"({"action":"template_compose","name":"block_grid","width":160,"height":160,"parts":[{"name":"block","x":0,"y":0,"columns":32,"rows":32,"dx":5,"dy":5}]})");
  // Add a second grid to exceed the legacy 4096-cell tool limit.
  run(R"({"action":"template_compose","name":"large","width":320,"height":160,"parts":[{"name":"block_grid","x":0,"y":0},{"name":"block_grid","x":160,"y":0}]})");
  const auto info = run(R"({"action":"template_info","name":"large"})");
  run(R"({"action":"template_place","name":"large","x":2000,"y":2000,"mode":"empty_only"})");
  const auto summary = run(R"({"action":"summary","x":2000,"y":2000,"width":320,"height":160})");
  rapidjson::Document expected, actual;
  expected.Parse(info.c_str());
  actual.Parse(summary.c_str());
  if (actual["population"].GetUint64() != 8192 ||
      std::string(actual["hash"].GetString()) != expected["hash"].GetString())
    return 7;
  run(R"({"action":"template_place","name":"large","x":2001,"y":2000,"mode":"empty_only"})", false);
  if (run(R"({"action":"summary","x":2000,"y":2000,"width":320,"height":160})") != summary)
    return 8;
  run(R"({"action":"step","value":4})");
  if (run(R"({"action":"summary","x":2000,"y":2000,"width":320,"height":160})") != summary)
    return 9;
  run(R"({"action":"template_place","name":"large","x":8100,"y":2000,"mode":"merge"})", false);
  if (session.Game()->AgentCommand(R"({"action":"metacell_guide"})").find("unsupported_action") == std::string::npos)
    return 42;
  session.Game()->SetAgentBuiltinDirectory(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                                           "mobile/AgentTemplates");
  std::string parts = "[";
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x) {
      if (x || y)
        parts += ",";
      parts += "{\"name\":\"otca_b3s23_" + std::string(y == 2 && x >= 1 && x <= 3 ? "on" : "off") +
               "\",\"x\":" + std::to_string(x * 2048) + ",\"y\":" + std::to_string(y * 2048) + "}";
    }
  parts += "]";
  run("{\"action\":\"template_compose\",\"name\":\"macro_blinker\",\"width\":10250,\"height\":10250,\"parts\":" +
      parts + "}");
  run(R"({"action":"template_place","name":"macro_blinker","x":-5125,"y":-5125,"mode":"replace"})");
  auto macro = run(R"({"action":"summary","x":-5125,"y":-5125,"width":10250,"height":10250})");
  actual.Parse(macro.c_str());
  if (actual["population"].GetUint64() != 724793 || std::string(actual["hash"].GetString()) != "16266944192161686577")
    return 10;
  run(R"({"action":"fit","x":-5125,"y":-5125,"width":10250,"height":10250})");
  run(R"({"action":"step","value":64})");
  macro = run(R"({"action":"summary","x":-5125,"y":-5125,"width":10250,"height":10250})");
  actual.Parse(macro.c_str());
  if (actual["population"].GetUint64() != 678123 || std::string(actual["hash"].GetString()) != "4885622696760063268")
    return 11;
  std::cout << "PASS real 5x5 OTCA construction and 64 GPU generations match independent Golly reference\n";
  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  std::cout
      << "PASS durable compound templates, 8192-cell single batch, GPU fingerprint, atomic conflict and evolution\n";
  std::cout
      << "PASS Agent controls, invalid parameters, idempotent edits, exact blinker step/readback and cancelled reset\n";
}
