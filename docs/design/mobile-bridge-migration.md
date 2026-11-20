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
- Independent validation of final LongMarch commit `fae8a79c`: desktop RHI
  regression selected 45 tests, 37 passed and 8 existing window-dependent cases
  skipped. Triangle rebuilt with fresh caches and passed RGB/size readback, real
  simulator drawable colors, pause/resume and destruction/recreation; missing
  and corrupt bundles were rejected. Independent logs: `/tmp/mobile-bridge-ohos-*.log`.
- Independent JS tests passed: research Life/playback and Harmony Agent, native
  bridge, file recovery, research lookup and privacy startup/hosted-policy paths.

## Mate 70 device validation (2026-11-20)

Build 116 was installed over the existing app without uninstalling or clearing data.
`mate70/installed.json` confirms version 1.0.3, versionCode 1000003 and buildVersion 116.
The capturePattern path uses a transient canvas and does not overwrite LastCanvas.gol.

The independent reviewer executed the continuous device test at 14:16:36-45 Beijing
time and visually checked screenshots 03-07. Results:

- Startup and built-in glider import rendered correctly (`01-import.jpeg`).
- Real two-finger input through `uinput -T` enlarged cells approximately twofold
  (`03-before-pinch.jpeg`, `04-after-pinch.jpeg`).
- Play advanced the glider's position and phase; pause restored the play icon
  (`05-evolving.jpeg`, `06-paused.jpeg`).
- Home/background then foreground preserved the paused canvas and view
  (`07-resumed.jpeg`). PID remained 22392 across the transition.

Evidence is local under `out/migration-mobile-bridge/mate70/`: `interaction.log`
records commands, timestamps and PIDs; `pid22392-hilog.log` contains the final
read-only per-process log capture. The captured interval contains no ERROR/FATAL
entries or VK_ERROR/device-lost reports. Most GPU-tagged entries are ArkWeb
configuration messages, not Vulkan validation-layer diagnostics. These checks
establish the tested runtime behavior, not exhaustive GPU synchronization validation.
`02-pinch.jpeg` is an earlier lock-screen capture and is not passing evidence.

No App submission, release, PR merge, App push or history rewrite was performed.
