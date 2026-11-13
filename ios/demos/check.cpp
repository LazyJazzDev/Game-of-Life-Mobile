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
    auto extent = session.Image()->GetDesc();
    std::vector<uint8_t> bytes(size_t(extent.width) * extent.height * 4);
    grassland::rhi::ObjPtr<grassland::rhi::ICommandContext> commands = session.Game()->Queue()->CreateCommandContext();
    grassland::rhi::ObjPtr<grassland::rhi::IDownloadToken> download = commands->CmdDownloadImage(session.Image());
    grassland::rhi::ObjPtr<grassland::rhi::ISubmissionToken> submit = commands->Submit();
    if (!submit || !submit->WaitHost() || !download || !download->CopyTo(bytes.data(), bytes.size()))
      throw std::runtime_error("Image readback failed");
    auto output = std::filesystem::path(argv[2]) / "gol.png";
    if (!stbi_write_png(output.c_str(), extent.width, extent.height, 4, bytes.data(), extent.width * 4))
      throw std::runtime_error("Cannot save image");
    std::cout << "PASS gol " << extent.width << "x" << extent.height << "\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
