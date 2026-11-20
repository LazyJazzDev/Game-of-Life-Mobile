# 生命游戏 · Game of Life (mobile)

Conway's Game of Life for iPhone, iPad and HarmonyOS, with 736 patterns from the
Life Lexicon. This repository owns the simulation, controls, rendering, shaders,
Agent integration, patterns and mobile apps. Shared Game of Life code lives in
`game/`. [LongMarch](https://github.com/LazyJazzDev/LongMarch) is a Git submodule
providing general RHI/util APIs and mobile build support; application business
logic must be changed here, not in LongMarch demos.

| | iOS / iPadOS | HarmonyOS |
|---|---|---|
| Bundle | `net.lazyjazz.gameoflife` | `net.lazyjazz.gameoflife` |
| Store | App Store | AppGallery |
| Build | [ios/README.md](ios/README.md) | [harmonyos/README.md](harmonyos/README.md) |

## Layout

- `game/`: shared GoL computation, rendering, shaders, controls, patterns and licenses.
- `LongMarch/`: general RHI/util dependency and mobile build support (submodule, branch
  `mobile-bridge`). Clone with `git submodule update --init LongMarch`; the
  submodule's own submodules (LongMarch assets) are not needed.
- `ios/`: SwiftUI app, Metal host and resource preparation.
- `harmonyos/`: DevEco project with the ArkUI page and Vulkan host.
- `mobile/`: shared Agent tools/research, Vulkan shader preparation and host dependency configuration.
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
(`game/patterns/LICENSE.md`).
