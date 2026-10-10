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

## 实机与尚未覆盖的部分

该应用的窗口、图标、手动连接和真实 HomePod 播放在此前测试中已验证；本次发布不修改应用音频和 UI 源码。当前使用中的工作版本和已安装 VB-Cable 均保留。

最终安装包仍有以下未验收项目；隔离模式成功不能代替正常安装验收：

- **正常安装与卸载**：实际当前用户目录安装、真实桌面/开始菜单快捷方式、Windows“已安装的应用”卸载入口、正常重复安装/升级和卸载清理，均未实机验证。
- **防火墙授权**：管理员 UAC 接受/拒绝、实际创建/移除规则、已安装路径/UDP/LocalSubnet 范围，以及拒绝授权后的手动放行流程，均未实机验证。
- **首次播放**：最终安装器安装出的程序独立启动、中文 UI、HomePod 发现/临时配对、播放器路由到 CABLE、仅 HomePod 输出和首次真实播放，均未进行整流程验收。此前工作版本播放成功不作为该包的首次播放结果。
- **干净系统与长期使用**：第二台干净 Windows 上的驱动安装/重启/播放、长时间连续播放、暂停恢复、掉网和不同网络环境仍待测试。

本次未中断正在运行的 HomePodTray，也未改动 VB-Cable、实际防火墙或用户音频设置。

这些检查不能证明所有网络环境兼容。首版定位为实验性发布；真实长时间稳定性、PIN 接收器、本地监听同步仍需后续验证。

## 预发布准备核对

README 显示、链接及最终 ZIP 的核对见 [v0.1.0 准备记录](docs/releases/v0.1.0-preparation.md)。拟用的公开发布正文见 [v0.1.0 预发布说明草稿](docs/releases/v0.1.0.md)。本次仅准备文档和校验文件，未创建 GitHub Release 或上传附件。
