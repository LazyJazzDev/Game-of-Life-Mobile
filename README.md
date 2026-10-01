# 生命游戏 · Game of Life (mobile)

Conway's Game of Life for iPhone, iPad and HarmonyOS, with 736 patterns from the
Life Lexicon. The game itself (simulation, controls, rendering and shaders) is
the Game of Life demo of [LongMarch](https://github.com/LazyJazzDev/LongMarch),
included as a Git submodule; this repository holds the mobile apps around it.

| | iOS / iPadOS | HarmonyOS |
|---|---|---|
| Bundle | `net.lazyjazz.gameoflife` | `net.lazyjazz.gameoflife` |
| Store | App Store | AppGallery |
| Build | [ios/README.md](ios/README.md) | [harmonyos/README.md](harmonyos/README.md) |

## Layout

- `LongMarch/`: the engine and the Game of Life demo (submodule, branch
  `mobile-app-base`). Clone with `git submodule update --init LongMarch`; the
  submodule's own submodules (LongMarch assets) are not needed.
- `ios/`: SwiftUI app, Metal host and resource preparation.
- `harmonyos/`: DevEco project with the ArkUI page and Vulkan host.
- `mobile/`: dependencies shared by both hosts (vcpkg manifest and CMake).
- `docs/`: GitHub Pages, including the [privacy policy](https://lazyjazzdev.github.io/Game-of-Life-Mobile/privacy/).
- `store/`: store listing text and the screenshot capture/compose scripts.
- `tools/app_icon.py`: renders the app icons for both platforms.

## Dependencies

Install the shared host dependencies once (Apple Silicon example):

```sh
/path/to/vcpkg/vcpkg install --x-manifest-root=mobile \
  --x-install-root=out/mobile/deps --triplet=arm64-osx
```

## Licenses

The app code is MIT licensed (`LICENSE`). The built-in patterns are adapted from
the Life Lexicon by Stephen A. Silver et al. and licensed under
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/)
(`LongMarch/demo/gol/patterns/LICENSE.md`).
