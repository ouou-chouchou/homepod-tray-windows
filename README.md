# HomePodTray

Windows 中文窗口与托盘应用，将勾选应用的音频通过局域网 AirPlay 投射到 HomePod。QQ 音乐只是可选播放器之一，也可选择其他播放应用。

![项目图标](assets/homepod-anime-preview.png)

本项目独立开发，与 Apple、QQ 音乐、VB-Audio 没有官方关联。HomePod、AirPlay 等商标属于各自权利人。0.1.0 为实验版本：最终安装包已在当前 Windows 环境完成安装、授权、启动、播放和卸载验收；长期稳定性、不同网络和设备兼容性仍需持续测试。

## 安装与首次使用

支持 Windows 11 x64；按进程采集 API 要求 Windows build 20348 及以上，普通 Windows 10 22H2 build 19045 不支持该模式。Windows 10 用户请升级到支持的系统；虚拟声卡端点诊断模式未作为 Windows 10 完整兼容承诺。

从 [GitHub Releases](https://github.com/ouou-chouchou/homepod-tray-windows/releases) 下载 [HomePodTray-0.1.0-Windows-x64.zip](https://github.com/ouou-chouchou/homepod-tray-windows/releases/download/v0.1.0/HomePodTray-0.1.0-Windows-x64.zip)，并可下载 [SHA-256 校验文件](https://github.com/ouou-chouchou/homepod-tray-windows/releases/download/v0.1.0/HomePodTray-0.1.0-Windows-x64.zip.sha256)。本机安装与首次使用验收记录见 [验证记录](VALIDATION.md)。

解压完整 ZIP 后：

1. 运行 `01-HomePodTray-Setup.exe`，安装至当前用户的稳定程序目录。
2. 打开包内 VB-Cable 官方链接，自行下载、解压官方完整驱动包，以管理员身份运行其中的 x64 安装程序，再重启电脑。详细步骤见 [VB-Cable 安装说明](docs/INSTALL-VBCABLE.md)。
3. 用 iPhone/iPad 初始化 HomePod，确保电脑与 HomePod 在同一个局域网。
4. 在 Windows 音量混合器把需要投射的播放器输出设为 `CABLE Input`，保留其他应用的默认物理输出。
5. 启动 HomePodTray，搜索或填写 HomePod IPv4 地址（`<HomePod-IP>`），选择虚拟声卡、勾选播放应用。
6. 选择“仅 HomePod”，点击“应用设置”和“连接”，再开始播放音乐。

安装器可选请求管理员权限创建防火墙规则，拒绝也能完成安装。软件未做数字签名，Windows 可能显示未知发布者提示。卸载可从 Windows“已安装的应用”操作；保留用户音频设置和配置日志，VB-Cable 独立管理。

以上安装行为已在当前 Windows 环境完成实机验收，包括桌面和开始菜单快捷方式、已安装应用卸载入口、管理员授权接受与拒绝、首次启动、连接播放、断开重连和卸载清理。VB-Cable 未由本项目安装或卸载。

## 功能

- 默认只捕获勾选应用；空选等待，不自动切换全系统声音。
- 三种播放目标：仅 HomePod、HomePod + 电脑默认输出、仅电脑默认输出。所有目标以所选播放器已路由到 CABLE 为前提。
- HomePod 音量 0–100，实时调节，不重连；调整设备、应用或采集模式后点“应用设置”会重建链路。
- 手动连接/断开和重置音频链路；没有自动抢连。手机/iPad 接管前先断开。
- 关闭窗口隐藏至托盘，托盘菜单可以退出；可按需启用开机启动。
- 端点诊断模式捕获所有路由到虚拟声卡的音频，此时应用勾选名单不生效。
- 不更改系统默认设备、其他应用路由、EQ、响度均衡或驱动采样率。

配置与日志存放在 `%LOCALAPPDATA%/HomePodTray-Rebuild/`。日志可能含接收器地址，分享前应自行检查。公开源码与发布包不含个人配置或日志。普通终端命令不会影响投射；避免多个发送器同时连接同一 HomePod。

## 防火墙

HomePod 需要向发送端返回 timing/control UDP。配对成功但 stream SETUP 超时时，检查实际运行程序的入站规则。

安装器可创建仅匹配已安装 `HomePodTray.exe` 的 UDP 入站规则，远端限制为 `LocalSubnet`。该范围包括本地子网其他主机；它不会关闭防火墙或允许其他程序。卸载会请求管理员权限移除本安装器创建的规则；拒绝时残留规则需手动清理。源码编译版本需自行为其实际路径配置规则。

## 构建与测试

需要 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.20+、Git。Release 使用静态 MSVC runtime；Mbed TLS 固定提交由 CMake 从 GitHub 获取，没有云端服务或 API key 依赖。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 8 --target HomePodTray raop_core_tests raop_loop_tests audio_policy_tests
ctest --test-dir build -C Release --output-on-failure
```

产物为 `build/Release/HomePodTray.exe`。测试使用假接收器与本机回环，不连接真实 HomePod，不播放测试音。

## 复现发布包

构建并测试成功后运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/build-release.ps1
```

脚本使用 Windows 自带 IExpress，输出同名发布文件夹与 ZIP，均被 Git 忽略。包含安装器、中文快速开始、官方驱动下载链接和第三方完整许可文本。安装器不携带 VB-Cable，不自动播放或连接 HomePod。其安装/卸载测试方式见 [验证记录](VALIDATION.md)。

## 音频与兼容边界

WASAPI 采集支持 44100/48000 Hz、单/双声道、float32 或 PCM 16/24/32，内部保持浮点重采样/混合，最终输出 44100 Hz / 16-bit / 双声道。使用上游 AirPlay 2 实时 RAOP 与加密 ALAC，通过局域网传输。

音频预缓冲目标 200 ms，欠载重新预缓冲；缺音边缘淡出/恢复淡入，时钟补偿限 ±2000 ppm。日志区分活跃欠载、空闲、预缓冲、UDP 发送错误和重传。

本地与 HomePod 输出未对齐延迟。多应用精确时间戳对齐、任意多物理输出、完整 PIN/密码配对、IPv6/跨网段发现、设备切换自动恢复、自动更新尚未实现。主要面向无需 PIN 的 HomePod 临时配对。

## 来源与许可

新应用代码使用 Apache-2.0，见 [LICENSE](LICENSE)、[第三方来源](THIRD_PARTY.md) 和 [NOTICE](NOTICE)。协议代码以 [akustikrausch/airplay2-sender-cpp](https://github.com/akustikrausch/airplay2-sender-cpp) 提交 `fc913509039412790aa2427558d35178aa7842ea` 为基础，并保留其来源和许可。

本地协议改动包括 transient Events 密钥派生、实时预缓冲/暂停恢复、缺音淡出、UDP 发送结果统计和对应假接收器测试。仓库直接包含修改后的 vendor 源码，无需初始化 Git 子模块。

二次元图标为 AI 生成原图，本地转换为 ICO；原图、提示词与转换脚本保留于 assets。
