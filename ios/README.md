# Game of Life for iOS and iPadOS

The SwiftUI app hosts the desktop Game of Life UI from the LongMarch submodule
(`LongMarch/demo/gol`) in a Metal view, on the LongMarch RHI and its
Metal backend. Bundle `net.lazyjazz.gameoflife`, version 1.0.2 (build 5), iOS 18+,
iPhone and iPad.

- Conventional iPhone locks the interface to the orientation the game opened in; gravity turns
  only the button icons. iPad rotates freely and the controls follow the window's
  short edges.
- Notch and Dynamic Island safe areas, the home-indicator strip and an iPad size
  cap for the controls are passed to the game layout.
- The grid-size popover points at the rendered slider (on iPad it closes when the
  device turns). Open and save start in the in-app pattern library: the 736
  built-in Life Lexicon patterns, the user's saved patterns, and the system files.
- Frames are rendered only on demand: while the game is paused and idle, nothing is
  scheduled.

Foldable iPhones (including Duo) use UIKit's `UIHingeInteraction` capability
on iOS 27.1 and later, follow system orientation, and retain the same game
session when the scene migrates between inner and outer displays. Layout follows
the current viewport aspect ratio; controls stay at 40 points, matching Harmony's
40 vp policy, with the current screen scale applied to Metal pixels. Resizing or
switching screens cancels in-flight drawing and two-finger pan gestures.
Duo supports all four system orientations. UIKit's active occlusion regions
(including their interaction margins) move only intersecting buttons along their
rail; a corner camera does not shift the whole toolbar or reduce the board.
The cover's portrait randomize action therefore moves left of the camera; in
landscape the affected sidebar action moves vertically. The home gesture strip
remains reserved, and the normal 20-point edge spacing is shared across displays.
Device Hub exposes Closed, Partially Open, Open and Rotate Right controls for
checking the physical mask as well as the framebuffer.

For simulator regression, launch with `LONGMARCH_SMOKE_LAYOUT=1`, rotate and
switch displays, then inspect `Documents/LayoutSmoke.json` for viewport, density
and renderer continuity. `LONGMARCH_SMOKE_ORIENTATION=right` tests programmatic
rotation separately; some Duo windowing modes reject programmatic orientation
requests even though system-driven rotation works.

## Layout

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | Engine, game UI, host library, host checks and the app target |
| `gol/` | App entry (`GameOfLifeApp.swift`), Info.plist, Liquid Glass icon, localized app name |
| `games/GamesView.swift` | Game page: Metal view, input, orientation, popover and pattern library |
| `games/DesktopGameSession.*` | The desktop `GameOfLife` hosted without a GLFW window |
| `demos/DemoBridge.*` | `DemoRenderer`: frame scheduling, presentation and input on a serial render queue |
| `demos/DemoSession.*` | Shader-cache configuration around the hosted game |
| `demos/check.cpp` | `gol_check`: renders one frame on macOS (prepares or replays shaders) |
| `tests/games_check.cpp` | `gol_games_check`: input, animation, files, size controls and idle checks |
| `prepare_resources.py` | Builds the `GameResources` folder (patterns and shader cache) |
| `embed_shaders.py`, `finish_bundle.py` | Build helpers (embedded Slang, Ninja bundle Info.plist) |

## Dependencies

Install the shared mobile dependencies once (the manifest is `../mobile/vcpkg.json`), for example:

```sh
/path/to/vcpkg/vcpkg install --x-manifest-root=mobile \
  --x-install-root=out/mobile/deps --triplet=arm64-osx
```

Pass `-DLONGMARCH_MOBILE_DEPS=/path/to/deps` (or set the environment variable)
for another installation. CMake never downloads dependencies.

## Resources

The app reads precompiled shaders: Metal shaders cannot be compiled from Slang on
the device. A prepare build (links Slang and SPIRV-Cross) compiles the game's
shaders; a replay build, which like the app has no shader compiler, then extracts
exactly the entries the game reads, together with the pattern library.

```sh
cmake -S ios -B out/ios-prepare -G Ninja -DCMAKE_BUILD_TYPE=Release -DGOL_PREPARE=ON -DGOL_APP=OFF
cmake --build out/ios-prepare --target gol_check
cmake -S ios -B out/ios-replay -G Ninja -DCMAKE_BUILD_TYPE=Release -DGOL_APP=OFF
cmake --build out/ios-replay --target gol_check gol_games_check
python3 ios/prepare_resources.py --prepare out/ios-prepare/gol_check \
  --checker out/ios-replay/gol_check --output out/ios/GameResources
out/ios-replay/gol_games_check out/ios/GameResources
```

The output refuses to overwrite an existing folder. Regenerate it after updating
the LongMarch submodule or changing any game shader: the cache keys include the
shader sources. Both SPIR-V and Metal entries are kept, so the same folder is the
source of the HarmonyOS bundle.

## Simulator

```sh
cmake -S ios -B out/ios-sim -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 -DGAME_OF_LIFE_RESOURCES=$PWD/out/ios/GameResources
cmake --build out/ios-sim --target GameOfLife
xcrun simctl install booted out/ios-sim/GameOfLife.app
xcrun simctl launch booted net.lazyjazz.gameoflife
```

## Device and App Store

The Xcode generator signs automatically with the given team:

```sh
cmake -S ios -B out/ios-xcode -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<team> -DGAME_OF_LIFE_RESOURCES=$PWD/out/ios/GameResources
xcodebuild -project out/ios-xcode/GameOfLife.xcodeproj -scheme GameOfLife \
  -configuration Release -destination id=<device> -allowProvisioningUpdates build
```

For the App Store, a team without registered devices cannot create a development
profile, so archive unsigned and let the export sign for distribution and upload
(`ExportOptions.plist`: `method` `app-store-connect`, `destination` `upload`,
`signingStyle` `automatic`, `teamID` `<team>`):

```sh
xcodebuild -project out/ios-xcode/GameOfLife.xcodeproj -scheme GameOfLife -configuration Release \
  -destination generic/platform=iOS -archivePath out/ios-archive/GameOfLife.xcarchive \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" archive
xcodebuild -exportArchive -archivePath out/ios-archive/GameOfLife.xcarchive \
  -exportOptionsPlist ExportOptions.plist -exportPath out/ios-export -allowProvisioningUpdates
```

Raise `CFBundleVersion` in `gol/Info.plist` for every upload.

## Smoke hooks

Launch environment variables (`SIMCTL_CHILD_<name>` with `xcrun simctl launch`)
drive the app without touch input. Results are written to the app's Documents.

| Variable | Effect |
| --- | --- |
| `LONGMARCH_SMOKE_ORIENTATION=left\|right\|portrait\|exit` | iPhone: supplies a device direction and checks that a rotation request is rejected (`OrientationSmoke.json`); `exit` also checks the lock is released (`OrientationExitSmoke.json`) |
| `LONGMARCH_SMOKE_IDLE=1` | Checks that an idle game schedules no frames and that a tap wakes it (`IdleSmoke0.json`, `IdleSmoke1.json`) |
| `LONGMARCH_SMOKE_AUTORUN=1` | Frame counts over two input-free seconds (`AutorunSmoke.json`) |
| `LONGMARCH_SMOKE_GOL_SIZE=<n>`, `LONGMARCH_SMOKE_BENCHMARK=1` | A random running n × n grid; 60 frames after 10 warm-up frames (`BenchmarkResult.json`) |
| `LONGMARCH_SMOKE_DEMO=gol` | Frame and device report after three frames (`DemoSmokeResult.json`) |
| `LONGMARCH_SMOKE_FILE=open\|save`, `LONGMARCH_SMOKE_FOLDER=<category>` | Opens the pattern library, optionally in a category such as `gun` |
| `LONGMARCH_SMOKE_SIZE_PICKER=width\|height` | Taps a portrait size readout to open its popover |
| `LONGMARCH_SMOKE_TAPS="x,y;x,y"` | Taps points (fractions of the view) one second apart |
| `LONGMARCH_SMOKE_FILE_DELAY=<seconds>` | Delays opening the library so a pattern can be loaded first; defaults to one second |
| `LONGMARCH_SMOKE_ZOOM=<scale>` | Simulator-only: sends a centered pinch after three seconds for repeatable screenshot framing |
| `LONGMARCH_SMOKE_PATTERN=<name>` | The open button loads that built-in pattern, such as `Gosper glider gun` |

The iOS shader bundle uses Tier 1 argument buffers so it also runs in the iOS
simulator. The native Metal command queue presents the RHI image after rendering
on the same queue. Shader preparation includes preprocessed source, SPIR-V and MSL;
regenerate `GameResources` whenever the LongMarch shaders or pipeline layouts change.

### Deterministic store capture

Set `LONGMARCH_CAPTURE_PATTERN` to an exact built-in library name (for example
`pulsar`), `LONGMARCH_CAPTURE_ZOOM` to a positive pinch multiplier (up to 10),
and optionally `LONGMARCH_CAPTURE_PANEL=library` or `save`. The host waits for
its first rendered frame, imports through the normal file workflow, then adjusts
the camera and opens the requested panel. The board stays paused. No coordinate
taps or fixed startup delays are required. With simctl, prefix these environment
variables with `SIMCTL_CHILD_`; select the locale with `-AppleLanguages`.
Do not combine these parameters with the older timed `LONGMARCH_SMOKE_*` hooks.
