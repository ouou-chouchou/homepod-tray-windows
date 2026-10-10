// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace homepod {
std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
std::string lower(std::string text);
struct AudioApp { std::string exe, endpoint; unsigned long pid; bool active; };
std::vector<AudioApp> listApps();
enum class Output { HomePodOnly, Both, LocalOnly };
enum class CaptureMode { Applications, CableEndpoint };
struct Config {
    std::string receiver;
    unsigned short port = 7000;
    std::vector<std::string> selected;
    std::string cable = "CABLE Input";
    Output output = Output::HomePodOnly;
    CaptureMode capture = CaptureMode::Applications;
    int volume = 25;
};
struct Status {
    enum class State { Disconnected, Waiting, Connecting, Streaming, Error };
    State state = State::Disconnected;
    std::string text = "未连接";
    double bufferMs = 0.;
    double appliedPpm = 0.;
};
class AudioEngine {
public:
    using Log = std::function<void(const std::string&)>;
    using Changed = std::function<void(const Status&)>;
    AudioEngine(Log log, Changed changed);
    ~AudioEngine();
    bool start(Config config);
    void stop();
    void setVolume(int value);
    bool running() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
