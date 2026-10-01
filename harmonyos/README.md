# Game of Life for HarmonyOS

The 生命游戏 / Game of Life app (`net.lazyjazz.gameoflife`): an ArkUI page over the
desktop Game of Life from LongMarch (`../LongMarch/demo/gol`), rendered with
LongMarch's Vulkan backend into an XComponent surface.

## Layout

| Path | Purpose |
|---|---|
| `AppScope/` | Bundle name, version, app name (en/zh) and the layered icon (`demo/gol/tools/app_icon.py`) |
| `entry/src/main/ets/entryability/EntryAbility.ets` | Fullscreen window, native initialization, foreground/background |
| `entry/src/main/ets/pages/Index.ets` | Game page: touch and pinch input, gravity-turned icons, grid-size popover, pattern library |
| `entry/src/main/resources/` | Strings (en/zh) and the bundled game resources (`rawfile/Resources`, generated) |
| `entry/src/main/cpp/` | Native build entry for DevEco; builds `native/` |
| `native/` | N-API bridge, render-thread host, Vulkan presentation and the hosted game session |
| `prepare_resources.py` | Stages the patterns and SPIR-V shaders into `rawfile/Resources` |
| `build_hap.sh` | Command-line HAP and `.app` builds |

## Build

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
- The interface keeps the orientation the game opened in; the gravity sensor turns
  the icons, and is used only while the page is visible.
- One finger edits cells or pans an enlarged grid; two fingers pan and pinch.
- Open and save start in the in-app pattern library (736 Life Lexicon patterns in
  folders, plus My Patterns); the system document picker is one link away.
- Built-in patterns are adapted from the Life Lexicon by Stephen A. Silver et al.,
  licensed under CC BY-SA 3.0 (`../LongMarch/demo/gol/patterns/LICENSE.md`).
