#include <zlib.h>

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>

#include "gpu/tiled_life.h"
#include "gpu/tiled_life_storage.h"

static void Put(std::string &bytes, size_t offset, uint32_t value, int count = 4) {
  for (int i = 0; i < count; ++i)
    bytes[offset + i] = char(value >> (8 * i));
}

static uint32_t Get(const std::string &bytes, size_t offset) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i)
    value |= uint32_t(uint8_t(bytes[offset + i])) << (8 * i);
  return value;
}

static void IndexCRC(std::string &bytes) {
  Put(bytes, 20, uint32_t(crc32(0, reinterpret_cast<const Bytef *>(bytes.data() + 24), Get(bytes, 16) * 24)));
}

int main() {
  using namespace life_demo;
  std::vector<uint32_t> words(TiledLife::kWorldSize * TiledLife::kWorldSize / 32, 0), decoded;
  std::string error, saved;
  auto roundtrip = [&] {
    std::ostringstream output(std::ios::binary);
    if (!WriteTiledState(output, words, error))
      return false;
    saved = output.str();
    std::istringstream input(saved, std::ios::binary);
    return saved.substr(0, 8) == "GOLSPRS2" && ReadTiledState(input, decoded, error) && decoded == words &&
           error.empty();
  };
  auto reject = [&](const std::string &bytes) {
    std::istringstream bad(bytes, std::ios::binary);
    std::vector<uint32_t> sentinel{123};
    return !ReadTiledState(bad, sentinel, error) && !error.empty() && sentinel == std::vector<uint32_t>{123};
  };
  if (!roundtrip() || saved.size() != 24)
    return 1;
  words[0] = 1;
  words.back() = 0x80000000;
  if (!roundtrip() || saved.size() != 84)
    return 2;
  const auto sparse_size = saved.size();
  // An independent V1 fixture includes sparse and raw records, not a V2 export.
  std::string legacy(20 + 14 + 8 + 8192 + 14, '\0');
  legacy.replace(0, 8, "GOLSPRS1");
  Put(legacy, 8, 16384);
  Put(legacy, 12, 256);
  Put(legacy, 16, 3);
  Put(legacy, 20, 0);
  Put(legacy, 24, 1);
  Put(legacy, 28, 0, 2);
  Put(legacy, 30, 1);
  Put(legacy, 34, 1);
  Put(legacy, 38, 0xffffffff);
  std::mt19937 random(84911);
  for (size_t i = 0; i < 2048; ++i) {
    words[2048 + i] = random();
    Put(legacy, 42 + 4 * i, words[2048 + i]);
  }
  Put(legacy, 8234, 4095);
  Put(legacy, 8238, 1);
  Put(legacy, 8242, 2047, 2);
  Put(legacy, 8244, 0x80000000);
  std::istringstream old(legacy, std::ios::binary);
  if (!ReadTiledState(old, decoded, error) || decoded != words)
    return 3;
  if (!roundtrip() || Get(saved, 24 + 24 + 16) != 0)
    return 4;  // Random tile falls back to raw.
  const auto mixed_size = saved.size();
  for (size_t cut : {size_t(0), size_t(7), size_t(23), size_t(47), saved.size() - 1})
    if (!reject(saved.substr(0, cut)))
      return 5;
  if (!reject(saved + "x") || !reject(legacy + "x") || !reject(legacy.substr(0, legacy.size() - 1)))
    return 6;
  auto corrupt = saved;
  corrupt.back() ^= 1;
  if (!reject(corrupt))
    return 7;
  for (auto mutation : {std::pair<size_t, uint32_t>{48, 0}, {52, 0}, {36, 0xffffffff}, {40, 4}, {16, 4097}}) {
    corrupt = saved;
    Put(corrupt, mutation.first, mutation.second);
    if (mutation.first != 16)
      IndexCRC(corrupt);
    if (!reject(corrupt))
      return 8;
  }
  // Repetitive dense data and sparse indexed data both benefit from compression.
  std::fill(words.begin(), words.end(), 0);
  std::fill(words.begin(), words.begin() + 2048, 0xffffffff);
  for (size_t i = 2048; i < 4096; i += 17)
    words[i] = 7;
  if (!roundtrip() || saved.size() >= 1000 || !(Get(saved, 40) & 2))
    return 9;
  const auto repetitive_size = saved.size();
  corrupt = saved;
  Put(corrupt, 44, Get(corrupt, 44) ^ 1);
  IndexCRC(corrupt);
  if (!reject(corrupt))
    return 10;
  // A valid compressed stream followed by junk must not be accepted silently.
  const uint32_t first_size = Get(saved, 32);
  corrupt = saved;
  corrupt.insert(72 + first_size, 1, 'x');
  Put(corrupt, 32, first_size + 1);
  Put(corrupt, 52, first_size + 1);
  IndexCRC(corrupt);
  if (!reject(corrupt))
    return 11;
  std::fill(words.begin(), words.end(), 0xffffffff);
  const auto start = std::chrono::steady_clock::now();
  if (!roundtrip() || saved.size() >= 1024 * 1024)
    return 12;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  const auto all_alive_size = saved.size();
  for (auto &word : words)
    word = random();
  if (!roundtrip() || saved.size() > words.size() * 4 + 24 + 4096 * 24)
    return 13;
  const auto random_size = saved.size();
  std::cout
      << "PASS V1 compatibility, V2 roundtrips, raw fallback, corruption and bounded decompression; empty 24 B, sparse "
      << sparse_size << " B, mixed " << mixed_size << " B, repetitive " << repetitive_size << " B; all-alive world "
      << all_alive_size << " B, roundtrip " << elapsed << " s; random world " << random_size << " B\n";
}
