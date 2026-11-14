#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

#include "gpu/tiled_life.h"

using namespace grassland::rhi;

std::vector<uint32_t> Pack(const std::vector<uint8_t> &cells, uint32_t side) {
  const auto tiles = side / 256;
  std::vector<uint32_t> packed(cells.size() / 32);
  for (uint32_t y = 0; y < side; ++y)
    for (uint32_t x = 0; x < side; ++x)
      packed[((y / 256) * tiles + x / 256) * 2048 + (y % 256) * 8 + (x % 256) / 32] |= uint32_t(cells[y * side + x])
                                                                                       << (x % 32);
  return packed;
}

std::vector<uint8_t> Reference(const std::vector<uint8_t> &cells, uint32_t side) {
  auto result = cells;
  for (int y = 0; y < int(side); ++y) {
    for (int x = 0; x < int(side); ++x) {
      int neighbors = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if (dx || dy)
            neighbors += cells[((y + dy + side) % side) * side + (x + dx + side) % side];
      result[y * side + x] = neighbors == 3 || (neighbors == 2 && cells[y * side + x]);
    }
  }
  return result;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  std::ifstream input(argv[1]);
  const std::string source{std::istreambuf_iterator<char>(input), {}};
  ObjPtr<ICore> core = CreateCore(BACKEND_API_METAL);
  ObjPtr<IDevice> device = core ? core->CreateDeviceByAutoSelection() : nullptr;
  if (!device || source.empty())
    return 2;
  ObjPtr<IQueue> queue = device->GetQueue(QUEUE_TYPE_MAIN);
  life_demo::TiledLife simulation;
  if (!simulation.Initialize(device.Get(), source, 2, 1))
    return 3;
  std::mt19937 random(84911);
  std::vector<uint8_t> cpu(512 * 512);
  // Dense random data includes all tile edges, corners and world-wrap edges.
  for (auto &cell : cpu)
    cell = random() % 3 == 0;
  ObjPtr<ICommandContext> commands = queue->CreateCommandContext();
  if (!simulation.Upload(commands.Get(), Pack(cpu, 512)))
    return 4;
  // Several generations in a single submission exercise GPU-only dependencies.
  for (int generation = 0; generation < 12; ++generation) {
    if (!simulation.Step(commands.Get()))
      return 5;
    cpu = Reference(cpu, 512);
  }
  ObjPtr<IDownloadToken> download =
      commands->CmdDownloadBuffer(simulation.Cells(), 0, simulation.WordCount() * sizeof(uint32_t));
  ObjPtr<ISubmissionToken> submission = commands->Submit();
  std::vector<uint32_t> actual(simulation.WordCount());
  if (!download || !submission || !submission->WaitHost() ||
      !download->CopyTo(actual.data(), actual.size() * sizeof(uint32_t)))
    return 6;
  auto expected = Pack(cpu, 512);
  for (size_t i = 0; i < actual.size(); ++i) {
    if (actual[i] != expected[i]) {
      std::cerr << "Mismatch word " << i << ": " << actual[i] << " != " << expected[i] << '\n';
      return 7;
    }
  }
  std::cout << "PASS 512x512, 12 generations, in-place byte-cache update, edges/corners/wrap\n";
}
