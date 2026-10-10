# VB-Cable 安装说明

本项目不重新分发 VB-Cable 驱动。请从 [VB-Audio 官方页面](https://vb-audio.com/Cable/) 下载完整压缩包，解压后以管理员身份运行 x64 安装程序，按官方提示安装并重启电脑。不要只从解压包中单独取出 Setup.exe 运行，安装依赖包中的其他驱动文件。

安装后应有 `CABLE Input` 和 `CABLE Output`。在 Windows 音量混合器将需要投射的播放器输出设为 `CABLE Input`，其他应用继续使用正常输出设备。在 HomePodTray 中选择对应的虚拟声卡并勾选播放器。

HomePodTray 不安装、更新或卸载该驱动，不改动其采样率，不替应用自动配置音频路由。
