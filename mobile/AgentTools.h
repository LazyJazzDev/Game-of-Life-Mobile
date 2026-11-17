#pragma once
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cmath>

#include "AgentTemplates.h"
#include "MetacellGuide.h"
#include "demo/gol/game_of_life_gui.h"

namespace mobile_agent {
inline std::string Execute(life_demo::GameOfLife &game,
                           const std::string &input,
                           TemplateStore *templates = nullptr,
                           bool allow_preset_guides = true) {
  rapidjson::Document d;
  if (input.size() > 2 * 1024 * 1024)
    return R"({"ok":false,"error":"request_too_large"})";
  d.Parse(input.c_str());
  if (d.HasParseError() || !d.IsObject() || !d.HasMember("action") || !d["action"].IsString())
    return R"({"ok":false,"error":"invalid_arguments"})";
  auto number = [&](const char *key, double &v) {
    if (!d.HasMember(key) || !d[key].IsNumber())
      return false;
    v = d[key].GetDouble();
    return std::isfinite(v);
  };
  std::string action = d["action"].GetString();
  if (action == "metacell_guide")
    return allow_preset_guides ? kMetacellGuide : R"({"ok":false,"error":"unsupported_action"})";
  if (action.rfind("template_", 0) == 0) {
    if (!templates)
      return R"({"ok":false,"error":"template_store_unavailable"})";
    auto error = [] { return std::string(R"({"ok":false,"error":"invalid_template_or_limits_or_existing_name"})"); };
    if (action == "template_list") {
      std::string out = "{\"ok\":true,\"templates\":[";
      bool first = true;
      for (const auto &name : templates->List()) {
        Template p;
        if (!templates->Load(name, p))
          continue;
        if (!first)
          out += ",";
        first = false;
        out += TemplateSummary(name, p);
      }
      return out + "]}";
    }
    if (!d.HasMember("name") || !d["name"].IsString() || !TemplateName(d["name"].GetString()))
      return error();
    const std::string name = d["name"].GetString();
    Template p;
    if (action == "template_save") {
      if (!d.HasMember("rle") || !d["rle"].IsString() || !DecodeRLE(d["rle"].GetString(), p) ||
          !templates->Save(name, p))
        return error();
    } else if (action == "template_compose") {
      if (!d.HasMember("width") || !d["width"].IsInt() || !d.HasMember("height") || !d["height"].IsInt() ||
          !d.HasMember("parts") || !templates->Compose(d["parts"], d["width"].GetInt(), d["height"].GetInt(), p) ||
          !templates->Save(name, p))
        return error();
    } else {
      if (!templates->Load(name, p))
        return error();
      if (action == "template_place") {
        if (!d.HasMember("x") || !d["x"].IsInt() || !d.HasMember("y") || !d["y"].IsInt())
          return error();
        int x = d["x"].GetInt(), y = d["y"].GetInt();
        if (x < -8192 || y < -8192 || x > 8192 - p.width || y > 8192 - p.height)
          return error();
        std::string mode = "empty_only";
        if (d.HasMember("mode")) {
          if (!d["mode"].IsString())
            return error();
          mode = d["mode"].GetString();
        }
        std::vector<glm::ivec2> cells;
        cells.reserve(p.cells.size());
        for (auto c : p.cells)
          cells.push_back({x + c.first, y + c.second});
        if (!game.AgentStamp({{x, y, p.width, p.height}}, cells, mode))
          return R"({"ok":false,"error":"canvas_busy_or_nonempty_or_invalid_mode","applied":false})";
      } else if (action != "template_info")
        return error();
    }
    return TemplateSummary(name, p);
  }
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
  } else if (action == "summary" || action == "fit") {
    if (d.HasMember("x") && d["x"].IsInt() && d.HasMember("y") && d["y"].IsInt() && d.HasMember("width") &&
        d["width"].IsInt() && d.HasMember("height") && d["height"].IsInt()) {
      uint64_t population = 0, hash = 0;
      if (action == "fit")
        ok = game.AgentFit(d["x"].GetInt(), d["y"].GetInt(), d["width"].GetInt(), d["height"].GetInt());
      else
        ok = game.AgentRegionSummary(d["x"].GetInt(), d["y"].GetInt(), d["width"].GetInt(), d["height"].GetInt(),
                                     population, hash);
      if (ok && action == "summary") {
        w.Key("population");
        w.Uint64(population);
        w.Key("hash");
        w.String(std::to_string(hash).c_str());
      }
    }
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
