// SPDX-License-Identifier: Apache-2.0
#include "audio.h"
#include "policy.h"
#include "raop_sender.h"
#include "raop_loop.h"
#include "ring_buffer.h"
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <tlhelp32.h>
#include <wrl/implements.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

namespace homepod {
std::wstring wide(const std::string& value) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), out.data(), n);
    return out;
}
std::string utf8(const std::wstring& value) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string lower(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
namespace {
using Clock = std::chrono::steady_clock;
void require(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char text[128]; std::snprintf(text, sizeof(text), "%s: HRESULT 0x%08lX", operation, (unsigned long)hr);
        throw std::runtime_error(text);
    }
}
ComPtr<IMMDeviceEnumerator> enumerator() {
    ComPtr<IMMDeviceEnumerator> out;
    require(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&out)), "MMDeviceEnumerator");
    return out;
}
std::string endpointName(IMMDevice* device) {
    ComPtr<IPropertyStore> store; require(device->OpenPropertyStore(STGM_READ, &store), "endpoint properties");
    PROPVARIANT value{}; require(store->GetValue(PKEY_Device_FriendlyName, &value), "endpoint name");
    const std::string name = value.vt == VT_LPWSTR ? utf8(value.pwszVal) : "Unknown endpoint";
    PropVariantClear(&value); return name;
}
std::string exeName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t buffer[32768]; DWORD size = DWORD(std::size(buffer));
    std::string name;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        name = utf8(std::wstring(buffer, size));
        const size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos) name.erase(0, slash+1);
    }
    CloseHandle(process); return name;
}
ComPtr<IMMDevice> cableEndpoint(const std::string& needle) {
    auto e = enumerator(); ComPtr<IMMDeviceCollection> devices;
    require(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices), "render endpoints");
    UINT count=0; devices->GetCount(&count);
    for (UINT i=0;i<count;++i) {
        ComPtr<IMMDevice> device; devices->Item(i,&device);
        if (device && lower(endpointName(device.Get())).find(lower(needle)) != std::string::npos) return device;
    }
    throw std::runtime_error("找不到虚拟声卡：" + needle + "；不会退回默认设备采集");
}
std::vector<DWORD> selectedRoots(const std::vector<std::string>& selected) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("无法读取进程列表");
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
    std::map<DWORD,DWORD> parents; std::set<DWORD> matches;
    if (Process32FirstW(snapshot,&entry)) do {
        parents[entry.th32ProcessID]=entry.th32ParentProcessID;
        const std::string name=lower(utf8(entry.szExeFile));
        for (const auto& wanted : selected)
            if (name==lower(wanted) && entry.th32ProcessID!=GetCurrentProcessId()) matches.insert(entry.th32ProcessID);
    } while (Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    std::vector<DWORD> out;
    for (DWORD pid : matches) {
        DWORD walk=pid; bool covered=false;
        std::set<DWORD> visited;
        while (parents.count(walk) && visited.insert(walk).second) {
            walk=parents[walk]; if (matches.count(walk)) { covered=true; break; }
        }
        if (!covered) out.push_back(pid);
    }
    if (out.size()>8) throw std::runtime_error("同时采集最多 8 个进程树；请减少勾选");
    return out;
}
class Activation : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, Microsoft::WRL::FtmBase,
    IActivateAudioInterfaceCompletionHandler> {
public:
    Activation() : event(CreateEventW(nullptr,FALSE,FALSE,nullptr)) {}
    ~Activation() { CloseHandle(event); }
    HANDLE event;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation*) override { SetEvent(event); return S_OK; }
};
ComPtr<IAudioClient> processClient(DWORD pid) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId=pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode=PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT value{}; value.vt=VT_BLOB; value.blob.cbSize=sizeof(params);
    value.blob.pBlobData=reinterpret_cast<BYTE*>(&params);
    auto handler=Microsoft::WRL::Make<Activation>();
    if (!handler || !handler->event) throw std::runtime_error("无法创建采集激活事件");
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    require(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),
            &value,handler.Get(),&operation),"activate process loopback");
    if (WaitForSingleObject(handler->event,5000)!=WAIT_OBJECT_0) throw std::runtime_error("进程采集激活超时");
    HRESULT status=E_FAIL; ComPtr<IUnknown> iface;
    require(operation->GetActivateResult(&status,&iface),"activation result");
    require(status,"process loopback activation"); ComPtr<IAudioClient> client;
    require(iface.As(&client),"capture interface"); return client;
}

// 97-tap Blackman windowed sinc; float PCM stays float until final mix quantization.
class Resampler {
public:
    explicit Resampler(unsigned rate) : nominal_(double(rate)/44100.) {
        const double cutoff=.97*std::min(1.,44100./rate);
        table_.resize(1024*97);
        for (int p=0;p<1024;++p) {
            double sum=0.;
            for (int t=0;t<97;++t) {
                const double u=double(t-48)-double(p)/1024.;
                const double x=cutoff*u;
                const double sinc=std::abs(x)<1e-9 ? 1. : std::sin(3.141592653589793*x)/(3.141592653589793*x);
                const double w=std::abs(u)>48 ? 0. : .42+.5*std::cos(3.141592653589793*u/48)+.08*std::cos(2*3.141592653589793*u/48);
                table_[p*97+t]=float(sinc*w); sum+=sinc*w;
            }
            for (int t=0;t<97;++t) table_[p*97+t]/=float(sum);
        }
    }
    void append(std::span<const float> samples, double appliedScale, std::deque<std::array<float,2>>& output) {
        data_.insert(data_.end(),samples.begin(),samples.end());
        while (size_t(pos_)+48<data_.size()/2) {
            const int i=int(pos_); const int phase=int((pos_-i)*1024);
            std::array<float,2> frame{};
            for (int t=0;t<97;++t) {
                const int index=i+t-48;
                if (index>=0 && size_t(index)<data_.size()/2)
                    for (int c=0;c<2;++c) frame[c]+=data_[index*2+c]*table_[phase*97+t];
            }
            output.push_back(frame); pos_+=nominal_*appliedScale;
        }
        const int drop=int(pos_)-48;
        if (drop>0) { data_.erase(data_.begin(),data_.begin()+drop*2); pos_-=drop; }
    }
private:
    double nominal_,pos_=0.; std::vector<float> table_,data_;
};

struct Source {
    DWORD pid=0; unsigned rate=0,channels=0,bits=0,block=0; bool floating=false;
    ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> capture;
    std::unique_ptr<Resampler> resampler;
    std::deque<std::array<float,2>> frames;
    Clock::time_point lastPacket{};
    uint64_t packets=0,discontinuities=0,lastDevicePosition=0,lastQpc=0;
    ~Source() { if (client) client->Stop(); }
    void initialize(DWORD process, IMMDevice* endpoint, WAVEFORMATEX* fallback) {
        pid=process;
        if (pid) client=processClient(pid);
        else require(endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),"endpoint capture");
        WAVEFORMATEX* format=nullptr;
        if (FAILED(client->GetMixFormat(&format)) || !format) {
            const size_t bytes=sizeof(WAVEFORMATEX)+fallback->cbSize;
            format=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(bytes));
            if (!format) throw std::runtime_error("无法分配 PCM 格式");
            std::memcpy(format,fallback,bytes);
        }
        rate=format->nSamplesPerSec; channels=format->nChannels; bits=format->wBitsPerSample; block=format->nBlockAlign;
        bool pcm=format->wFormatTag==WAVE_FORMAT_PCM;
        floating=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
        if (format->wFormatTag==WAVE_FORMAT_EXTENSIBLE && format->cbSize>=22) {
            const auto* ext=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
            floating=IsEqualGUID(ext->SubFormat,KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
            pcm=IsEqualGUID(ext->SubFormat,KSDATAFORMAT_SUBTYPE_PCM);
        }
        const bool supported=(rate==44100 || rate==48000) && channels>=1 && channels<=2 &&
            ((floating && bits==32) || (pcm && (bits==16 || bits==24 || bits==32)));
        const HRESULT result=supported ? client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,200000,0,format,nullptr) : E_INVALIDARG;
        CoTaskMemFree(format); require(result,"initialize capture (44100/48000 PCM required)");
        require(client->GetService(IID_PPV_ARGS(&capture)),"capture service");
        resampler=std::make_unique<Resampler>(rate);
        require(client->Start(),"start capture");
    }
    bool drain(double scale) {
        UINT32 next=0; require(capture->GetNextPacketSize(&next),"next capture packet");
        bool got=false;
        while (next) {
            BYTE* bytes=nullptr; UINT32 count=0; DWORD flags=0; UINT64 position=0,qpc=0;
            require(capture->GetBuffer(&bytes,&count,&flags,&position,&qpc),"capture GetBuffer");
            // ReleaseBuffer is paired even if conversion or allocation fails.
            try {
                std::vector<float> converted(size_t(count)*2,0.f);
                if (!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) for (size_t f=0;f<count;++f) {
                    for (size_t c=0;c<2;++c) {
                        const BYTE* sample=bytes+f*block+std::min(c,size_t(channels-1))*(bits/8);
                        float v=0;
                        if (floating) std::memcpy(&v,sample,4);
                        else if (bits==16) { int16_t n; std::memcpy(&n,sample,2); v=n/32768.f; }
                        else if (bits==32) { int32_t n; std::memcpy(&n,sample,4); v=float(double(n)/2147483648.); }
                        else { int32_t n=int32_t(sample[0])|(int32_t(sample[1])<<8)|(int32_t(sample[2])<<16); if(n&0x800000)n|=int32_t(0xff000000); v=n/8388608.f; }
                        converted[f*2+c]=std::isfinite(v) ? std::clamp(v,-1.f,1.f) : 0.f;
                    }
                }
                if (flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++discontinuities;
                lastDevicePosition=position; lastQpc=qpc; ++packets;
                resampler->append(converted,scale,frames);
            } catch (...) { capture->ReleaseBuffer(count); throw; }
            require(capture->ReleaseBuffer(count),"capture ReleaseBuffer");
            lastPacket=Clock::now(); got=true;
            if (frames.size()>44100) throw std::runtime_error("采集队列超过 1 秒：已停止，避免静默丢帧；可重置音频链路");
            require(capture->GetNextPacketSize(&next),"next capture packet");
        }
        return got;
    }
};

class LocalMonitor {
public:
    void start() {
        auto e=enumerator(); ComPtr<IMMDevice> device;
        require(e->GetDefaultAudioEndpoint(eRender,eConsole,&device),"local default endpoint");
        const auto name=lower(endpointName(device.Get()));
        if (name.find("cable")!=std::string::npos) throw std::runtime_error("默认输出是虚拟声卡，拒绝创建回环；请选择物理默认输出");
        require(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client_.GetAddressOf())),"local playback client");
        WAVEFORMATEX format{}; format.wFormatTag=WAVE_FORMAT_PCM; format.nSamplesPerSec=44100;
        format.nChannels=2; format.wBitsPerSample=16; format.nBlockAlign=4; format.nAvgBytesPerSec=176400;
        require(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                2000000,0,&format,nullptr),"local monitor format");
        require(client_->GetBufferSize(&capacity_),"local buffer size");
        require(client_->GetService(IID_PPV_ARGS(&render_)),"local render service");
        require(client_->Start(),"local monitor start");
    }
    ~LocalMonitor() { if(client_)client_->Stop(); }
    void append(std::span<const int16_t> samples) {
        pending_.insert(pending_.end(),samples.begin(),samples.end());
        if (pending_.size()>44100) throw std::runtime_error("本地监听队列超过 500 毫秒");
        pump();
    }
    void pump() {
        UINT32 padding=0; require(client_->GetCurrentPadding(&padding),"local padding");
        const UINT32 count=std::min(capacity_-padding,UINT32(pending_.size()/2));
        if (!count)return;
        BYTE* out=nullptr; require(render_->GetBuffer(count,&out),"local render buffer");
        for (size_t i=0;i<size_t(count)*2;++i) { std::memcpy(out+i*2,&pending_.front(),2); pending_.pop_front(); }
        require(render_->ReleaseBuffer(count,0),"local render release");
    }
private:
    ComPtr<IAudioClient> client_; ComPtr<IAudioRenderClient> render_; UINT32 capacity_=0;
    std::deque<int16_t> pending_;
};
}

std::vector<AudioApp> listApps() {
    std::vector<AudioApp> out;
    auto e=enumerator(); ComPtr<IMMDeviceCollection> devices;
    require(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices),"list endpoints");
    UINT count=0; devices->GetCount(&count);
    for (UINT d=0;d<count;++d) {
        ComPtr<IMMDevice> device; devices->Item(d,&device); if(!device)continue;
        const std::string name=endpointName(device.Get());
        ComPtr<IAudioSessionManager2> manager;
        if (FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf()))))continue;
        ComPtr<IAudioSessionEnumerator> sessions; if(FAILED(manager->GetSessionEnumerator(&sessions)))continue;
        int total=0; sessions->GetCount(&total);
        for (int i=0;i<total;++i) {
            ComPtr<IAudioSessionControl> session; sessions->GetSession(i,&session); if(!session)continue;
            ComPtr<IAudioSessionControl2> detail; if(FAILED(session.As(&detail)))continue;
            DWORD pid=0; detail->GetProcessId(&pid);
            if (!pid || pid==GetCurrentProcessId())continue;
            AudioSessionState state; session->GetState(&state);
            const auto exe=exeName(pid);
            if (!exe.empty()) out.push_back({exe,name,pid,state==AudioSessionStateActive});
        }
    }
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return lower(a.exe)<lower(b.exe);});
    return out;
}

struct AudioEngine::Impl {
    Log log; Changed changed; std::atomic<bool> stop{false},running{false};
    std::atomic<int> volume{25}; std::thread worker;
    void status(Status::State state,const std::string& text,double buffered=0,double ppm=0) { changed({state,text,buffered,ppm}); }
    void run(Config config) {
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com)) { status(Status::State::Error,"无法初始化 COM"); running=false; return; }
        try {
            if (config.capture==CaptureMode::Applications && config.selected.empty()) {
                status(Status::State::Waiting,"未勾选应用：不采集任何声音");
                while (!stop) std::this_thread::sleep_for(50ms);
            } else {
                const bool network=config.output!=Output::LocalOnly;
                const bool monitor=config.output!=Output::HomePodOnly;
                auto endpoint=cableEndpoint(config.cable);
                ComPtr<IAudioClient> endpointClient;
                require(endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(endpointClient.GetAddressOf())),"format endpoint");
                WAVEFORMATEX* fallback=nullptr; require(endpointClient->GetMixFormat(&fallback),"virtual cable mix format");
                struct FormatGuard { WAVEFORMATEX* f; ~FormatGuard(){CoTaskMemFree(f);} } guard{fallback};
                std::unique_ptr<LocalMonitor> local;
                if(monitor){local=std::make_unique<LocalMonitor>();local->start();}
                fxchain::RingBuffer<int16_t> ring(44100*2); // actual power-of-two capacity ~743 ms
                fxchain::RaopLoop loop;
                bool connected=false,failed=false;
                std::string failure;
                fxchain::RaopEvents events;
                events.launched=[&](bool ok,const std::string& error){connected=ok; if(!ok){failed=true;failure=error;}};
                events.closed=[&]{connected=false;failed=true;if(failure.empty())failure="HomePod 已断开；请手动重新连接";};
                events.pinRequired=[&](const std::string&){failed=true;failure="此 MVP 支持 HomePod 无 PIN 配对；请检查家庭 App 的扬声器访问权限";};
                fxchain::RaopSender sender(loop,events,[&](fxchain::RaopLogLevel,const std::string& line){log(line);});
                sender.attachRing(&ring); sender.setInputFormat(44100); sender.setCaptureActive(false);
                sender.setAuth(fxchain::RaopDeviceInfo::Auth::HapTransient,true,"", "", "");
                sender.setIdentity({"HomePodTray Rebuild","AA:BB:CC:DD:EE:FF","iPhone14,3"});
                int sentVolume=volume.load();
                if(network) { sender.start(config.receiver,config.port,"HomePod"); sender.setVolume(sentVolume); }
                status(network?Status::State::Connecting:Status::State::Waiting,network?"正在连接 HomePod":"本地监听等待音频");
                std::map<DWORD,std::unique_ptr<Source>> sources;
                ClockPolicy controller;
                auto nextSources=Clock::time_point{},lastTick=Clock::now(),nextReport=Clock::now()+1s,nextLog=Clock::now()+10s;
                std::vector<int16_t> mixed;
                bool echoed=false; auto connectedAt=Clock::time_point{};
                while (!stop && !failed) {
                    const auto now=Clock::now();
                    if(now>=nextSources && (!network || connected)) {
                        nextSources=now+1s;
                        // Only explicit virtual-cable routing can guarantee selected audio stays
                        // off local speakers. Never mute a real/default endpoint to enforce it.
                        if(config.capture==CaptureMode::Applications)
                            for(const auto& app : listApps()) for(const auto& selected : config.selected)
                                if (lower(app.exe)==lower(selected) && app.active && lower(app.endpoint).find(lower(config.cable))==std::string::npos)
                                    throw std::runtime_error(app.exe+" 尚未输出到 "+config.cable+"；请在 Windows 音量混合器显式路由，程序不会改默认设备");
                        auto roots=config.capture==CaptureMode::CableEndpoint?std::vector<DWORD>{0}:selectedRoots(config.selected);
                        for(auto it=sources.begin();it!=sources.end();) {
                            if(std::find(roots.begin(),roots.end(),it->first)==roots.end())it=sources.erase(it);else ++it;
                        }
                        for(DWORD pid:roots) if(!sources.count(pid)) {
                            auto source=std::make_unique<Source>(); source->initialize(pid,endpoint.Get(),fallback);
                            log("capture source="+std::to_string(pid)+" format="+std::to_string(source->rate)+"Hz "+std::to_string(source->bits)+"bit "+std::to_string(source->channels)+"ch; wire=44100Hz 16bit stereo");
                            sources[pid]=std::move(source);
                        }
                    }
                    bool receiving=false;
                    for(auto& [pid,source]:sources) { source->drain(controller.applied()); receiving|=now-source->lastPacket<100ms; }
                    sender.setCaptureActive(receiving);
                    const double dt=std::chrono::duration<double>(now-lastTick).count(); lastTick=now;
                    controller.update(network?double(ring.availableRead())/88200.:.20,receiving,dt);
                    // Do not consume source FIFOs until the destination has space for the
                    // complete mixed block. Bounded source queues fail visibly, never drop.
                    size_t frames=441;
                    bool anySource=false;
                    for(auto& [pid,source]:sources) {
                        // A recent source whose FIFO is briefly empty must wait for its
                        // next WASAPI packet, rather than inject an artificial silent hole.
                        if(!source->frames.empty() || now-source->lastPacket<100ms) {
                            frames=std::min(frames,source->frames.size());anySource=true;
                        }
                    }
                    if(!anySource)frames=0;
                    if(network)frames=std::min(frames,ring.availableWrite()/2);
                    if(frames) {
                        mixed.assign(frames*2,0);
                        for(size_t i=0;i<frames;++i) {
                            std::array<float,2> value{};
                            for(auto& [pid,source]:sources) if(!source->frames.empty()) {
                                const auto frame=source->frames.front(); source->frames.pop_front();
                                value[0]+=frame[0]; value[1]+=frame[1];
                            }
                            for(size_t c=0;c<2;++c)mixed[i*2+c]=int16_t(std::lround(std::clamp(value[c],-1.f,1.f)*32767.f));
                        }
                        if(network && !ring.tryPush(mixed))throw std::runtime_error("ring reservation invariant failed");
                        if(local)local->append(mixed);
                    }
                    if(local)local->pump();
                    if(network) {
                        loop.pump(sender,2ms);
                        if(connected && connectedAt==Clock::time_point{})connectedAt=now;
                        if(connected && (!echoed && now-connectedAt>=1500ms)) { sender.setVolume(volume.load()); echoed=true; }
                        const int wanted=volume.load();
                        if(connected && wanted!=sentVolume){sender.setVolume(wanted);sentVolume=wanted;}
                    } else std::this_thread::sleep_for(2ms);
                    if(now>=nextReport) {
                        nextReport=now+1s;
                        const bool primed=!network || sender.playbackPrimed();
                        const auto state=network && !connected?Status::State::Connecting:receiving && primed?Status::State::Streaming:Status::State::Waiting;
                        const std::string text=network && !connected?"正在连接 HomePod":receiving?(primed?"正在播放所选音频":"正在预缓冲（目标 200ms）"):(network?"已连接，等待所选应用音频":"本地监听等待所选音频");
                        status(state,text,
                                double(ring.availableRead())/88.2,(controller.applied()-1.)*1e6);
                    }
                    if(now>=nextLog) {
                        nextLog=now+10s; const auto stats=sender.audioStats();
                        uint64_t gaps=0;for(auto& [pid,source]:sources)gaps+=source->discontinuities;
                        log("audio active="+std::to_string(receiving)+" ring_ms="+std::to_string(double(ring.availableRead())/88.2)+
                            " applied_ppm="+std::to_string((controller.applied()-1.)*1e6)+" active_underflows="+std::to_string(stats.activeUnderflows)+
                            " active_padding_frames="+std::to_string(stats.activePaddedFrames)+" idle_packets="+std::to_string(stats.idlePackets)+
                            " priming_packets="+std::to_string(stats.primingPackets)+" capture_discontinuities="+std::to_string(gaps)+
                            " udp_send_errors="+std::to_string(loop.udpSendErrors())+" retransmit_requests="+std::to_string(stats.retransmitRequests)+
                            " retransmit_missed="+std::to_string(stats.retransmitMissed));
                    }
                }
                sender.stop(); loop.pump(sender,0ms);
                if(failed && !stop){log("session error: "+failure);status(Status::State::Error,failure);}
            }
        } catch(const std::exception& error) { log(std::string("engine error: ")+error.what());status(Status::State::Error,error.what()); }
        CoUninitialize(); running=false;
        if(stop)status(Status::State::Disconnected,"已断开；不会自动抢占 HomePod");
    }
};
AudioEngine::AudioEngine(Log log,Changed changed):impl_(std::make_unique<Impl>()) {impl_->log=std::move(log);impl_->changed=std::move(changed);}
AudioEngine::~AudioEngine(){stop();}
bool AudioEngine::start(Config config) {
    if(impl_->running)return false;
    if(impl_->worker.joinable())impl_->worker.join();
    if(config.output!=Output::LocalOnly && config.receiver.empty()){impl_->status(Status::State::Error,"请先选择 HomePod 或输入 IPv4 地址");return false;}
    impl_->stop=false;impl_->running=true;impl_->volume=config.volume;
    impl_->worker=std::thread([this,config=std::move(config)]{impl_->run(config);});return true;
}
void AudioEngine::stop(){impl_->stop=true;if(impl_->worker.joinable())impl_->worker.join();}
void AudioEngine::setVolume(int value){impl_->volume=std::clamp(value,0,100);}
bool AudioEngine::running() const{return impl_->running;}
}
