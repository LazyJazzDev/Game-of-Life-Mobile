#include <rapidjson/document.h>

#include <iostream>

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
    auto result = session.Game()->AgentCommand(json);
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
  run(R"({"action":"view","value":2,"x":1001,"y":1001})");
  run(R"({"action":"reset"})");
  if (session.Game()->ConfirmationRequest() != 1)
    return 5;
  session.Game()->CompleteConfirmation(false);
  if (run(R"({"action":"read","x":1000,"y":1000,"width":3,"height":3})") != result)
    return 6;
  std::cout
      << "PASS Agent controls, invalid parameters, idempotent edits, exact blinker step/readback and cancelled reset\n";
}
