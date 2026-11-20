# Mobile RHI dependency migration

The App owns all Game of Life computation, rendering, shaders, controls, Agent
integration, patterns and their licenses. LongMarch supplies general RHI/util
APIs and mobile build configuration only.

## Baseline and mapping

Old submodule: `159481f63d50f99d731769a3c7bda41a6172e694` (`feat/agent-controls`).
Local preservation ref: `backup/gol-before-mobile-bridge-20261120`.
New submodule: `fae8a79c` on published `mobile-bridge`, PR #125. No merge performed.
The App branch stays `research/ai-agent`; existing unrelated local changes remain.

All 67 tracked entries from old `demo/gol/` were copied byte-for-byte to `game/`
before adaptation. The temporary SHA-256 inventory is
`out/migration-mobile-bridge/source-manifest.json`.

- `demo/gol/<path>` maps to `game/<path>`, except its old `CMakeLists.txt`.
- The old demo CMake entry used LongMarch-only demo packaging helpers and is
  removed. `ios/CMakeLists.txt` and `harmonyos/native/CMakeLists.txt` now build
  App-owned sources and link `LongMarch::Mobile`.
- `main.cpp` remains as the historical desktop entry point; mobile builds exclude it.
- README and pattern-license tool paths now point to the App. The upstream MIT
  text is retained in `game/LongMarch-LICENSE`; pattern CC BY-SA terms remain intact.
- `geometry/util.h` includes only the logging API it uses, avoiding unrelated
  desktop mesh dependencies.
- Shared render/compute shader recipes were extracted into `game/gpu/` headers.
  Runtime and offline preparation use the same pipeline layouts, attributes and
  compute definitions. GoL shaders and pattern data are unchanged.

## Platform boundary

The App retains SwiftUI/MetalKit presentation, XComponent/N-API, native-window
ownership, input, lifecycle, Agent scheduling, packaging and file dialogs.
Metal presentation remains asynchronous through public native device/image/queue
interfaces. It does not switch to the library's synchronous drawable helper.

OHOS presentation signals a semaphore owned by the acquired swapchain image,
rather than reusing a frame semaphore before presentation consumes it. Fence
reset occurs immediately before submission. Failures after acquisition poison
the surface, preventing reuse of uncertain semaphore/fence state; the owner must
recreate it. Partial semaphore creation releases the incomplete swapchain.

LongMarch adds OHOS headless configuration, the OHOS surface extension, subgroup
vote capability queries and explicit shader target selection. These APIs have
no GoL-specific names or data.

## Shader bundles

Do not copy iOS shader cache entries into HarmonyOS. Cache requests include RHI
ABI, backend and platform policy. The host compiler and read-only replay tool
are built separately (`GOL_PREPARE=ON/OFF`).

- `ios/prepare_resources.py` prepares and replays iOS Metal requests.
- `harmonyos/prepare_resources.py` copies patterns only, then invokes
  `gol_vulkan_shaders` to prepare and replay OHOS Vulkan requests independently.
- `mobile/prepare_vulkan_shaders.cpp` uses existing RHI preprocessing and Slang
  compiler functions; it needs no Vulkan GPU and contains only App shader recipes.
- OHOS covers render shaders and both subgroup-vote and portable compute variants.
- Failed cache configuration, preparation or replay stops the operation.

## Validation (2026-11-20)

Evidence directory: `out/migration-mobile-bridge/` (local, not committed).

- iOS simulator build 41: configured/built, installed on iPhone 18 Pro / iOS 27.2,
  launched and rendered. Loaded a built-in glider and exercised zoom through the
  existing smoke input path; backgrounded to Settings and resumed the same process.
- HarmonyOS 1.0.3 build 116: arm64 native library and signed debug HAP built.
  HAP: `GameOfLife-migration-mate70-debug.hap` in the evidence directory.
- `iOSResources-final`: independent Metal prepare and read-only replay passed.
- `OHOSResources-final`: independent Vulkan prepare and read-only replay passed,
  22 requests including both compute variants. Each final bundle has 62 cache entries.
- Native storage, Agent templates and Agent tests passed, including OTCA GPU
  evolution, research isolation/restoration, camera, editing and saving.
- `gol_games_check` passed: input, lifecycle/focus, confirmation/cancellation,
  exact session restore, animation, orientation, stable fold control sizing and
  hit testing. Its initial idle check now waits a bounded 180 frames for the
  existing two-second C2 contrast animation, retaining the idle assertion.
- LongMarch shader-target test passed: fresh-thread default platform, OHOS macros,
  target isolation, conflicting configuration rejection, replay and corruption rejection.
- Independent JS tests passed: research Life/playback and Harmony Agent, native
  bridge, file recovery, research lookup and privacy startup/hosted-policy paths.

The Mate 70 is currently absent from `hdc list targets`. Compilation and host
SPIR-V replay do not establish native Vulkan driver correctness. Installation,
rendering, touch and foreground/background validation on HarmonyOS remain pending
connection. No App submission, release, PR merge or history rewrite was performed.
