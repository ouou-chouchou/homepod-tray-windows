# 0.1.0 验证记录

验证日期：2026-10-10。Windows x64、Visual Studio 2022 Build Tools / MSVC 19.44、Windows SDK、CMake。

## 构建和自动测试

从已提交公开仓库重新克隆到空目录并独立配置，从 GitHub 获取固定提交的 Mbed TLS，无旧构建缓存参与编译。Release 编译成功，三个 CTest 项目全部通过（audio_policy_tests、raop_core_tests、raop_loop_tests）；最终整套运行约 11.98 秒。

重新构建的 HomePodTray.exe 为 1310720 字节，SHA256：

`3A5638DCBD8A302A54F351409A0CCDC175ED59ED9170E6F7ED727624779D0D8C`

测试使用假接收器和本机回环，不连接真实 HomePod，不播放测试音。部分上游编译警告仍存在，不影响本次构建结果。

## 安装包

IExpress 安装包使用以下命令进行 Windows PowerShell 5.1 测试：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File packaging/test-package.ps1 -InstallerPath "./HomePodTray-0.1.0-Windows-x64/01-HomePodTray-Setup.exe"
```

已验证：完整解包、预期载荷文件清单、脚本解析、临时目录安装、两个快捷方式目标、卸载注册表项、许可文本安装、重复安装、拒绝 TEMP 外测试路径、移除测试安装和测试快捷方式/注册表入口。

测试模式所有文件操作限定为新建的临时目录，注册表限定测试键，不修改真实桌面/开始菜单，不添加防火墙规则，不启动播放器、不连接 HomePod。

## 实机验收

用户已按当前 HomePodTray 窗口完成最终安装包的实机验收。本次验收通过：

- 使用最终 ZIP 中的安装器完成当前用户安装；桌面快捷方式、开始菜单快捷方式和 Windows“已安装的应用”卸载入口均正常。
- 分别测试防火墙授权允许和拒绝；允许时安装完成并创建对应 UDP 入站规则，拒绝时安装仍完成；没有关闭系统防火墙，也没有删除其他程序的规则。
- 安装包没有包含或安装 VB-Cable；已有 `CABLE Input`、`CABLE Output` 和 VB-Audio 驱动保持不变。
- 从桌面快捷方式启动后中文界面正常；按 `CABLE Input`、指定播放器、仅 HomePod、只采集勾选应用完成配置。
- 首次连接播放、未勾选应用隔离、其他应用原输出、HomePod 音量、断开、重新连接及必要时的音频链路恢复均通过。
- 从“已安装的应用”卸载后，程序文件、快捷方式、卸载入口和安装器创建的防火墙规则清理；VB-Cable、用户配置和日志保留。

本次验收针对当前 Windows 环境；第二台干净系统、长时间运行、掉网恢复和不同网络环境仍属于后续兼容性测试范围。

## 预发布准备核对

README 显示、链接及最终 ZIP 的核对见 [v0.1.0 发布记录](docs/releases/v0.1.0-preparation.md)。完整功能和安装说明见 [v0.1.0 发布说明](docs/releases/v0.1.0.md)。

## 后续修复：引擎主循环受系统定时器精度限制（2026-11-16）

在另一台干净的 Windows 11（build 26200）上安装 v0.1.0 发布包后复现：连接握手全部成功，能听到约 3 秒声音且明显卡顿，随后固定报“采集队列超过 1 秒：已停止，避免静默丢帧”并断开。换 48 kHz 与 44.1 kHz 采集格式、改回重采样与直通，断开时间都是 3.0 秒，与采样率无关。

定位：引擎线程用 `poll/WSAPoll` 的 2 ms 超时驱动主循环，但工程内未调用 `timeBeginPeriod`。Windows 默认时钟节拍为 15.6 ms，实测 2 ms 套接字等待实际耗时 15.56 ms，主循环只有约 64 Hz；每次循环最多搬运 441 帧（10 ms 音频），即 28.2k 帧/秒，低于采集端的 44.1k 帧/秒，队列以约 15.9k 帧/秒累积，约 2.8 秒达到 `frames.size() > 44100` 的阈值——与实测每次 3.0 秒断开吻合。卡顿同源：只有约 64% 的音频被送出。

修复：引擎线程启动时调用 `timeBeginPeriod(1)`、结束时调用 `timeEndPeriod(1)`，并把每次搬运上限由 441 帧放宽到 4410 帧，作为循环被拖慢时的第二层保险。同进程实测 `select(2 ms)` 由 15.56 ms 降到 2.42 ms。

需要强调的是：Windows 10 2004 之后 `timeBeginPeriod` 只对调用进程生效，外部进程无法代为提升，因此该修复必须落在程序自身。v0.1.0 的验收环境当时存在其他提升定时器精度的进程，掩盖了该问题；本次是在没有此类进程的干净系统上复现的。

修复后重新构建：MSVC 14.44.35207、Windows SDK 10.0.26100.0、Ninja，CTest 3/3 通过（audio_policy_tests 0.03 s、raop_core_tests 0.42 s、raop_loop_tests 9.15 s）。产物为 1311232 字节，SHA256：

`B8354C0774BE31DE34CAA6F926EED654B18BD0B16F0704B2DBB85F36485BBFC5`

在本机连续播放期间缓冲稳定在预缓冲目标 200 ms 附近，未再出现队列超限。仍待补充：另一台干净系统的完整安装包验收，以及长时间运行、掉网恢复测试。
