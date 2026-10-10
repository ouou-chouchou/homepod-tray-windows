# 第三方来源

| 组件 | 来源 | 许可 |
| --- | --- | --- |
| AirPlay 2 sender core / protocol tests | https://github.com/akustikrausch/airplay2-sender-cpp @ fc913509039412790aa2427558d35178aa7842ea | Apache-2.0；保留 vendor/airplay2-sender-cpp/LICENSE、NOTICE 与 licenses/ |
| Mbed TLS | https://github.com/Mbed-TLS/mbedtls @ 2ca6c285a0dd3f33982dd57299012dacab1ff206 | Apache-2.0；上游 CMake 固定依赖 |
| orlp/ed25519 | vendor/airplay2-sender-cpp/third_party/ed25519 | zlib；保留 SOURCE.md 与许可 |

Win32 / WASAPI API 由 Windows SDK 提供，不包含任何微软样例源码。新的捕获激活代码调用文档公开的进程树 loopback API。新的 sinc 重采样实现采用标准 Blackman 窗口公式，没有复制旧托盘采集引擎。

在散发二进制时应同时提供本文件、根目录 LICENSE、上游 NOTICE，以及 vendor 中对应的第三方许可文本。代码没有收费服务、API key、云端账户或授权码依赖。
