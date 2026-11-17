#pragma once
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cmath>

#include "demo/gol/game_of_life_gui.h"

namespace mobile_agent {
inline std::string Execute(life_demo::GameOfLife &game, const std::string &input) {
  rapidjson::Document d;
  d.Parse(input.c_str());
  if (input.size() > 256 * 1024 || d.HasParseError() || !d.IsObject() || !d.HasMember("action") ||
      !d["action"].IsString())
    return R"({"ok":false,"error":"invalid_arguments"})";
  auto number = [&](const char *key, double &v) {
    if (!d.HasMember(key) || !d[key].IsNumber())
      return false;
    v = d[key].GetDouble();
    return std::isfinite(v);
  };
  std::string action = d["action"].GetString();
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  bool ok = false;
  if (action == "state") {
    const auto s = game.GetAgentState();
    w.Key("playing");
    w.Bool(s.playing);
    w.Key("speed_level");
    w.Int(s.speed);
    w.Key("periodic");
    w.Bool(s.periodic);
    w.Key("zoom");
    w.Double(s.zoom);
    w.Key("world_size");
    w.Int(16384);
    w.Key("coordinate_system");
    w.String("center origin; x right, y down; [-8192,8191]");
    ok = true;
  } else if (action == "edit" && d.HasMember("cells") && d["cells"].IsArray() && d["cells"].Size() <= 4096) {
    std::vector<glm::ivec3> cells;
    bool valid = true;
    for (const auto &c : d["cells"].GetArray()) {
      if (!c.IsArray() || c.Size() != 3 || !c[0].IsInt() || !c[1].IsInt() || !c[2].IsInt()) {
        valid = false;
        break;
      }
      cells.push_back({c[0].GetInt(), c[1].GetInt(), c[2].GetInt()});
    }
    ok = valid && game.AgentEdit(cells);
  } else if (action == "read") {
    double x, y, width, height;
    if (number("x", x) && number("y", y) && number("width", width) && number("height", height) && x == std::floor(x) &&
        y == std::floor(y) && width == std::floor(width) && height == std::floor(height) && std::abs(x) <= 8192 &&
        std::abs(y) <= 8192 && width >= 1 && height >= 1 && width <= 64 && height <= 64) {
      std::vector<glm::ivec2> cells;
      ok = game.AgentRead(int(x), int(y), int(width), int(height), cells);
      if (ok) {
        w.Key("alive");
        w.StartArray();
        for (auto c : cells) {
          w.StartArray();
          w.Int(c.x);
          w.Int(c.y);
          w.EndArray();
        }
        w.EndArray();
      }
    }
  } else {
    double value = 0, x = 0, y = 0;
    bool valid = action == "reset" || action == "randomize" || number("value", value);
    if (action == "view")
      valid = valid && number("x", x) && number("y", y);
    if (valid)
      ok = game.AgentControl(action, value, x, y);
    if (ok && (action == "reset" || action == "randomize")) {
      w.Key("status");
      w.String("awaiting_user_confirmation; no change applied yet");
    }
  }
  w.Key("ok");
  w.Bool(ok);
  if (!ok) {
    w.Key("error");
    w.String("invalid_arguments_or_canvas_busy");
  }
  w.EndObject();
  return buffer.GetString();
}
}  // namespace mobile_agent
