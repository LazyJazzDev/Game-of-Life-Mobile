#include <iostream>
#include <sstream>

#include "gpu/tiled_life.h"
#include "gpu/tiled_life_storage.h"

int main() {
  using namespace life_demo;
  std::vector<uint32_t> words(TiledLife::kWorldSize * TiledLife::kWorldSize / 32, 0), decoded;
  std::string error;
  std::ostringstream empty(std::ios::binary);
  if (!WriteTiledState(empty, words, error) || empty.str().size() != 20)
    return 1;
  words[0] = 1;
  words.back() = 0x80000000;
  std::ostringstream sparse(std::ios::binary);
  if (!WriteTiledState(sparse, words, error) || sparse.str().size() != 48)
    return 2;
  std::istringstream sparse_input(sparse.str(), std::ios::binary);
  if (!ReadTiledState(sparse_input, decoded, error) || decoded != words)
    return 3;
  for (size_t i = 2048; i < 4096; ++i)
    words[i] = uint32_t(i * 7919);
  std::ostringstream mixed(std::ios::binary);
  if (!WriteTiledState(mixed, words, error) || mixed.str().size() != 8248)
    return 4;
  std::istringstream input(mixed.str(), std::ios::binary);
  if (!ReadTiledState(input, decoded, error) || decoded != words)
    return 5;
  const auto saved = mixed.str();
  auto reject = [&](std::string bytes) {
    std::istringstream bad(bytes, std::ios::binary);
    std::vector<uint32_t> sentinel{123};
    return !ReadTiledState(bad, sentinel, error) && !error.empty() && sentinel == std::vector<uint32_t>{123};
  };
  if (!reject(saved.substr(0, saved.size() - 1)) || !reject(saved + "x"))
    return 6;
  auto corrupt = saved;
  // First sparse word index is outside its 2048-word tile.
  corrupt[28] = char(255);
  corrupt[29] = char(255);
  if (!reject(corrupt))
    return 7;
  corrupt = saved;
  // Duplicate the first tile index in the second record.
  for (int i = 0; i < 4; ++i)
    corrupt[34 + i] = 0;
  if (!reject(corrupt))
    return 8;
  std::cout
      << "PASS sparse save: empty 20 B, two remote live cells 48 B, mixed dense/sparse 8248 B; invalid data rejected atomically\n";
}
