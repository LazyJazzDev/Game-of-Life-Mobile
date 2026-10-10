# Game of Life for HarmonyOS

The 生命游戏 / Game of Life app (`net.lazyjazz.gameoflife`): an ArkUI page over the
desktop Game of Life from LongMarch (`../LongMarch/demo/gol`), rendered with
LongMarch's Vulkan backend into an XComponent surface.

## Layout

| Path | Purpose |
|---|---|
| `AppScope/` | Bundle name, version, app name (en/zh) and the layered icon (`demo/gol/tools/app_icon.py`) |
| `entry/src/main/ets/entryability/EntryAbility.ets` | Fullscreen window, native initialization, foreground/background |
| `entry/src/main/ets/pages/Index.ets` | Game page: touch and pinch input, fixed-orientation icons, grid-size popover, pattern library |
| `entry/src/main/resources/` | Strings (en/zh) and the bundled game resources (`rawfile/Resources`, generated) |
| `entry/src/main/cpp/` | Native build entry for DevEco; builds `native/` |
| `native/` | N-API bridge, render-thread host, Vulkan presentation and the hosted game session |
| `prepare_resources.py` | Stages the patterns and SPIR-V shaders into `rawfile/Resources` |
| `build_hap.sh` | Command-line HAP and `.app` builds |

## Build

The minimum supported system is HarmonyOS 6.0 (API 20), configured as
`compatibleSdkVersion: "6.0.0(20)"`. The current package is version 1.0.1,
build 1000008. The AGC draft still contains the earlier package and has not
been resubmitted.

Install the shared mobile dependencies once (`../mobile`), into `../out/mobile/deps`
or a directory named by `LONGMARCH_MOBILE_DEPS`. Then stage the game's prepared
resources, which hold the pattern library and the SPIR-V the game requests:

```sh
python3 harmonyos/prepare_resources.py --source <GameOfLifeResources>
sh harmonyos/build_hap.sh
```

The source is a game resource folder prepared by LongMarch's iOS tools
(`prepare_game_resources.py`); only its `slang-*` SPIR-V entries are staged. A
staged bundle is tied to the Slang compiler that prepared it.

## Signing

`build-profile.json5` is committed without signing material. Add signing
configurations locally (DevEco Studio's automatic signing, or AppGallery Connect
certificates and profiles for `net.lazyjazz.gameoflife`) and set the product's
`signingConfig`, but do not commit them. A debug profile installs on registered
devices; for AppGallery, use the release profile and build the `.app` package:

```sh
BUILD_MODE=release TASK=assembleApp sh harmonyos/build_hap.sh
```

The package is written to `build/outputs/default/harmonyos-default-signed.app`.

## Behavior

- The page opens the game as soon as the bundled resources are extracted.
- The interface and button icons keep the orientation the game opened in.
  HarmonyOS does not subscribe to gravity sensors or offer sensor-based rotation.
- One finger edits cells or pans an enlarged grid; two fingers pan and pinch.
- Open and save start in the in-app pattern library (736 Life Lexicon patterns in
  folders, plus My Patterns); the system document picker is one link away.
- Built-in patterns are adapted from the Life Lexicon by Stephen A. Silver et al.,
  licensed under CC BY-SA 3.0 (`../LongMarch/demo/gol/patterns/LICENSE.md`).

## Privacy verification

The HarmonyOS implementation contains no sensor subscriptions, consent prompt or
rotation preference. Verify an upgrade from the earlier consent-enabled build:
the game opens directly and icons stay fixed even if rotation was previously
enabled. Check normal editing, pattern loading and saving on a real phone.

When submitting this sensor-free package, update AppGallery privacy labels and
the listing to remove gravity sensor collection. Do not resubmit the older
sensor-reading package with labels claiming that it does not read sensors.

## Mate 60 graphics investigation

On 2026-10-10, a Huawei cloud Mate 60 RS reproduced the startup error. The
Maleoon 910 driver advertises Vulkan 1.2.275 (driver version 3906508039).
`vkCreateDevice` returns `VK_ERROR_EXTENSION_NOT_PRESENT` (-7):
`VK_KHR_dynamic_rendering` and `VK_EXT_extended_dynamic_state` are absent,
and both corresponding feature bits are false. Descriptor indexing and scalar
block layout bits requested by this application are supported.

The backend now negotiates these capabilities instead of requiring them. When
unavailable, it uses cached render passes/framebuffers and lazy static-topology
pipeline variants. All four capability combinations passed GPU readback tests
on MoltenVK, including its Vulkan 1.2 extension path.

Cloud build 1000002 passed device creation but crashed inside
`libbishenggpucompiler_v200.so.15` during first-draw graphics pipeline creation.
SPIR-V validation passed, and standard optimization did not resolve the crash.
The game vertex shader's mixed-type `ByteAddressBuffer.Load<InstanceInfo>` emitted
float and uint descriptor variables sharing one binding. Loading uint4 words and
bitcasting the float fields eliminates those aliased views; four column vectors
are transposed into Slang's matrix constructor without changing the CPU layout.
This restored startup and rendering on the same Mate 60 RS running
6.0.0.108(SP6C00E107R8P7). The shader change is LongMarch `2c32fcd`.

The final source and regenerated resources are packaged as 1.0.1 build 1000008,
minimum HarmonyOS 6.0 (API 20), target API 26. The cloud device passed startup,
pattern-library loading, continuous evolution, pause and background/resume checks;
rendering showed no abnormal stripes. Metal renderer/input/animation/focus/
orientation regression checks pass as well. Cloud test evidence and package
SHA-256 hashes are under `out/harmonyos/release-1.0.1-1000008/`; the earlier
failure logs remain under `out/harmonyos/mate60-fallback-1000002/`.
Device-creation failures now report the VkResult, GPU/driver, requested extensions
and relevant feature support under the `LongMarchGPU` HiLog tag on HarmonyOS.

## nova 15 Pro graphics investigation

On 2026-10-10, cloud device KLE-AL00 running 7.0.0.105(SP6C00E105R3P3)
reproduced dark, regularly striped output. Its Maleoon 920C reports Vulkan 1.3
and driver version 3104880233. A constant-color fragment shader reproduced the
stripes; transfer clears and a blit of a cleared image displayed correctly.

The trigger was assigning each dynamic-rendering attachment's own image view
to `resolveImageView` while `resolveMode` was `VK_RESOLVE_MODE_NONE`. These
single-sampled attachments retain zero-initialized resolve fields (NONE and a null
view) for both color and depth. Removing the self-resolve views restored normal
game rendering on this device without changing the game shaders or grid.
Vulkan specifies that the resolve view is ignored in this mode, so this is a
device-tested compatibility workaround, not proof that self-resolve with NONE
violates the specification. The separate Mate 60 pipeline-compiler workaround is described above.

## Build identification

Keep `versionName` at the intended marketing version (currently `1.0.1`).
Increment `AppScope/app.json5` `versionCode` for each distinct build distributed
for testing or submission, including rebuilds under the same marketing version.
Record both values, the LongMarch commit and the package SHA-256 with the artifact;
include the version and build number in uploaded filenames and delivery reports.
Compatibility-only cloud packages must be identified separately from release
packages when their minimum OS differs. Do not submit these diagnostic packages.
