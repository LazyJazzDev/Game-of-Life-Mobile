#include <cassert>
#include <chrono>
#include <iostream>

#include "../../mobile/AgentTemplates.h"

// Use explicit checks: Release builds define NDEBUG.
#define CHECK(x)                                               \
  do {                                                         \
    if (!(x)) {                                                \
      std::cerr << "failed line " << __LINE__ << ": " #x "\n"; \
      return 1;                                                \
    }                                                          \
  } while (false)

int main() {
  using namespace mobile_agent;
  Template p, q;
  CHECK(DecodeRLE("#N example\nx = 3, y = 2, rule = B3/S23\nobo$bo!", p));
  CHECK(p.cells.size() == 3);
  CHECK(DecodeRLE(EncodeRLE(p), q) && p.cells == q.cells && p.width == q.width);
  for (const auto &bad : {"x=1,y=1\n2o!", "x=1,y=1\no", "x=1,y=1\no!o", "x=1,y=1\n0o!", "x=1,y=1,rule=B36/S23\no!",
                          "x=1,y=1\n999999999999o!", "x=1,y=1\n$o!"})
    CHECK(!DecodeRLE(bad, q));
  auto directory = std::filesystem::temp_directory_path() /
                   ("life-agent-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemplateStore store;
  store.SetDirectory(directory);
  CHECK(!store.Save("../escape", p));
  CHECK(store.Save("seed", p));
  CHECK(store.Save("seed", p));
  q = p;
  q.cells.clear();
  CHECK(!store.Save("seed", q));
  TemplateStore reopened;
  reopened.SetDirectory(directory);
  CHECK(reopened.Load("seed", q) && q.cells == p.cells);
  rapidjson::Document parts;
  parts.Parse(R"([{"name":"seed","x":1,"y":2,"rotation":1,"columns":2,"dx":4}])");
  CHECK(reopened.Compose(parts, 7, 5, q));
  CHECK((q.cells == std::vector<std::pair<int, int>>{{2, 2}, {6, 2}, {1, 3}, {5, 3}, {2, 4}, {6, 4}}));
  CHECK(!reopened.Compose(parts, 6, 5, q));
  parts.Parse(R"([{"name":"seed","x":0,"y":0,"rotation":3,"mirror":1}])");
  CHECK(reopened.Compose(parts, 2, 3, q));
  CHECK((q.cells == std::vector<std::pair<int, int>>{{0, 0}, {1, 1}, {0, 2}}));
  parts.Parse(R"([{"name":"seed","x":0,"y":0,"columns":1024,"rows":1024}])");
  CHECK(!reopened.Compose(parts, 3, 2, q));
  parts.Parse(R"([{"name":"seed","x":2147483647,"y":0}])");
  CHECK(!reopened.Compose(parts, 3, 2, q));
  CHECK(reopened.List() == std::vector<std::string>{"seed"});
  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  CHECK(!ec);
  std::cout << "PASS bounded RLE, durable templates, name safety, rotation/mirror, repetition and capacity checks\n";
}
