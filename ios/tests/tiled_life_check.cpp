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

// Compare every mip texel against exact CPU population counts, including
// packed tile-local levels and the global levels above the compute tile size.
bool CheckMipmaps(const std::vector<uint8_t> &cells, uint32_t tile_size, const std::vector<uint32_t> &packed) {
  uint32_t tile_levels = 0;
  for (uint32_t n = tile_size; n > 1; n >>= 1)
    ++tile_levels;
  auto bits_at = [](uint32_t l) { return l == 1 ? 4u : l <= 3 ? 8u : l <= 7 ? 16u : 32u; };
  std::vector<uint32_t> offsets(tile_levels + 2);
  for (uint32_t l = 1; l <= tile_levels; ++l) {
    uint32_t n = tile_size >> l;
    offsets[l + 1] = offsets[l] + (n * n * bits_at(l) + 31) / 32;
  }
  const uint32_t stride = offsets.back(), tiles = 512 / tile_size;
  uint32_t global_offset = tiles * tiles * stride;
  std::vector<uint32_t> previous(cells.begin(), cells.end());
  for (uint32_t level = 1, side = 256; side; ++level, side >>= 1) {
    std::vector<uint32_t> current(side * side);
    for (uint32_t y = 0; y < side; ++y)
      for (uint32_t x = 0; x < side; ++x) {
        uint32_t value = 0;
        for (uint32_t dy = 0; dy < 2; ++dy)
          for (uint32_t dx = 0; dx < 2; ++dx)
            value += previous[(y * 2 + dy) * side * 2 + x * 2 + dx];
        current[y * side + x] = value;
        uint32_t actual;
        if (level <= tile_levels) {
          uint32_t n = tile_size >> level, bits = bits_at(level), per_word = 32 / bits;
          uint32_t tile = (y / n) * tiles + x / n, local = (y % n) * n + x % n;
          actual = (packed[tile * stride + offsets[level] + local / per_word] >> ((local % per_word) * bits)) &
                   (0xffffffffu >> (32 - bits));
        } else
          actual = packed[global_offset + y * side + x];
        if (actual != value) {
          std::cerr << "Mip mismatch level=" << level << " x=" << x << " y=" << y << " expected=" << value
                    << " got=" << actual << "\n";
          return false;
        }
      }
    if (level > tile_levels)
      global_offset += side * side;
    previous = std::move(current);
  }
  return true;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 5)
    return 1;
  std::ifstream input(argv[1]);
  const std::string source{std::istreambuf_iterator<char>(input), {}};
  ObjPtr<ICore> core = CreateCore(BACKEND_API_METAL);
  ObjPtr<IDevice> device = core ? core->CreateDeviceByAutoSelection() : nullptr;
  if (!device || source.empty())
    return 2;
  ObjPtr<IQueue> queue = device->GetQueue(QUEUE_TYPE_MAIN);
  life_demo::TiledLife simulation;
  if (!simulation.Initialize(device.Get(), source, 2, argc >= 3 ? uint32_t(std::stoul(argv[2])) : 256u,
                             argc >= 4 && std::string(argv[3]) == "lookup"
                                 ? life_demo::TiledLife::UpdateMethod::Lookup2x2
                                 : life_demo::TiledLife::UpdateMethod::BitPlanes,
                             true, argc < 5))
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
  // A sleeping tile must wake after edits, including edits across edges/corners.
  // Compare every phase with a dense CPU simulation, not only its final image.
  auto verify_steps = [&](int count, bool periodic) {
    ObjPtr<ICommandContext> step_commands = queue->CreateCommandContext();
    for (int i = 0; i < count; ++i) {
      if (!simulation.Step(step_commands.Get(), periodic))
        return false;
      cpu = Reference(cpu, 512, periodic);
    }
    if (!simulation.UpdateMipmaps(step_commands.Get()))
      return false;
    std::vector<uint32_t> mips(simulation.Mipmaps()->GetDesc().size / 4);
    ObjPtr<IDownloadToken> mip_readback = step_commands->CmdDownloadBuffer(simulation.Mipmaps(), 0, mips.size() * 4);
    auto readback = ObjPtr<IDownloadToken>(
        step_commands->CmdDownloadBuffer(simulation.Cells(), 0, simulation.WordCount() * sizeof(uint32_t)));
    auto submitted = ObjPtr<ISubmissionToken>(step_commands->Submit());
    return readback && submitted && submitted->WaitHost() &&
           readback->CopyTo(actual.data(), actual.size() * sizeof(uint32_t)) && actual == Pack(cpu, 512) &&
           mip_readback && mip_readback->CopyTo(mips.data(), mips.size() * 4) &&
           CheckMipmaps(cpu, argc >= 3 ? uint32_t(std::stoul(argv[2])) : 256u, mips);
  };
  auto submit_upload = [&]() {
    ObjPtr<ICommandContext> upload = queue->CreateCommandContext();
    if (!simulation.Upload(upload.Get(), Pack(cpu, 512)))
      return false;
    ObjPtr<ISubmissionToken> submitted = upload->Submit();
    return submitted && submitted->WaitHost();
  };
  for (auto &cell : cpu)
    cell = random() & 1;
  if (!submit_upload() || !verify_steps(2, true))
    return 34;
  cpu.assign(512 * 512, 1);
  if (!submit_upload() || !verify_steps(0, true))
    return 35;
  cpu.assign(512 * 512, 0);
  commands = queue->CreateCommandContext();
  if (!simulation.Fill(commands.Get(), false))
    return 23;
  submission = commands->Submit();
  if (!submission || !submission->WaitHost() || !verify_steps(4, true))
    return 24;
  const uint32_t edge = argc >= 3 ? uint32_t(std::stoul(argv[2])) : 256u;
  commands = queue->CreateCommandContext();
  for (uint32_t y : {edge - 1, edge})
    for (uint32_t x : {edge - 1, edge}) {
      cpu[y * 512 + x] ^= 1;
      if (!simulation.Toggle(commands.Get(), x, y))
        return 25;
    }
  submission = commands->Submit();
  if (!submission || !submission->WaitHost() || !verify_steps(8, true))
    return 26;
  commands = queue->CreateCommandContext();
  cpu[edge * 512 + edge + 1] ^= 1;
  if (!simulation.Toggle(commands.Get(), edge + 1, edge))
    return 27;
  submission = commands->Submit();
  if (!submission || !submission->WaitHost() || !verify_steps(32, true))
    return 28;
  // Editing a distant sleeping tile must preserve an already active oscillator.
  cpu.assign(512 * 512, 0);
  for (uint32_t x = 9; x <= 11; ++x)
    cpu[10 * 512 + x] = 1;
  if (!submit_upload() || !verify_steps(4, true))
    return 31;
  commands = queue->CreateCommandContext();
  for (uint32_t x : {400u, 401u, 402u}) {
    cpu[400 * 512 + x] ^= 1;
    if (!simulation.Toggle(commands.Get(), x, 400))
      return 32;
  }
  submission = commands->Submit();
  if (!submission || !submission->WaitHost() || !verify_steps(12, true))
    return 33;
  // Boundary mode changes can wake a stable configuration without a cell edit.
  cpu.assign(512 * 512, 0);
  for (uint32_t y : {0u, 511u})
    for (uint32_t x : {0u, 511u})
      cpu[y * 512 + x] = 1;
  if (!submit_upload() || !verify_steps(4, true) || !verify_steps(4, false))
    return 29;
  cpu.assign(512 * 512, 0);
  for (uint32_t y : {10u, 11u})
    for (uint32_t x : {0u, 1u, 510u, 511u})
      cpu[y * 512 + x] = 1;
  if (!submit_upload() || !verify_steps(4, false) || !verify_steps(12, true))
    return 30;
  std::cout << "PASS dormant tiles, cross-corner edits, uploads, clear and boundary-mode wakeups\n";
  if (argc >= 4 && std::string(argv[3]) == "lookup") {
    // Shift each 4x4 input by one so the central output is even-aligned.
    life_demo::TiledLife exhaustive;
    if (!exhaustive.Initialize(device.Get(), source, 4, uint32_t(std::stoul(argv[2])),
                               life_demo::TiledLife::UpdateMethod::Lookup2x2, true, argc < 5))
      return 19;
    std::vector<uint8_t> all(1024 * 1024);
    for (uint32_t pattern = 0; pattern < 65536; ++pattern) {
      uint32_t x = (pattern % 256) * 4 + 1, y = (pattern / 256) * 4 + 1;
      for (uint32_t bit = 0; bit < 16; ++bit)
        all[((y + bit / 4) % 1024) * 1024 + (x + bit % 4) % 1024] = (pattern >> bit) & 1;
    }
    commands = queue->CreateCommandContext();
    if (!exhaustive.Upload(commands.Get(), Pack(all, 1024)) || !exhaustive.Step(commands.Get()))
      return 20;
    std::vector<uint32_t> full(exhaustive.WordCount());
    download = commands->CmdDownloadBuffer(exhaustive.Cells(), 0, full.size() * 4);
    submission = commands->Submit();
    if (!submission || !submission->WaitHost() || !download || !download->CopyTo(full.data(), full.size() * 4))
      return 21;
    if (full != Pack(Reference(all, 1024), 1024))
      return 22;
    std::cout << "PASS all 65536 lookup inputs, including wrapped 4x4 windows\n";
  }
  std::cout
      << "PASS 512x512, 12 random generations, tile update, all 512 neighborhoods, count-eight overflow, fixed/wrapped edges and cross-tile edits\n";
}
