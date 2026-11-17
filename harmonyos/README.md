# Game of Life for HarmonyOS

The 生命游戏 / Game of Life app (`net.lazyjazz.gameoflife`): an ArkUI page over the
desktop Game of Life from LongMarch (`../LongMarch/demo/gol`), rendered with
LongMarch RHI's Vulkan backend into an XComponent surface.

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

## Foldable layout

The tiled canvas uses a fixed 360 vp reference width for its base cell pitch
(360 × 0.95 / 64 vp), multiplied by the user's zoom and current display density.
Folding, unfolding and rotation change the visible range rather than cell size;
pixel-based pan offsets also follow density changes to preserve the viewed cell.

Native controls use a 40 vp icon size converted to framebuffer pixels by ArkUI's
current display density. Folding changes available canvas space and toolbar
placement, not the target control size; unusually small windows shrink controls
to keep them inside the surface. Desktop and iOS sizing remain unchanged.

The window uses `AUTO_ROTATION_RESTRICTED`: HarmonyOS handles orientation and
respects the user's rotation lock. The app does not subscribe to motion sensors.
Window/area changes refresh density and cutout/gesture insets, cancel in-flight
touch gestures and dismiss stale size popovers. Landscape uses side toolbars;
portrait uses top/bottom controls without resetting the simulation.

Build 1000009 was tested on cloud Mate XT 2 (HarmonyOS 7.0.0.107), including
three-screen portrait/landscape and two-screen expansion with a loaded pattern.
Shared Metal regression tests exercise control hit testing and constant vp
thickness across four window extents and three densities. Evidence is under
`out/harmonyos/fold-1000009/`.

## Build

The minimum supported system is HarmonyOS 6.0 (API 20), configured as
`compatibleSdkVersion: "6.0.0(20)"`. The current package is version 1.0.1,
`versionCode` 1000001 and `buildVersion` 10 (foldable layout).

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

Keep `versionName` at the intended marketing version (currently `1.0.1`) and
`versionCode` at that release's version code (currently `1000001`). Increment
`AppScope/app.json5` **`buildVersion`** for distinct builds of the same version;
this is AGC's separate “构建版本” column. Do not increment `versionCode` merely
to identify rebuilds. Record all three fields, the LongMarch commit and package
SHA-256 with each artifact. Name uploads `GameOfLife-1.0.1-build-10.app`, for example.
The older artifacts named `1000002` through `1000009` incorrectly used
`versionCode` as a build counter; those names are historical evidence only.
Compatibility-only cloud packages must be identified separately from release
packages when their minimum OS differs. Do not submit these diagnostic packages.

### Hosted privacy consent (build 11)

The HarmonyOS app uses AppGallery Kit's hosted privacy service. Configure a
HarmonyOS-only privacy statement under Distribution > More services > Agreement
services, generate it, and associate it with the release in AGC before publishing.
The app does not provide a second consent dialog or fall back to a local consent
flag. Native initialization waits for consent matching the current hosted policy
ID and version. Missing hosting configuration or a service error leaves the app
on a retry/exit screen. This is intentional; do not publish this build without
associating the hosted policy.

The pattern library footer provides **Privacy policy** and **Withdraw consent**.
The policy URL comes from `getAppPrivacyMgmtInfo()`, rather than the shared iOS
privacy webpage. Withdrawal preserves saved patterns and exits the app; the
system asks for consent again on the next launch.

For an unpublished app, Huawei requires a **debug** build on a physical device
with working AppGallery and network access. In a local debug-only copy of the
entry module metadata, set `appgallery_privacy_hosted` to `"1"` and
`appgallery_privacy_link_privacy_statement` to the generated HTTPS policy URL.
Restore the module file after building; do not treat a side-loaded release build
as a test of production hosting. Simulators do not support this service.

Before release, test fresh installation, accept/decline, relaunch without a
second prompt, policy viewing, withdrawal, updated policy consent, and offline
relaunch after acceptance. Check that no native resources are initialized before
consent and saved patterns survive withdrawal. Huawei's documentation does not
specify all offline cases; these must be verified on-device.

Controller tests (uses DevEco's bundled TypeScript compiler):

```sh
/Applications/DevEco-Studio.app/Contents/tools/node/bin/node harmonyos/tests/hosted_privacy.cjs
```

References: [setup](https://developer.huawei.com/consumer/cn/doc/app/agc-help-privacy-policy-app-0000002282162168),
[client integration](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/store-privacy).

Hosted policy configured in the AGC 1.0.1 draft:
https://agreement-drcn.hispace.dbankcloud.cn/index.html?lang=zh&agreementId=2060870700201172672

Build 12 keeps versionName 1.0.1 and versionCode 1000001. File-operation
failures use `fileError`, separate from fatal renderer `error`. A malformed
import preserves the current grid, releases the file operation, and reports a
localized message without stopping rendering. A picker or IO failure cancels
only a still-pending native request.

```sh
/Applications/DevEco-Studio.app/Contents/tools/node/bin/node harmonyos/tests/file_recovery.cjs
```

Build 13 requests system consent automatically on launch when no current consent
exists. It does not show an intermediate consent page. Acceptance starts the
game; cancellation exits. Only service failures expose retry/exit controls.
`harmonyos/tests/privacy_startup.cjs` checks automatic launch and duplicate-event,
acceptance, cancellation, failure and late-completion behavior.

Build 14 also updates this application's `harmonyos/native/Host.cpp` (which is
built separately from LongMarch's demo host) to publish recoverable `fileError`.
Empty/undefined picker results cancel silently. File alerts use localized
resource keys, never raw platform exception text. Picker cancellation coverage
includes both open/save and empty, null and undefined results.

Version 1.0.2 uses `versionCode=1000002` and `buildVersion=15`, with HarmonyOS 6.0
(API 20) as the minimum. Native rendering links `grassland/rhi` rather than the
removed `graphics` module. The native surface presents images on the RHI main
queue and retains each image until its presentation blit completes.

Stage a newly prepared shader bundle with `prepare_resources.py`; preprocessed
source and SPIR-V entries are required. The Vulkan backend requires Vulkan 1.2
and timeline semaphores, and uses its render-pass fallback where dynamic rendering
is unavailable. The byte-address shader loads retain the Mate 60 driver workaround.

## Deterministic store capture (cloud devices)

An explicit Ability Want can prepare the actual app for screenshots after normal
privacy consent and the first rendered frame. Without these arguments, startup
is unchanged. `capturePattern` is an exact built-in pattern name; `captureZoom`
is a positive pinch multiplier (maximum 10); optional `capturePanel` is `library`
or `save`. `captureLanguage` accepts `zh-CN` or `en-US` and sets the app language.
The imported board stays paused. Stop the app before each new capture launch.
For cloud shells that reject quoted names, `pi-orbital` and `koks-galaxy` are
aliases for `pi orbital` and `Kok's galaxy`; they load the same built-in patterns.
With `capturePanel=save`, `captureSaveName` pre-fills a localized display name
instead of the timestamp for store screenshots. Empty values retain the normal
default name. This does not save a file or change ordinary launches.

In the Huawei cloud device's **Control parameters → Shell script** panel:

```sh
aa force-stop net.lazyjazz.gameoflife
aa start -b net.lazyjazz.gameoflife -a EntryAbility --ps capturePattern pulsar --ps captureZoom 3 --ps capturePanel library --ps captureLanguage zh-CN
```

Capture through the cloud screenshot panel after the real UI settles. Use the
same pattern and zoom parameters on iOS; do not substitute other device frames.

### Canvas confirmation and recovery

Reset and Randomize require a localized system confirmation dialog; dismissing
or canceling it preserves the canvas and previous play/pause state. On background
entry the ability waits for the render thread to checkpoint the full compressed
world to `filesDir/LastCanvas.gol`. A fresh launch restores it paused, after privacy
consent and native initialization. A temporary file and atomic replacement protect
the previous snapshot from incomplete writes. This private snapshot is independent
of My Patterns; explicit screenshot capture sessions do not read or replace it.
An abrupt process kill without a background callback restores the last completed
checkpoint.

## Current Agent provider

Both platforms now use OpenAI `gpt-6.1-sol` with high reasoning via Responses.
`prepare_agent.py` stages the local `tools/openai-api.key`; the credential remains
ignored by Git. Consent UI names OpenAI. Requests disable server-side response
storage, preserve encrypted reasoning/assistant phase for tool continuation,
and execute only complete responses. Stop and network-error recovery retain
completed task context without replaying incomplete tool batches. The loop has
no fixed round or tool-call limit. See `ios/agent/README.md` for the shared tool
contract and fresh-install testing requirements.

The following build reports describe historical DeepSeek validation runs.

## Agent research parity (1.0.3 build 103)

HarmonyOS now stages the same 17-tool contract, generic system prompt, reference
links and HashLife engine as iOS. `prepare_agent.py`, called by `build_hap.sh`,
copies these assets and the local `tools/deepseek-api.key`; the credential is
ignored by Git and is not exposed through an editing field. Network consent
remains explicit. No OTCA template, construction guide or pattern catalog is
available to the Agent. Previously installed user templates use a separate
`AgentTemplatesBasic` directory.

`mobile/agent-research.js` is the shared disposable-worker implementation used
by WKWebView and ArkWeb. It supports JavaScript, Pyodide Python, generic B3/S23
HashLife, RLE codecs, stdout and a 60-second execution deadline. The worker
receives research documents only, with no credential or native filesystem
bridge. The ArkWeb bridge only returns execution results. Fetched documents
and Agent-authored programs live in an in-memory workspace; returned RLE
artifacts are saved through the native template implementation.

The DeepSeek Flash loop supports 192 rounds / 768 tool calls, streamed replies
and tool arguments, an activity list and current-step timer. Oversized model
responses are discarded without executing partial tool calls, then retried
with smaller steps and low reasoning effort. Cancellation also stops the
research worker. Model reasoning is retained for the API protocol but never
shown in the UI.

Mate 70 build 103 passed the same Python runtime regression as iOS (custom
list subclasses, helper-name collisions, RLE round trips and repeated 66,000-cell
results). Streaming tests cover split UTF-8 bytes and tool arguments, reasoning
isolation, truncation recovery, cancellation, consent and HTTP errors. Existing
privacy startup/hosted policy and file recovery regressions passed. Build 104 completed a fresh-install live DeepSeek Flash integration run on Mate 70:
25 tool calls exercised public fetching, workspace read/write, JavaScript and
Python evolution, template-save error recovery and native canvas placement.
The final three-cell oscillator population/hash matched its saved template and
the canvas remained paused. Evidence is in `out/agent/mate70-port104/`. This is
not a full on-device 6×6 OTCA trial.

Debug-only startup parameter `researchSelftest` accepts Python test source and
writes `files/ResearchSelftest.json` through the same research/tool wrapper.
`agentSmoke` runs an explicit task and saves `files/AgentSmoke.json` when finished.
Both wait for native readiness rather than a rendered-frame count, since a
paused canvas may render only one frame. Do not copy previous workspace or
conversation state into fresh full trials.


### Mate 70 autonomous OTCA verification (build 105)

A fresh uninstall/install trial on physical Mate 70 (PLU-AL10) completed with
`deepseek-flash`, high reasoning, and 72 tool calls. The only supplied task was
to independently research, construct and verify a genuine 6×6 B3/S23 OTCA array.
No previous workspace, construction recipe, coordinates or OTCA template was
injected. The Agent fetched public sources, implemented its own Golly pattern
adapter, corrected its rule-bit and RLE/template composition mistakes, and
saved `otca_off_b3s23`, `otca_on_b3s23` and `otca6x6_rpent2`.

Independent checks used artifacts downloaded from this phone, not the earlier
iOS run. Both unit templates matched canonical reference cell sets exactly.
The final 12,298×12,298 array contains 1,072,443 live cells and represents a
6×6 macro grid with initial live positions (2,2), (3,2), (1,3), (2,3), (2,4).
An independent bgolly HashLife run decoded each tile's central display and
matched ordinary B3/S23 at generation 0, the 35,328-generation warm-up, and
macro generations 1–3 (70,656, 105,984 and 141,312 actual generations).
The phone Agent also tested an L-to-block transition and a small glider array.

Native placement at (-6149,-6149) reported the same region population and hash
as the saved template: `4488188516091322777`. The full canvas population was also
1,072,443, with no extra cells. `canvas_fit` succeeded and final `canvas_state`
confirmed paused. Evolution verification used research HashLife plus independent
bgolly; this does not claim native GPU execution of 141,312 generations.

Local evidence: `out/agent/mate70-otca105/AgentSmoke.json`, downloaded RLE files,
`independent-saved-units.json`, `independent-array-verification.json`,
`verify-array.py` and `final.jpeg`. The screenshot includes the open Agent panel
over the fitted canvas. The phone retains build 105 and its successful canvas.
The subsequent source fix preserving primitive/array research results was built
but not installed during this trial, to avoid interrupting or replacing it.


### Agent work visualization (1.0.3 build 109 / iOS build 30)

Agent work automatically collapses the conversation into the compact status/input
bar with Stop. Completion, cancellation and failure leave a visible outcome.
Source code and fetched documents stay in the background; only the latest action
is shown. No Web canvas, large research panel or system frame covers the app.

Research snapshots temporarily replace the native canvas's displayed cell buffers.
A separate TiledLife instance uses the same CellGridTheme shader, grid detail,
contrast, mipmaps and viewport rendering, with pale-blue live cells. Dead cells, gaps and the background retain their original colors. The original
canvas remains authoritative for saves and resumes when research finishes or is
stopped. The preview is read-only; panning and zooming remain available.

`mobile/agent-preview.js` transports exact RLE snapshots from research codecs and
HashLife input/intermediate/final generations. For unbounded results larger than
16384 cells per axis it supplies occupied display bins with an explicit stride;
these display bins never feed back into evolution. Native template saves and
compositions also focus the template on the research canvas. Preview delivery is
coalesced and generation-guarded so canceled workers cannot replace a later view.
The hidden 1x1 Harmony Web / offscreen iOS WKWebView only executes research code.

Native Agent reads, edits, summaries and placement return to the real canvas and
focus their region. Camera center and log-scale interpolate over 350 ms; manual
camera input can interrupt. Stop terminates research workers and restores the
original canvas and view, with no fabricated evolution animation.

Validation: both platform builds; HashLife's 480 reference comparisons and 100 RLE
codec cases; snapshot/checkpoint and cancellation regressions; native
`gol_agent_check` verifying original-buffer isolation, byte-identical saves during
preview, restoration, and smooth camera interruption. A factory-state iOS build
29 trial independently checked 64 blinkers through 1001 generations, then placed
192 cells with matching template/canvas hashes. Evidence is under
`out/agent/native-ios29/` and `out/agent/native-preview-*.log`.
The final iOS self-test also checks live research streaming and Stop restoration.
Deferred native preview creation rebinds the offline shader cache on each Agent
command: serial GCD queues can change OS threads between initialization and work.

Mate 70 build 109 ran a Python research-worker blinker through 30 live checkpoints
and returned population 3, then restored the main canvas. iOS build 30 streamed
the same native preview via JavaScript. Screenshots confirm that only live cells
are blue, with unchanged dead-cell/background colors; no Web panel is displayed.
Evidence: `out/agent/native-mate109/`, `out/agent/native-ios30/`.

### Agent-authored work briefs (1.0.3 build 110 / iOS build 31)

Every model tool schema requests a `work_brief`: one concrete sentence in the
user's language. It appears in the conversation and stays in the collapsed bar
until the Agent reports new work. Ordinary streamed replies also remain in chat;
reasoning and generic tool-phase labels no longer overwrite the current brief.
The expanded conversation shows messages rather than a tool activity timeline.
Stopping retains text already received during streaming.

The former 192-response and 768-tool-call caps are removed on both platforms.
Cancellation, request deadlines and per-request memory/context bounds remain.
A deterministic streaming regression completes 194 responses and 772 tool calls,
checks that a model brief survives tool execution, and verifies that both brief
and full response text remain visible in the message history.

Fresh OTCA/glider verification on 2026-11-17: physical Mate 70 build 110 completed
in 35 tool calls; iPhone 18 Pro simulator (iOS 27.0), build 31, completed in 55.
Both trials started from uninstall/install with only the same task prompt. No old
workspace, templates or construction adapter was injected. Both independently
fetched sources and implemented their own metafier/glife compatibility code.
Every tool call supplied a concrete brief, retained in the conversation alongside
ordinary assistant text. The simulator UI was checked expanded and collapsed.

Both produced byte-identical 12298×12298 arrays (36 OTCA metacells, pitch 2048),
with 1,072,443 cells and template/native-region hash `10639125039446826425`.
Initial live macro cells are (1,0), (2,1), (0,2), (1,2), (2,2): a southeast glider.
The phone left the array centered; the simulator used the upper-left world area.
Both fitted the complete array after placement. The phone explicitly confirmed
paused state through canvas_state. The simulator Agent reported paused but did
not read that state back; the later screenshot shows playback active, so no final
paused-state claim is made for the simulator. Its current UI is left untouched.

An independent bgolly HashLife run on the downloaded phone template decoded all
36 central displays after warm-up and 12 macro generations (459,264 ordinary
Life generations). Every state matches independent B3/S23, ending with the glider
translated (+3,+3) into the opposite corner. The downloaded simulator RLE has the
same SHA-256 (`2845c7c37ab5b2374336c1c699df0bf8f4f4a07fc7016fc7b3f222a37b1150b8`),
so this verification also covers its identical template. The simulator Agent itself
verified macro generations 0–4 and (+1,+1) displacement. The phone Agent verified
shorter runs and honestly reported its 60-second script timeout; the independent
check closes that gap. These long runs use research/independent HashLife, not a
claim of 459,264 native GPU generations on either device.

Evidence: `out/agent/otca-mate110/` and `out/agent/otca-ios31/`, including traces,
RLEs, screenshots, `independent-glider.json` and cross-platform byte comparison.
The iOS simulator remains open at the user's explicit request.
