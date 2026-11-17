# Agent 初步实现

此研究版本在中央画布下方提供原生助手入口，展开后显示对话区域；工作期间在对话框上方显示当前请求或工具操作进度。鸿蒙使用半透明背景与模糊，iOS 26 及以上入口使用 Liquid Glass，旧版本回退到系统材质。

## 使用与凭据

进入助手设置，输入 DeepSeek API Key 并同意将消息和必要的画布数据发送到 DeepSeek。请求固定使用官方 `https://api.deepseek.com/chat/completions` 和 `deepseek-flash`。iOS 将密钥保存到本机 Keychain；鸿蒙原型只在进程内存中保留密钥，重启后需要重新配置。

本地 `tools/*.key` 已排除在 Git 之外。调试配置可以把 `tools/deepseek-api.key` 通过 hdc 的应用私有目录传输机制放到应用的 `filesDir/deepseek-api.key`；仅 debug 应用会读取并删除该文件。iOS 模拟器使用 Documents 下同名文件，读入内存后删除，同时尝试存入 Keychain。不得将密钥加入 rawfile、应用 Bundle、资源生成脚本或日志。

鸿蒙测试包使用已登记设备的 debug 签名以及本机托管隐私元数据。此配置不改变正式发行签名配置，不提交商店审核。商店安装版与调试版签名不一致时，不能强行覆盖或自动卸载；应先保留用户存档并取得卸载授权。

## 已提供的工具

协议见 `mobile/agent-tools.json`。鸿蒙 rawfile 中保存相同协议文件。

- `canvas_state`：运行状态、速度档位、边界模式、缩放及画布尺寸。
- `canvas_control`：开始／暂停、速度档位、边界模式、视野中心与缩放、精确推进 1–64 代、请求重置／随机化。
- `canvas_read`：读取不超过 64 × 64 的区域，返回活细胞坐标。
- `canvas_edit`：批量设置不超过 4096 格的活／死状态，暂停后执行。
- `pattern_search`：查询内置图案库。
- `pattern_place`：在指定位置放置内置结构，最多覆盖 4096 格的矩形范围。
- `knowledge_sources`：返回 Life Lexicon、LifeWiki、Golly 与 metapixel 参考链接；尚不在线抓取正文。

坐标原点在画布中心，x 向右、y 向下，合法范围为 [-8192,8191]。重置和随机化沿用原生确认流程，工具返回等待确认而不是假称操作已经完成。

共享工具校验位于 `mobile/AgentTools.h`；两端命令分别进入 iOS 的 LongMarchRenderQueue 和鸿蒙 Host 命令队列，再调用 LongMarch `demo/gol` 的语义控制接口。没有给 RHI 添加 Agent 专用抽象。

## 调度与限制

单轮最多 10 次模型请求、24 次工具调用；每次请求最多 4096 输出 token，并限制对话请求体大小。iOS 使用 SSE 接收工具调用增量，完整解析后执行；鸿蒙当前使用普通请求，并在请求和各工具阶段更新进度。两端均按官方协议回传工具调用记录及 reasoning_content。

停止或进入后台会取消后续网络与工具轮次；已经交给渲染线程的操作可能完成当前批次。网络失败不会自动重试写操作。用户撤回助手联网同意后不允许继续发送。

这是设计方案的第一步，尚不包含独立实验画布、编辑撤销、revision 冲突检测、完整图案文件管理、真实网页检索或通用 RLE 导入工具。区域读写目前使用完整 bit 缓冲读回，编辑后上传；这条原型路径保持稀疏块唤醒正确，但不适合高频逐格调用。正式版本应改成局部 GPU gather／批量 patch，并增加事务和冲突处理。

本原型的全局 reset／randomize 使用已有确认流程；界面所称“放置图案”会设置整个图案矩形内的状态，不仅写入活格。模型提示词要求先读后改并保留无关内容，但这不能代替后续的撤销与事务功能。

## 回归与设备测试入口

- `harmonyos/tests/agent.cjs`：模拟官方请求，检查工具链、reasoning 回传、取消、Key 与上下文隔离、联网同意及 HTTP 错误。
- `ios/tests/agent_check.cpp` / `gol_agent_check`：Metal 上验证无效参数、幂等编辑、振荡器准确推进与读取、取消重置。
- 保留现有鸿蒙隐私、自动启动和文件取消测试。
- 鸿蒙 debug 启动参数 `agentSmoke` 在成功导入调试密钥后可自动执行指定测试任务；通过设置界面输入密钥时仍须手动发送，避免输入未完成便启动请求。显式使用该参数时，工具结果与对话记录写入 filesDir/AgentSmoke.json，不记录 Key。
- iOS 模拟器环境变量 `LONGMARCH_AGENT_SMOKE` 提供相同测试入口，输出 Documents/AgentSmoke.json。

测试入口仅用于明确的本地调试启动，不应把这些诊断记录包含在商店素材或正式发布包中。真实设备验收需覆盖对话入口、键盘布局、动态进度、真实 API、工具结果、停止与后台行为。

## Mate 70 验收（2026-11-17）

版本 1.0.3，鸿蒙 versionCode=1000003、buildVersion=95；iOS 版本同步为 1.0.3，build 8。用户确认允许清除数据后卸载原商店包，安装登记设备签名的调试包。

Mate 70 / Maleoon 920 真机通过以下检查：

- 华为隐私确认后进入画布；入口在画布下方，展开原生半透明对话框，键盘出现时可输入。修复尺寸字段未参与 ArkUI 状态更新导致入口初始停在顶部的问题。
- 使用设置中的密码输入框配置密钥并同意联网后，真实调用 DeepSeek；显示请求轮次与工具操作进度。
- 查询、暂停、读取空区域、放置 glider、推进四代、再次读回：5 个活格整体平移 (-1,-1)，与 iOS / Metal 测试一致。
- 请求中点击停止、请求中切入后台均取消轮次；两次测试未新增工具调用。返回前台后对话仍可使用。

本地证据位于 out/agent/mate70-glider-proof.json、mate70-stop-proof.json、mate70-background-proof.json，以及 mate70-chat.png、mate70-progress.png。这些仅为本地调试材料，不纳入商店发布。调试文件导入在本次设备流程中未成功读取，真机验收采用手动密码输入，不能把文件导入列为已通过的检查。
