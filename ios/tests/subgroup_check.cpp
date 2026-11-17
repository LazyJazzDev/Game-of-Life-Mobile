#include <array>
#include <iostream>

#include "grassland/rhi/rhi.h"
#include "grassland/rhi/shader_cache.h"

using namespace grassland::rhi;

int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  // Exercise the iOS conversion path on Metal, including its cache key.
  ConfigureShaderCache({argv[1], false, true});
  ObjPtr<ICore> core = CreateCore(BACKEND_API_METAL);
  ObjPtr<IDevice> device = core ? core->CreateDeviceByAutoSelection() : nullptr;
  if (!device)
    return 2;
  const char *source = R"(
RHI_RW_BYTE_ADDRESS_BUFFER(0, 0, result);
[numthreads(128, 1, 1)] void Main(uint index : SV_GroupIndex) {
  uint lane = WaveGetLaneIndex(), size = WaveGetLaneCount();
  uint shuffled = WaveReadLaneAt(index, (lane + 5) % size);
  result.Store(index * 12, lane);
  result.Store(index * 12 + 4, size);
  result.Store(index * 12 + 8, shuffled);
}
)";
  PipelineLayoutDesc layout;
  layout.groups = {{{{0, BINDING_RW_BYTE_ADDRESS_BUFFER, 1, SHADER_VISIBILITY_COMPUTE}}}};
  ObjPtr<IBuffer> buffer = device->CreateBuffer({128 * 12});
  ObjPtr<IBindingGroup> bindings = device->CreateBindingGroup(layout.groups[0]);
  ObjPtr<IShader> shader = device->CreateShaderFromSource(source, "Main", SHADER_STAGE_COMPUTE);
  if (!buffer || !bindings || !shader || !bindings->BindResource(0, buffer.Get()))
    return 3;
  ObjPtr<IComputeProgram> program = device->CreateComputeProgram({shader.Get(), layout});
  ObjPtr<IQueue> queue = device->GetQueue(QUEUE_TYPE_MAIN);
  ObjPtr<ICommandContext> commands = queue ? queue->CreateCommandContext() : nullptr;
  if (!program || !commands || !commands->CmdBindComputeProgram(program.Get()) ||
      !commands->CmdBindGroup(0, bindings.Get()) || !commands->CmdDispatch(1, 1, 1))
    return 4;
  ObjPtr<IDownloadToken> download = commands->CmdDownloadBuffer(buffer.Get(), 0, 128 * 12);
  ObjPtr<ISubmissionToken> submission = commands->Submit();
  std::array<uint32_t, 128 * 3> values{};
  if (!download || !submission || !submission->WaitHost() || !download->CopyTo(values.data(), sizeof(values)))
    return 5;
  for (uint32_t i = 0; i < 128; ++i) {
    const auto lane = values[i * 3], size = values[i * 3 + 1], shuffled = values[i * 3 + 2];
    if (!size || size > 128 || lane >= size || lane != i % size || shuffled != i - lane + (lane + 5) % size) {
      std::cerr << "Inconsistent subgroup at " << i << ": lane=" << lane << " size=" << size << " shuffled=" << shuffled
                << '\n';
      return 6;
    }
  }
  std::cout << "PASS iOS subgroup size, lane index and cross-quad shuffle agree\n";
}
