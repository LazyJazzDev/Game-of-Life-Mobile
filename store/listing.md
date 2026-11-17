# 生命游戏 / Game of Life — 商店介绍

## 中文

**应用名称**：生命游戏

**一句话简介**（AGC 应用简介；App Store 副标题取前半句）：
经典康威生命游戏，内置 736 种著名图案，观察简单规则如何演化出复杂世界。

**应用介绍**：

生命游戏是英国数学家约翰·康威在 1970 年提出的细胞自动机：每个细胞只按照身边八个邻居的数量决定生死，却能演化出静止的方块、周期闪烁的振荡器、在网格中穿行的飞船，甚至不断发射滑翔机的“枪”。它是理解复杂系统、涌现现象和计算理论的经典入门。

【内置图案库】
- 收录 Life Lexicon 中 736 种著名图案，按静物、振荡器、飞船、枪、繁殖器与耙、长寿图案和其他结构分类整理
- 展示图案尺寸及可用的周期、速度信息，支持按名称搜索
- 滑翔机、Gosper 滑翔机枪、各类飞船，一键载入即可观察

【自由创作】
- 轻点网格即可放置或移除细胞
- 16384 × 16384 大画布，容纳更丰富的图案与演化
- 一键随机生成，或一键清空重来
- 双指缩放、拖动平移；坐标尺以画布中心为原点，帮助定位细节

【演化控制】
- 四档速度：每秒 2、4、10 代，以及每秒 60 代的闪电模式
- 两种边界：环绕边界（从一侧离开会从另一侧回来），或固定边界（网格外视为空白）

【保存与分享】
- 把自己的图案保存到应用内的「我的图案」
- 可导入传统 .cells 文本图案；完整画布以稀疏索引和压缩格式保存，并兼容旧版完整画布存档

【安心使用】
- 无广告、无需注册，核心模拟可离线使用；系统文件选择器仅访问你选定的文件
- 适合所有年龄，可用于课堂演示与自主探索

内置图案改编自 Stephen A. Silver 等编写的 Life Lexicon，以 CC BY-SA 3.0 授权（https://creativecommons.org/licenses/by-sa/3.0/）。

**关键词**（App Store，100 字符以内）：
生命游戏,康威,细胞自动机,元胞自动机,滑翔机,数学,编程,科普,复杂系统,模拟

**新版本说明**：
新增 16384 × 16384 大画布，支持缩放、平移和中心坐标尺。改进大网格演化与显示，新增稀疏压缩存档并兼容旧存档；优化平板与折叠屏布局及屏幕切换。

## English

**App name**: Game of Life: Playground (App Store; the Chinese listing is 生命游戏 - Game of Life)

**Subtitle** (App Store, 30 characters max):
Conway's cellular automaton

**Short description** (AppGallery brief / App Store promotional text):
Classic Conway's Game of Life with 736 famous patterns built in. Watch simple rules grow into complex worlds.

**Description**:

The Game of Life is the cellular automaton devised by mathematician John Conway in 1970. Each cell lives or dies based only on how many of its eight neighbors are alive, yet these simple rules produce still lifes, blinking oscillators, spaceships that travel across the grid, and even guns that fire an endless stream of gliders. It is a classic introduction to complex systems, emergence and the theory of computation.

BUILT-IN PATTERN LIBRARY
- 736 famous patterns from the Life Lexicon, organized into still lifes, oscillators, spaceships, guns, puffers and rakes, methuselahs, and other structures
- Pattern dimensions and available period and speed information, with search by name
- Load a glider, the Gosper glider gun or a spaceship with one tap

CREATE YOUR OWN
- Tap the grid to add or remove cells
- Explore a 16,384 × 16,384 canvas with room for larger patterns
- Fill the grid at random, or clear it and start over
- Pinch to zoom and drag to pan; centered coordinate rulers help you find your way

CONTROL THE EVOLUTION
- Four speeds: 2, 4 and 10 generations per second, plus a 60-generations-per-second lightning mode
- Two boundaries: wrapping edges, where cells leaving one side return on the other, or fixed edges with empty space beyond

SAVE AND SHARE
- Keep your creations in My Patterns inside the app
- Import traditional .cells text patterns. Save complete worlds using a sparse, compressed format, with support for older full-world saves

PRIVATE BY DESIGN
- No ads or account required. The core simulation works offline; the system file picker accesses only the files you choose
- Suitable for all ages, in the classroom or on your own

Built-in patterns are adapted from the Life Lexicon by Stephen A. Silver et al. and licensed under CC BY-SA 3.0 (https://creativecommons.org/licenses/by-sa/3.0/).

**Keywords** (App Store, 100 characters max):
game of life,conway,cellular automaton,glider,life lexicon,math,simulation,emergence,science

**What's new**:
Explore a new 16,384 × 16,384 canvas with zoom, pan and centered coordinate rulers. This update improves large-world simulation and rendering, adds sparse compressed saves with legacy-save support, and refines tablet and foldable layouts and display transitions.

## HarmonyOS privacy wording

The updated HarmonyOS build removes gravity sensor access and keeps button icons
fixed. Use this wording only with the sensor-free package:

- 无广告、无需注册，游戏离线运行；不读取重力传感器信息。
- No ads or account; the game runs offline and does not read gravity sensor information.

Remove the gravity sensor privacy label when replacing the previously submitted
package. Earlier sensor-reading packages must not be resubmitted with this label.
