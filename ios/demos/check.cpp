#include <algorithm>
#include <cmath>
#include <iostream>

#include "DemoSession.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// Renders one Game of Life frame on the host. A prepare build compiles the
// game's Slang shaders into <resources>/shaders; a replay build reads them
// back and reports "Missing bundled shader <key>" for any absent entry.
int main(int argc, char **argv) {
  try {
    if (argc < 3)
      throw std::runtime_error("usage: gol_check <resources> <output-dir> [prepare]");
    std::filesystem::create_directories(argv[2]);
    DemoSession session(argv[1], argc > 3 && std::string(argv[3]) == "prepare");
    session.Render();
    auto extent = session.Image()->Extent();
    std::vector<float> pixels(extent.width * extent.height * 4);
    if (session.Image()->Format() == grassland::graphics::IMAGE_FORMAT_R8G8B8A8_UNORM) {
      std::vector<uint8_t> rgba(pixels.size());
      session.Image()->DownloadData(rgba.data());
      for (size_t i = 0; i < rgba.size(); ++i)
        pixels[i] = rgba[i] / 255.f;
    } else
      session.Image()->DownloadData(pixels.data());
    std::vector<uint8_t> bytes(pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i) {
      if (!std::isfinite(pixels[i]))
        throw std::runtime_error("Nonfinite pixel");
      bytes[i] = uint8_t(std::clamp(pixels[i], 0.f, 1.f) * 255.f + .5f);
    }
    auto output = std::filesystem::path(argv[2]) / "gol.png";
    if (!stbi_write_png(output.c_str(), extent.width, extent.height, 4, bytes.data(), extent.width * 4))
      throw std::runtime_error("Cannot save image");
    std::cout << "PASS gol " << extent.width << "x" << extent.height << "\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
