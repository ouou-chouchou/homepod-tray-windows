# HomePodTray

Windows 上将指定应用音频投射到 HomePod 的开源实验性工具。

本项目由社区独立开发，与 Apple、HomePod、QQ 音乐或 VB-Audio 没有官方关系。HomePod 是 Apple Inc. 的商标；QQ 音乐是其各自权利人的产品。

当前版本为 MVP，适合愿意自行安装 VB-Cable 并排查 Windows 防火墙的用户。它不承诺消除所有网络抖动、音频设备驱动问题或 HomePod 兼容性问题。

桌面、窗口和托盘图标现使用 `assets/homepod-anime.ico`，由本机 ai-media-gen 技能的 DSG / gpt-image-2 通道生成二次元音乐角色原图，再本地转换为 16～256 像素的多尺寸 ICO。原图 `assets/homepod-anime-ai.png`、提示词 `assets/anime-icon-prompt.txt` 和转换脚本均保留。错误状态采用同一图标加提示标记。普通启动仍手动连接；内部 `--resume-session` 参数仅在更新一个原本已连接的运行版本时使用，以恢复该连接。

这是一份独立的 Windows 中文窗口与托盘应用源码。项目已包含离线和本机回环测试；真实 HomePod 播放仍取决于局域网、Windows 音频路由、虚拟声卡和接收器状态。应用不会自动覆盖其他音频程序的输出设置。

## 对应需求与使用方式

运行后默认显示中文主窗口，同时有独立托盘图标，不会自动连接 HomePod，也不会默认捕获全系统声音。先勾选应用、选择发现到的 HomePod，或填写 IPv4 地址，再点击连接。QQ 音乐只是普通可选进程，程序没有硬编码 QQMusic。

主窗口集中显示接收器搜索/选择、手动 IP、虚拟声卡名称、应用勾选列表与当前 Windows 输出设备、播放目标、诊断采集方式、HomePod 音量，以及连接/断开/重置/查看日志。状态区显示清晰的连接状态、错误、ring 毫秒和实际生效 ppm。

设备、应用勾选、输出目标和采集方式的调整先留在界面，点击“应用设置”才保存；若正在运行且这些设置确实发生变化，只重建一次链路。首次点击“连接”会一并应用界面当前设置。HomePod 音量滑条直接实时生效，不等待应用按钮，也不重建链路。刷新应用保留尚未提交的勾选，空选仍不回退系统音频。

关闭主窗口或按 Escape 隐藏到托盘，不停止投射；左键托盘或右键菜单“打开主窗口”重新显示，重复启动快捷方式也会显示已经运行的窗口。退出仍通过托盘菜单执行。界面支持 Tab 导航、空格勾选应用、方向键调节音量。主窗口按系统 DPI 缩放。

**所有三种播放目标都以所选应用已显式路由到虚拟声卡为前提。** 在 Windows 音量混合器中把需要投射的应用设置为 `CABLE Input`；物理默认输出仍供其他程序使用。新版不修改默认设备、不修改应用路由、不静音共享扬声器、不改驱动采样率或 EQ。按应用模式会检查活跃所选应用的路由，发现它直接输出到其他设备时停止并显示错误，不假装已经屏蔽本地声音。

| 托盘选项 | 行为 |
| --- | --- |
| 仅 HomePod（默认） | 采集所选应用，经 AirPlay 发送；不创建电脑监听输出。CABLE 本身不连接物理扬声器，所以本地无声。 |
| HomePod + 电脑默认输出 | 同样投射，并把所选音频另送到当前默认物理输出；其他应用仍使用原路径。 |
| 仅电脑默认输出 | 不建立 AirPlay 连接，把原先路由到 CABLE 的所选音频送到当前默认物理输出。 |
| 只采集勾选应用（默认） | 用 Windows 进程树 loopback；空选只等待，不采集、不自动回退全系统。 |
| 采集虚拟声卡端点（诊断） | 用户确认后，采集所有被路由到指定 CABLE 的声音；此模式勾选名单不生效。它不会采集默认扬声器的所有声音。 |
| HomePod 音量 | 滑条 0–100，步进 1；网络 worker 发送实时音量命令，不重连。首次 Streaming 后延时再发一次防止 HomePod 初始状态覆盖。 |
| 断开 | 结束 AirPlay 会话和采集，保留设置；没有自动重连，方便手机/iPad接管。 |
| 重置音频链路 | 明确断开并重新连接、重新预缓冲；属于恢复操作，不能视为根因修复。 |
| 登录后启动 | 仅当用户点击时创建独立当前用户启动项 `HomePodTrayRebuild`；登录启动不自动连接。 |
| 新版桌面快捷方式 | 用户点击后创建 `HomePod 投射（新版）.lnk`，不覆盖旧版快捷方式。 |

Windows/播放器自身的源音量可照常改变，新版不会因它们改变而重启采集或 AirPlay。此 MVP 未额外提供单独的逐应用投射增益滑条。改变勾选、采集方式、输出目标或接收器会显式重建链路；改变 HomePod 音量不重建。

新版配置和日志独立存放在 `%LOCALAPPDATA%/HomePodTray-Rebuild/`，使用 UTF-8 文本和包含日期、毫秒的日志。只有用户运行新版后才创建这些文件。终端运行普通命令不受影响。

## 实现与音频边界

- `app/tray.cpp`：新写的中文 Win32 托盘、配置、连续音量滑条、输出目标、显式连接意图、诊断按钮、快捷方式与开机启动。
- `app/audio.cpp`：新写的 WASAPI 进程/端点采集、格式解析、97 taps / 1024 phases 窗口 sinc 重采样、混合、本地监听和单线程网络调度。
- `app/policy.h`：没有积分项的有界时钟补偿和预缓冲状态策略；实际使用的补偿范围与日志统一为 ±2000 ppm，闲置时恢复 1.0，不积累误差。
- `app/discovery.cpp`：新写的 IPv4 mDNS 搜索；只有用户点击才搜索，不扫描网段。报文解析检查边界并限制压缩指针跳数。搜索不成功时支持手动 IP。
- `vendor/airplay2-sender-cpp`：从公开 GitHub 克隆的协议、加密、网络宿主和测试代码，未复制旧托盘/旧采集引擎。

输入限定 44100 或 48000 Hz、单声道或立体声、float32 或 PCM 16/24/32。单声道复制为左右声道，立体声保留；其他格式明确报错，避免悄悄丢掉多声道内容。重采样和混合保持 float，最后量化成 AirPlay 的 44100 Hz / 16-bit / 双声道。网络采用上游 AirPlay 2 实时 RAOP、加密 ALAC（其实现使用无压缩 escape frames），走局域网 IP；没有蓝牙编码/无线蓝牙连接功能。

采集和网络状态机由同一个 worker 驱动，避免跨线程访问 sender 状态。每个输入有界队列，混合前确认 ring 有完整块的空间才消费输入；遇持续积压明确停止报错，不能静默丢帧。多应用混合会等待近期活跃源的下一包，避免因 WASAPI 到包时间差补出假的静音碎片；仍未实现多个进程按 QPC 绝对时间戳精确对齐。

sender 等至少 200 ms 音频才消费，活跃欠载后重新预缓冲而不重连；暂停/无包时清除旧音频，并把空闲包、预缓冲包和活跃期缺音分别计数。部分缺音和整包缺音均淡出到零，恢复时淡入。已有源恢复时需要重新积累预缓冲；这是可观测的恢复机制，不保证上游设备供帧不足时完全没有听感间断。

日志每 10 秒记录实际生效 ppm、ring 毫秒、活跃期欠载/补零帧、空闲包、预缓冲包、WASAPI discontinuity、UDP 实际发送错误与重传请求/过期计数。采集保留 device position / QPC，但尚未用它们计算实时帧率。活跃定义是近期收到采集包，**它不等同于人耳有音乐**；播放器暂停后若 Windows 持续送静音包，日志仍显示收包活跃。

## 上游归属与本地协议改动

参考项目：[akustikrausch/airplay2-sender-cpp](https://github.com/akustikrausch/airplay2-sender-cpp)，克隆提交 `fc913509039412790aa2427558d35178aa7842ea`。它使用 Apache-2.0，携带 Mbed TLS（Apache-2.0）、orlp/ed25519（zlib）和各项 NOTICE/来源说明。新的 `app/` 和 `tests/audio_policy_tests.cpp` 也使用 Apache-2.0；见根目录 LICENSE 与 THIRD_PARTY.md。

vendor 内的本地改动仅包括：

1. HomePod transient pairing 也派生 Events 的加密/解密密钥，以响应事件通道 keep-alive。
2. 主机显式开启的实时采集预缓冲/暂停/恢复门控，缺音边缘处理，以及区分空闲/活跃的统计。
3. UDP `sendto` 的返回结果检查和错误计数。
4. 对真实 sender 预缓冲、整包缺音恢复、暂停分类和 transient event 响应的 fake-receiver 测试。

没有复制旧 PI 控制器，也没有复制旧程序的调试探针、进度元数据定时器或托盘状态管理。事件密钥补丁沿用已验证的协议修复思路，但这份新应用的真实 HomePod 长时间 keep-alive 尚未验证。

## 构建与验证

Windows 11 / 支持进程 loopback 的 Windows 10 build 20348 及以后，Visual Studio 2022 Build Tools，CMake 3.20+。依赖 Mbed TLS 的提交由上游 CMake 固定，不需要服务 key 或付费组件。Release 使用静态 MSVC runtime。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 8 --target HomePodTray raop_core_tests raop_loop_tests audio_policy_tests
ctest --test-dir build -C Release --output-on-failure
```

普通构建命令会自行获取 CMake 固定版本的依赖；不要把本机 CMake 缓存或构建目录提交到 Git。

开发过程中可以使用独立构建目录，避免覆盖正在运行的程序；构建目录均已列入 `.gitignore`。

协议 core 测试使用进程内假接收器；loop 测试只使用本机回环假服务器。策略测试模拟长期积压/欠载与暂停恢复；集成测试验证真实 sender 保留不足预缓冲、缺音整包淡出、暂停分类、transient encrypted event 200。所有测试不访问 HomePod，不播放测试音。

## 首次连接与 Windows 防火墙

AirPlay 在握手阶段需要 HomePod 主动发往本机 timing/control UDP 端口。允许旧 EXE 的规则不会自动适用于另一条路径的新 EXE；如果 TCP 配对和 RECORD 均成功，但 stream SETUP 等待超时，应先检查新 EXE 的 UDP 入站规则，不能据此修改配对顺序或音频采样率。

在部分 Windows 网络配置中，HomePod 需要主动向发送端发回 timing/control UDP。若 TCP 配对和 RECORD 成功但 stream SETUP 超时，应检查发送端程序的入站 UDP 防火墙规则。

程序不关闭 Windows 防火墙，也不扫描网段。安装包可以创建一条仅针对 HomePodTray、远端限制为本地子网的 UDP 入站规则；如果用户拒绝，需要按实际安装路径手动放行程序。

## 尚未验证 / 未实现

- 当前 UI 版窗口布局、交互和键盘操作尚待实际运行检查；早期托盘候选进入过真实 HomePod Streaming，但当前 UI 版连续调音量、暂停恢复、长时间稳定性和本地监听听感仍待验证。
- 本地监听与 HomePod 固有约 1.5 秒延迟不做同步；同时播放会有延迟差。
- 多个应用精确同步、超过 8 个进程树、设备自动切换恢复、任意多声道/采样率未实现。
- 支持 HomePod transient 配对；要求 PIN、密码或持久凭据的接收器流程尚未做完整 UI。
- 应用路由检查基于音频会话和所选 exe 名字；子进程单独路由、应用途中换设备等仍需要测试，不自动强制系统路由。
- 本地输出目前为 Windows 默认物理设备，不提供任意多物理设备勾选。
- 搜索为 IPv4 mDNS；跨网段发现、IPv6、网段扫描未实现。
- 无自动更新、打包安装程序；文件可移动到稳定目录后启动，并重新生成快捷方式。

安装 VB-Cable 的步骤见 [`docs/INSTALL-VBCABLE.md`](docs/INSTALL-VBCABLE.md)。许可证和第三方来源见 [`LICENSE`](LICENSE) 与 [`THIRD_PARTY.md`](THIRD_PARTY.md)。
