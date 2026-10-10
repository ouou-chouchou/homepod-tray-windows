# 构建与验证记录

验证日期：2026-10-08，Windows / MSVC 19.44 / Visual Studio 2022 Build Tools / x64 Release。

构建产物：`build/Release/HomePodTray.exe`，965120 字节，生成时间 12:52:48。

SHA-256：`F79F54DCC6078F746C569E405BA28FCAADAB4BE331B9BC30BDAAB282A1BD5949`。

最终 `ctest --test-dir build -C Release --output-on-failure`：3/3 通过，11.46 秒。包含策略模拟、协议假接收器（新增真实 sender 预缓冲/缺音恢复/暂停分类和 transient event 响应检查）、本机回环网络宿主检查。未访问 HomePod，未渲染到声卡。

PE 依赖检查：仅 Windows 自带的 ole32、MMDevAPI、SHELL32、ADVAPI32、COMCTL32、IPHLPAPI、bcrypt、WS2_32、KERNEL32、USER32；Release 使用静态 MSVC runtime，无单独 VC runtime DLL 依赖。

**未执行新版 EXE。** 因此这份验证不包含真实 UI 操作、HomePod 发现/配对/播放、音量、用户路由切换、长时间稳定性或听感。不能把编译和自动检查通过解释成原滋滋声已修复。新应用的能力边界与后续测试方法见 README.md。

原安装目录、原托盘源码/EXE、VB-Cable 驱动、音频设置、应用输出路由、桌面和启动项均未修改。本地依赖源码缓存仅用于构建，新的构建输出均在本目录。

## UI 版增量验证（13:05）

新增默认显示的主窗口；关闭隐藏到托盘，左键托盘/右键打开窗口以及重复启动可重新显示。原后台音频与协议实现未修改。

UI 版产物：`build-ui/Release/HomePodTray.exe`，989184 字节，生成时间 13:05:49。

SHA-256：`6F133E6B8EE6B0B0D216D16BCCD171392EC573CCCA6C3D52DC53253D307AB790`。

这次只修改 `app/tray.cpp`、README 与构建目录忽略规则，使用独立 `build-ui` 输出，不覆盖正在运行的旧候选。重新构建后策略、协议 core 和本机回环测试均通过。UI 交互尚未由本子代理运行；主线程负责部署与可见窗口检查。原托盘候选的 SHA-256 仍为上述 `F79F54...`。

在部分 Windows 网络配置中，HomePod 需要发送 timing/control UDP 回发送端。发布安装包使用仅限本地子网的程序级 UDP 入站规则；实际连接仍需用户根据自己的网络和 HomePod 状态验证。
