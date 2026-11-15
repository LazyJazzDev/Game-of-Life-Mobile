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

std::vector<uint8_t> Reference(const std::vector<uint8_t> &cells, uint32_t side, bool periodic = true) {
  auto result = cells;
  for (int y = 0; y < int(side); ++y) {
    for (int x = 0; x < int(side); ++x) {
      int neighbors = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
          if ((dx || dy) && (periodic || (x + dx >= 0 && y + dy >= 0 && x + dx < int(side) && y + dy < int(side))))
            neighbors += cells[((y + dy + side) % side) * side + (x + dx + side) % side];
      result[y * side + x] = neighbors == 3 || (neighbors == 2 && cells[y * side + x]);
    }
  }
  return result;
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3)
    return 1;
  std::ifstream input(argv[1]);
  const std::string source{std::istreambuf_iterator<char>(input), {}};
  ObjPtr<ICore> core = CreateCore(BACKEND_API_METAL);
  ObjPtr<IDevice> device = core ? core->CreateDeviceByAutoSelection() : nullptr;
  if (!device || source.empty())
    return 2;
  ObjPtr<IQueue> queue = device->GetQueue(QUEUE_TYPE_MAIN);
  life_demo::TiledLife simulation;
  if (!simulation.Initialize(device.Get(), source, 2, argc == 3 ? uint32_t(std::stoul(argv[2])) : 256u))
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
  // Cover every possible nine-cell neighborhood independently, including
  // all carry combinations in the bit-plane vertical sum.
  cpu.assign(512 * 512, 0);
  for (uint32_t pattern = 0; pattern < 512; ++pattern) {
    uint32_t x = (pattern % 32) * 4, y = (pattern / 32) * 4;
    for (uint32_t bit = 0; bit < 9; ++bit)
      cpu[(y + bit / 3) * 512 + x + bit % 3] = (pattern >> bit) & 1;
  }
  commands = queue->CreateCommandContext();
  if (!simulation.Upload(commands.Get(), Pack(cpu, 512)) || !simulation.Step(commands.Get()))
    return 16;
  download = commands->CmdDownloadBuffer(simulation.Cells(), 0, actual.size() * 4);
  submission = commands->Submit();
  if (!submission || !submission->WaitHost() || !download || !download->CopyTo(actual.data(), actual.size() * 4))
    return 17;
  if (actual != Pack(Reference(cpu, 512), 512))
    return 18;
  // Eight live neighbors must die without carrying into the next packed cell.
  // Fixed boundaries additionally leave exactly the four live corner cells.
  for (bool periodic : {false, true}) {
    cpu.assign(512 * 512, 1);
    commands = queue->CreateCommandContext();
    if (!simulation.Upload(commands.Get(), Pack(cpu, 512)) || !simulation.Step(commands.Get(), periodic))
      return 8;
    download = commands->CmdDownloadBuffer(simulation.Cells(), 0, actual.size() * 4);
    submission = commands->Submit();
    if (!submission || !submission->WaitHost() || !download || !download->CopyTo(actual.data(), actual.size() * 4))
      return 9;
    if (actual != Pack(Reference(cpu, 512, periodic), 512))
      return 10;
  }
  commands = queue->CreateCommandContext();
  cpu.assign(512 * 512, 0);
  if (!simulation.Fill(commands.Get(), false))
    return 11;
  for (uint32_t x = 254; x <= 256; ++x) {
    cpu[255 * 512 + x] = 1;
    if (!simulation.Toggle(commands.Get(), x, 255))
      return 12;
  }
  if (!simulation.Step(commands.Get()))
    return 13;
  download = commands->CmdDownloadBuffer(simulation.Cells(), 0, actual.size() * 4);
  submission = commands->Submit();
  if (!submission || !submission->WaitHost() || !download || !download->CopyTo(actual.data(), actual.size() * 4))
    return 14;
  if (actual != Pack(Reference(cpu, 512), 512))
    return 15;
  std::cout
      << "PASS 512x512, 12 random generations, bit-plane cache, all 512 neighborhoods, count-eight overflow, fixed/wrapped edges and cross-tile edits\n";
}
