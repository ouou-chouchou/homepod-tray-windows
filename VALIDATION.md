# 0.1.0 验证记录

验证日期：2026-10-10。Windows x64、Visual Studio 2022 Build Tools / MSVC 19.44、Windows SDK、CMake。

## 构建和自动测试

公开源码副本独立配置，从 GitHub 获取固定提交的 Mbed TLS，无旧构建缓存参与编译。Release 编译成功，三个 CTest 项目全部通过（audio_policy_tests、raop_core_tests、raop_loop_tests）；最终整套运行约 9.76 秒。

重新构建的 HomePodTray.exe 为 1310720 字节，SHA256：

`B0788FBE7443EDC94D50EA6B81A8F3F8BFFC53942CA6A0613F109F0BF6006AB7`

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

本次未对真实安装的管理员 UAC/防火墙授权做自动交互测试，也未在第二台干净 Windows 机器做端到端首次播放验收。未中断正在运行的 HomePodTray 来启动另一个同互斥锁实例。

这些检查不能证明所有网络环境兼容，或杂音/卡顿已经根治。首版定位为实验性发布；真实长时间稳定性、PIN 接收器、本地监听同步仍需后续验证。
