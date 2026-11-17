#pragma once
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace mobile_agent {
// Templates use local top-left coordinates. No model-supplied filesystem paths.
struct Template {
  int width = 0, height = 0;
  std::vector<std::pair<int, int>> cells;
};

inline constexpr size_t kTemplateCells = 4 * 1024 * 1024;
inline constexpr size_t kTemplateBytes = 16 * 1024 * 1024;

inline bool TemplateName(const std::string &name) {
  return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

inline void Canonicalize(Template &p) {
  std::sort(p.cells.begin(), p.cells.end(),
            [](auto a, auto b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
  p.cells.erase(std::unique(p.cells.begin(), p.cells.end()), p.cells.end());
}

inline bool DecodeRLE(const std::string &text, Template &p) {
  if (text.size() > kTemplateBytes)
    return false;
  p = {};
  std::istringstream stream(text);
  std::string line, body;
  bool header = false;
  while (std::getline(stream, line)) {
    auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#')
      continue;
    if (!header) {
      // Standard two-state B3/S23 RLE only, with an explicit size header.
      std::string compact;
      for (unsigned char c : line)
        if (!std::isspace(c))
          compact += char(c);
      auto split = compact.find(",y=");
      if (compact.rfind("x=", 0) != 0 || split == std::string::npos)
        return false;
      auto rule = compact.find(",rule=", split + 3);
      auto integer = [](std::string s, int &v) {
        v = 0;
        if (s.empty())
          return false;
        for (auto c : s) {
          if (c < '0' || c > '9' || v > 16384)
            return false;
          v = v * 10 + c - '0';
        }
        return v >= 1 && v <= 16384;
      };
      if (!integer(compact.substr(2, split - 2), p.width) ||
          !integer(compact.substr(split + 3, rule == std::string::npos ? rule : rule - split - 3), p.height))
        return false;
      if (rule != std::string::npos && compact.substr(rule + 6) != "B3/S23" && compact.substr(rule + 6) != "b3/s23")
        return false;
      header = true;
    } else
      body += line;
  }
  if (!header)
    return false;
  int x = 0, y = 0, run = 0;
  bool digits = false, finished = false;
  for (unsigned char c : body) {
    if (std::isspace(c))
      continue;
    if (finished)
      return false;
    if (c >= '0' && c <= '9') {
      if (run > 16384)
        return false;
      run = run * 10 + c - '0';
      digits = true;
      continue;
    }
    const int n = digits ? run : 1;
    if (n < 1 || n > 16384)
      return false;
    if (c == '!') {
      if (digits)
        return false;
      finished = true;
    } else if (c == '$') {
      if (y + n > p.height)
        return false;
      y += n;
      x = 0;
    } else if (c == 'b' || c == 'o') {
      if (y >= p.height || x > p.width - n)
        return false;
      if (c == 'o') {
        if (p.cells.size() + n > kTemplateCells)
          return false;
        for (int i = 0; i < n; ++i)
          p.cells.emplace_back(x + i, y);
      }
      x += n;
    } else
      return false;
    digits = false;
    run = 0;
  }
  return finished;
}

inline std::string EncodeRLE(const Template &p) {
  std::ostringstream out;
  out << "x = " << p.width << ", y = " << p.height << ", rule = B3/S23\n";
  size_t column = 0;
  auto emit = [&](int n, char c) {
    if (n < 1)
      return;
    const auto token = (n > 1 ? std::to_string(n) : std::string()) + c;
    if (column + token.size() > 70) {
      out << '\n';
      column = 0;
    }
    out << token;
    column += token.size();
  };
  int x = 0, y = 0;
  for (size_t i = 0; i < p.cells.size();) {
    const auto [cx, cy] = p.cells[i];
    if (cy > y) {
      emit(cy - y, '$');
      y = cy;
      x = 0;
    }
    emit(cx - x, 'b');
    int length = 1;
    while (i + length < p.cells.size() && p.cells[i + length] == std::make_pair(cx + length, cy))
      ++length;
    emit(length, 'o');
    x = cx + length;
    i += length;
  }
  out << "!\n";
  return out.str();
}

inline std::string TemplateSummary(const std::string &name, const Template &p) {
  uint64_t hash = 14695981039346656037ull;
  for (auto c : p.cells) {
    hash ^= uint64_t(c.second) * p.width + c.first;
    hash *= 1099511628211ull;
  }
  rapidjson::StringBuffer b;
  rapidjson::Writer<rapidjson::StringBuffer> w(b);
  w.StartObject();
  w.Key("ok");
  w.Bool(true);
  w.Key("name");
  w.String(name.c_str());
  w.Key("width");
  w.Int(p.width);
  w.Key("height");
  w.Int(p.height);
  w.Key("population");
  w.Uint64(p.cells.size());
  w.Key("hash");
  w.String(std::to_string(hash).c_str());
  w.EndObject();
  return b.GetString();
}

class TemplateStore {
 public:
  void SetDirectory(const std::filesystem::path &directory) {
    directory_ = directory;
  }

  void SetBuiltinDirectory(const std::filesystem::path &directory) {
    builtin_directory_ = directory;
  }

  bool Load(const std::string &name, Template &p) const {
    if (!TemplateName(name))
      return false;
    return LoadFrom(builtin_directory_, name, p) || LoadFrom(directory_, name, p);
  }

  static bool LoadFrom(const std::filesystem::path &directory, const std::string &name, Template &p) {
    if (!TemplateName(name) || directory.empty())
      return false;
    std::error_code ec;
    const auto path = directory / (name + ".rle");
    if (std::filesystem::file_size(path, ec) > kTemplateBytes || ec)
      return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
      return false;
    std::string text((std::istreambuf_iterator<char>(input)), {});
    return DecodeRLE(text, p);
  }

  bool Save(const std::string &name, Template p) {
    if (!TemplateName(name) || directory_.empty())
      return false;
    if (p.width < 1 || p.height < 1 || p.width > 16384 || p.height > 16384 || p.cells.size() > kTemplateCells)
      return false;
    for (auto c : p.cells)
      if (c.first < 0 || c.second < 0 || c.first >= p.width || c.second >= p.height)
        return false;
    Canonicalize(p);
    auto text = EncodeRLE(p);
    if (text.size() > kTemplateBytes)
      return false;
    Template builtin;
    if (LoadFrom(builtin_directory_, name, builtin))
      return builtin.width == p.width && builtin.height == p.height && builtin.cells == p.cells;
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    if (ec)
      return false;
    const auto target = directory_ / (name + ".rle");
    // Existing identical templates are idempotent; never silently overwrite a different definition.
    if (std::filesystem::exists(target, ec)) {
      Template old;
      return Load(name, old) && old.width == p.width && old.height == p.height && old.cells == p.cells;
    }
    if (ec)
      return false;
    size_t bytes = text.size(), count = 0;
    for (std::filesystem::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->path().extension() == ".rle") {
        ++count;
        bytes += it->file_size(ec);
      }
    }
    if (ec || count >= 128 || bytes > 64 * 1024 * 1024)
      return false;
    const auto temp = directory_ / (name + ".tmp");
    {
      std::ofstream file(temp, std::ios::binary | std::ios::trunc);
      file << text;
      file.close();
      if (!file) {
        std::filesystem::remove(temp, ec);
        return false;
      }
    }
    std::filesystem::rename(temp, target, ec);
    return !ec;
  }

  std::vector<std::string> List() const {
    std::vector<std::string> result;
    for (const auto &directory : {builtin_directory_, directory_}) {
      if (directory.empty())
        continue;
      std::error_code ec;
      for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".rle" && result.size() < 130)
          result.push_back(it->path().stem().string());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  }

  // Compose using top-left anchors after rotation/mirror, then repeat on a regular grid.
  bool Compose(const rapidjson::Value &parts, int width, int height, Template &result) const {
    if (width < 1 || height < 1 || width > 16384 || height > 16384 || !parts.IsArray() || parts.Empty() ||
        parts.Size() > 256)
      return false;
    result = {width, height, {}};
    size_t instances = 0;
    for (const auto &part : parts.GetArray()) {
      if (!part.IsObject() || !part.HasMember("name") || !part["name"].IsString())
        return false;
      Template p;
      if (!Load(part["name"].GetString(), p))
        return false;
      auto get = [&](const char *key, int fallback, int &v) {
        v = fallback;
        if (!part.HasMember(key))
          return true;
        if (!part[key].IsInt())
          return false;
        v = part[key].GetInt();
        return true;
      };
      int x, y, rotation, mirror, columns, rows, dx, dy;
      if (!get("x", 0, x) || !get("y", 0, y) || !get("rotation", 0, rotation) || !get("mirror", 0, mirror) ||
          !get("columns", 1, columns) || !get("rows", 1, rows) || !get("dx", 0, dx) || !get("dy", 0, dy))
        return false;
      if (rotation < 0 || rotation > 3 || mirror < 0 || mirror > 1 || columns < 1 || rows < 1 || columns > 1024 ||
          rows > 1024 || dx < 0 || dy < 0 || dx > 16384 || dy > 16384 || x < 0 || y < 0)
        return false;
      instances += size_t(columns) * rows;
      if (instances > 1024 || result.cells.size() + p.cells.size() * size_t(columns) * rows > kTemplateCells)
        return false;
      int tw = rotation % 2 ? p.height : p.width, th = rotation % 2 ? p.width : p.height;
      if (int64_t(x) + int64_t(columns - 1) * dx + tw > width || int64_t(y) + int64_t(rows - 1) * dy + th > height)
        return false;
      for (auto c : p.cells) {
        int px = mirror ? p.width - 1 - c.first : c.first, py = c.second;
        if (rotation == 1) {
          const int temp = px;
          px = p.height - 1 - py;
          py = temp;
        } else if (rotation == 2) {
          px = p.width - 1 - px;
          py = p.height - 1 - py;
        } else if (rotation == 3) {
          const int temp = px;
          px = py;
          py = p.width - 1 - temp;
        }
        for (int row = 0; row < rows; ++row)
          for (int column = 0; column < columns; ++column)
            result.cells.emplace_back(x + column * dx + px, y + row * dy + py);
      }
    }
    Canonicalize(result);
    return true;
  }

 private:
  std::filesystem::path directory_, builtin_directory_;
};
}  // namespace mobile_agent
