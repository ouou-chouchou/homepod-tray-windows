// SPDX-License-Identifier: Apache-2.0
#include "audio.h"
#include "discovery.h"
#include "resources.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
using namespace homepod;
namespace {
constexpr UINT TrayMessage=WM_APP+1,DiscoveryMessage=WM_APP+2;
constexpr int Connect=1,Disconnect=2,Reset=3,Volume=4,Settings=5,Refresh=6,Log=7,Autostart=8,Shortcut=9,Quit=10;
constexpr int ShowMain=11;
constexpr int AppBase=100,ReceiverBase=300,OutputBase=500,CaptureBase=600;
constexpr int UiReceivers=701,UiIp=702,UiCable=703,UiApps=704,UiRefreshApps=705,
              UiOutput=706,UiCapture=707,UiVolume=708,UiApply=709,UiAutostart=710;
HWND window=nullptr,volumeWindow=nullptr,settingsWindow=nullptr,slider=nullptr,ipEdit=nullptr,cableEdit=nullptr;
HWND mainReceivers=nullptr,mainIp=nullptr,mainCable=nullptr,mainApps=nullptr,mainOutput=nullptr,
     mainCapture=nullptr,mainVolume=nullptr,mainVolumeLabel=nullptr,mainStatus=nullptr,mainAutostart=nullptr,
     mainConnect=nullptr,mainDisconnect=nullptr,mainReset=nullptr,mainSearch=nullptr;
HFONT mainFont=nullptr;
std::vector<std::string> mainAppNames;
void refreshMainApps(bool preserveDraft);
void syncMainConfig();
void updateMainStatus();
void refreshMainReceivers();
UINT taskbarCreated=0;
Config config;Status status;std::mutex stateMutex,logMutex;
std::unique_ptr<AudioEngine> engine;
std::vector<std::string> appNames;std::vector<Receiver> receivers;
std::thread discoveryThread;bool discoveryBusy=false,autostart=false,shutdown=false;
std::filesystem::path dataDir;
std::filesystem::path executablePath() {wchar_t text[32768];GetModuleFileNameW(nullptr,text,DWORD(std::size(text)));return text;}
void logLine(const std::string& text) {
    std::lock_guard lock(logMutex);
    std::ofstream file(dataDir/L"HomePodTray.log",std::ios::binary|std::ios::app);
    SYSTEMTIME time;GetLocalTime(&time);char stamp[64];
    std::snprintf(stamp,sizeof(stamp),"%04u-%02u-%02u %02u:%02u:%02u.%03u ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    file<<stamp<<text<<"\r\n";
}
void save() {
    const auto target=dataDir/L"settings.txt",temporary=dataDir/L"settings.tmp";
    {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    file<<"receiver="<<config.receiver<<"\nport="<<config.port<<"\nvolume="<<config.volume<<"\ncable="<<config.cable
        <<"\noutput="<<int(config.output)<<"\ncapture="<<int(config.capture)<<"\nautostart="<<autostart<<"\n";
    for(const auto& name:config.selected)file<<"app="<<name<<"\n";}
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))logLine("Failed to save config: "+std::to_string(GetLastError()));
}
void load() {
    std::ifstream file(dataDir/L"settings.txt",std::ios::binary);std::string line;
    while(std::getline(file,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();const auto equals=line.find('=');if(equals==std::string::npos)continue;
        const auto key=line.substr(0,equals),value=line.substr(equals+1);
        try {
            if(key=="receiver")config.receiver=value;
            else if(key=="port"){const int port=std::stoi(value);if(port>0&&port<=65535)config.port=static_cast<unsigned short>(port);}
            else if(key=="volume")config.volume=std::clamp(std::stoi(value),0,100);
            else if(key=="cable"&&!value.empty())config.cable=value;
            else if(key=="output"){const int mode=std::stoi(value);if(mode>=0&&mode<=2)config.output=Output(mode);}
            else if(key=="capture"){const int mode=std::stoi(value);if(mode>=0&&mode<=1)config.capture=CaptureMode(mode);}
            else if(key=="autostart")autostart=value=="1";
            else if(key=="app"&&!value.empty())config.selected.push_back(value);
        }catch(const std::exception&){logLine("Ignoring invalid setting: "+key);}
    }
}
void setAutostart() {
    HKEY key=nullptr;const auto code=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(code!=ERROR_SUCCESS){MessageBoxW(window,L"无法写入当前用户开机启动项。",L"HomePod 投射",MB_ICONERROR);return;}
    const std::wstring path=L"\""+executablePath().wstring()+L"\"";
    if(autostart)RegSetValueExW(key,L"HomePodTrayRebuild",0,REG_SZ,reinterpret_cast<const BYTE*>(path.c_str()),DWORD((path.size()+1)*sizeof(wchar_t)));
    else RegDeleteValueW(key,L"HomePodTrayRebuild");
    RegCloseKey(key);save();
}
void shortcut() {
    wchar_t desktop[MAX_PATH];if(FAILED(SHGetFolderPathW(window,CSIDL_DESKTOPDIRECTORY,nullptr,0,desktop)))return;
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))))return;
    const auto exe=executablePath();link->SetPath(exe.c_str());link->SetWorkingDirectory(exe.parent_path().c_str());
    link->SetDescription(L"HomePod 投射新版（启动后手动连接）");
    Microsoft::WRL::ComPtr<IPersistFile> persist;link.As(&persist);
    const auto target=std::filesystem::path(desktop)/L"HomePod 投射（新版）.lnk";
    if(SUCCEEDED(persist->Save(target.c_str(),TRUE)))MessageBoxW(window,L"已创建“HomePod 投射（新版）”桌面快捷方式。",L"HomePod 投射",MB_OK);
}
void updateTray(bool add=false) {
    Status snapshot;{std::lock_guard lock(stateMutex);snapshot=status;}
    NOTIFYICONDATAW icon{};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=1;
    icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;icon.uCallbackMessage=TrayMessage;
    icon.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(snapshot.state==Status::State::Error?IDI_HOMEPOD_WARNING:IDI_HOMEPOD));
    const auto tip=wide("HomePod 投射新版："+snapshot.text);wcsncpy_s(icon.szTip,tip.c_str(),_TRUNCATE);
    Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&icon);
}
void stop() {engine->stop();logLine("User disconnected; no automatic reconnect or takeover");}
void restartIfRunning(bool wasRunning) {if(wasRunning){stop();engine->start(config);}}
void refreshDiscovery() {
    if(discoveryBusy)return;if(discoveryThread.joinable())discoveryThread.join();discoveryBusy=true;
    discoveryThread=std::thread([] {
        std::vector<Receiver> found;
        try {found=discoverReceivers();logLine("mDNS discovery: "+std::to_string(found.size())+" receiver(s)");}
        catch(const std::exception& e){logLine(std::string("Discovery error: ")+e.what());}
        {std::lock_guard lock(stateMutex);receivers=std::move(found);}
        PostMessageW(window,DiscoveryMessage,0,0);
    });
}
void add(HMENU menu,int id,const std::wstring& title,bool checked=false,bool disabled=false) {
    AppendMenuW(menu,MF_STRING|(checked?MF_CHECKED:0)|(disabled?MF_GRAYED:0),UINT_PTR(id),title.c_str());
}
void submenu(HMENU parent,HMENU child,const wchar_t* name){AppendMenuW(parent,MF_POPUP,reinterpret_cast<UINT_PTR>(child),name);}
void popup() {
    HMENU menu=CreatePopupMenu();Status snapshot;std::vector<Receiver> found;
    {std::lock_guard lock(stateMutex);snapshot=status;found=receivers;}
    add(menu,0,wide(snapshot.text),false,true);
    add(menu,0,wide("缓冲 "+std::to_string(int(snapshot.bufferMs))+" ms / 实际补偿 "+std::to_string(int(snapshot.appliedPpm))+" ppm"),false,true);
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    add(menu,ShowMain,L"打开主窗口");
    add(menu,Connect,L"连接 / 开始监听",false,engine->running());add(menu,Disconnect,L"断开（让手机 / iPad 接管）",false,!engine->running());
    add(menu,Reset,L"重置音频链路（重新连接并预缓冲）",false,!engine->running());
    HMENU apps=CreatePopupMenu();appNames=config.selected;
    try{for(const auto& app:listApps())if(std::find_if(appNames.begin(),appNames.end(),[&](const auto& name){return lower(name)==lower(app.exe);})==appNames.end())appNames.push_back(app.exe);}
    catch(const std::exception& error){logLine(error.what());}
    std::sort(appNames.begin(),appNames.end(),[](const auto& a,const auto& b){return lower(a)<lower(b);});
    add(apps,0,L"空选 = 不投任何声音；修改勾选会重建链路",false,true);
    for(size_t i=0;i<appNames.size()&&i<180;++i) {
        const bool checked=std::find_if(config.selected.begin(),config.selected.end(),[&](const auto& name){return lower(name)==lower(appNames[i]);})!=config.selected.end();
        add(apps,AppBase+int(i),wide(appNames[i]),checked);
    }
    submenu(menu,apps,L"勾选播放应用");
    HMENU outputs=CreatePopupMenu();add(outputs,OutputBase,L"仅 HomePod",config.output==Output::HomePodOnly);
    add(outputs,OutputBase+1,L"HomePod + 电脑默认输出",config.output==Output::Both);
    add(outputs,OutputBase+2,L"仅电脑默认输出",config.output==Output::LocalOnly);
    submenu(menu,outputs,L"播放到哪些设备");
    HMENU modes=CreatePopupMenu();add(modes,CaptureBase,L"只采集勾选应用（默认）",config.capture==CaptureMode::Applications);
    add(modes,CaptureBase+1,L"采集虚拟声卡端点（诊断对照）",config.capture==CaptureMode::CableEndpoint);
    submenu(menu,modes,L"采集方式");
    HMENU destinations=CreatePopupMenu();
    for(size_t i=0;i<found.size()&&i<100;++i)add(destinations,ReceiverBase+int(i),wide(found[i].name+" ("+found[i].ip+")"),config.receiver==found[i].ip);
    add(destinations,Refresh,discoveryBusy?L"正在搜索…":L"搜索局域网 HomePod",false,discoveryBusy);
    add(destinations,Settings,L"输入 IP / 虚拟声卡名称…");submenu(menu,destinations,L"HomePod 选择");
    add(menu,Volume,wide("HomePod 音量 "+std::to_string(config.volume)+" / 100…"));
    add(menu,Autostart,L"登录后启动（不自动连接）",autostart);add(menu,Shortcut,L"创建新版桌面快捷方式");
    add(menu,Log,L"打开诊断日志");add(menu,Quit,L"退出");
    POINT point;GetCursorPos(&point);SetForegroundWindow(window);
    const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
    DestroyMenu(menu);if(command)PostMessageW(window,WM_COMMAND,WPARAM(command),0);
}
void showVolume() {
    if(volumeWindow){ShowWindow(volumeWindow,SW_SHOW);SetForegroundWindow(volumeWindow);return;}
    volumeWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"HomePodTrayWindow",L"HomePod 音量（实时调节，不重连）",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,420,130,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    slider=CreateWindowExW(0,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,20,20,365,50,volumeWindow,reinterpret_cast<HMENU>(20),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(slider,TBM_SETPOS,TRUE,config.volume);
    SendMessageW(slider,TBM_SETTICFREQ,10,0);ShowWindow(volumeWindow,SW_SHOW);
}
std::wstring editValue(HWND edit) {const int n=GetWindowTextLengthW(edit);std::wstring text(size_t(n+1),L'\0');GetWindowTextW(edit,text.data(),n+1);text.resize(size_t(n));return text;}
std::vector<std::string> mainSelected() {
    std::vector<std::string> selected;
    for(size_t i=0;i<mainAppNames.size();++i)
        if(ListView_GetCheckState(mainApps,int(i)))selected.push_back(mainAppNames[i]);
    return selected;
}
void refreshMainApps(bool preserveDraft) {
    if(!mainApps)return;
    const auto selected=preserveDraft?mainSelected():config.selected;
    std::map<std::string,std::pair<std::string,std::set<std::string>>> rows;
    for(const auto& name:selected)rows[lower(name)].first=name;
    try {
        for(const auto& app:listApps()) {
            auto& row=rows[lower(app.exe)];row.first=app.exe;row.second.insert(app.endpoint);
        }
    }catch(const std::exception& error){logLine(std::string("UI refresh apps: ")+error.what());}
    SendMessageW(mainApps,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(mainApps);mainAppNames.clear();
    for(const auto& [key,row]:rows) {
        const int index=int(mainAppNames.size());mainAppNames.push_back(row.first);
        auto label=wide(row.first);LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=label.data();
        ListView_InsertItem(mainApps,&item);
        std::string route;
        for(const auto& endpoint:row.second){if(!route.empty())route+="；";route+=endpoint;}
        if(route.empty())route="未发现播放会话（勾选仍保留）";
        auto routeLabel=wide(route);ListView_SetItemText(mainApps,index,1,routeLabel.data());
        const bool checked=std::find_if(selected.begin(),selected.end(),[&](const auto& name){return lower(name)==key;})!=selected.end();
        ListView_SetCheckState(mainApps,index,checked);
    }
    SendMessageW(mainApps,WM_SETREDRAW,TRUE,0);InvalidateRect(mainApps,nullptr,TRUE);
}
void refreshMainReceivers() {
    if(!mainReceivers)return;
    std::vector<Receiver> found;{std::lock_guard lock(stateMutex);found=receivers;}
    SendMessageW(mainReceivers,CB_RESETCONTENT,0,0);
    int selected=-1;
    const auto current=utf8(editValue(mainIp));
    for(size_t i=0;i<found.size();++i) {
        const auto label=wide(found[i].name+" ("+found[i].ip+")");
        SendMessageW(mainReceivers,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        if(found[i].ip==current)selected=int(i);
    }
    if(found.empty()) {
        SendMessageW(mainReceivers,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"尚未搜索；也可直接填写 IPv4"));selected=0;
    }
    SendMessageW(mainReceivers,CB_SETCURSEL,selected,0);
    SetWindowTextW(mainSearch,discoveryBusy?L"搜索中…":L"搜索 HomePod");EnableWindow(mainSearch,!discoveryBusy);
}
void syncMainConfig() {
    if(!mainIp)return;
    SetWindowTextW(mainIp,wide(config.receiver).c_str());SetWindowTextW(mainCable,wide(config.cable).c_str());
    SendMessageW(mainOutput,CB_SETCURSEL,int(config.output),0);SendMessageW(mainCapture,CB_SETCURSEL,int(config.capture),0);
    SendMessageW(mainVolume,TBM_SETPOS,TRUE,config.volume);
    SetWindowTextW(mainVolumeLabel,wide(std::to_string(config.volume)+" / 100").c_str());
    SendMessageW(mainAutostart,BM_SETCHECK,autostart?BST_CHECKED:BST_UNCHECKED,0);
    refreshMainApps(false);refreshMainReceivers();
}
bool applyMain(bool reconnect) {
    Config desired=config;
    desired.receiver=utf8(editValue(mainIp));desired.cable=utf8(editValue(mainCable));
    desired.selected=mainSelected();
    desired.output=Output(SendMessageW(mainOutput,CB_GETCURSEL,0,0));
    desired.capture=CaptureMode(SendMessageW(mainCapture,CB_GETCURSEL,0,0));
    IN_ADDR address{};
    if((desired.output!=Output::LocalOnly && inet_pton(AF_INET,desired.receiver.c_str(),&address)!=1) || desired.cable.empty()) {
        MessageBoxW(window,L"请选择 HomePod 或填写有效 IPv4 地址，并填写虚拟声卡名称。",L"无法应用设置",MB_ICONWARNING);return false;
    }
    desired.port=7000;
    {std::lock_guard lock(stateMutex);for(const auto& receiver:receivers)if(receiver.ip==desired.receiver)desired.port=receiver.port;}
    if(desired.capture==CaptureMode::CableEndpoint && config.capture!=CaptureMode::CableEndpoint &&
       MessageBoxW(window,L"端点诊断模式会采集所有输出到虚拟声卡的应用，勾选列表暂不生效。确认只有你需要的声音输出到该声卡后再继续。",L"端点诊断模式",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return false;
    const bool changed=desired.receiver!=config.receiver || desired.port!=config.port || desired.cable!=config.cable ||
        desired.selected!=config.selected || desired.output!=config.output || desired.capture!=config.capture;
    const bool running=engine->running();config=std::move(desired);save();
    if(reconnect && changed && running)restartIfRunning(true);
    logLine(changed?"UI settings applied":"UI settings unchanged");return true;
}
void updateMainStatus() {
    if(!mainStatus)return;
    Status snapshot;{std::lock_guard lock(stateMutex);snapshot=status;}
    const char* state=snapshot.state==Status::State::Disconnected?"未连接":snapshot.state==Status::State::Waiting?"等待音频":
        snapshot.state==Status::State::Connecting?"正在连接":snapshot.state==Status::State::Streaming?"播放中":"错误";
    const auto text=wide(std::string("状态：")+state+"\r\n"+snapshot.text+"\r\n缓冲："+
        std::to_string(int(snapshot.bufferMs))+" ms     实际时钟补偿："+std::to_string(int(snapshot.appliedPpm))+" ppm");
    if(editValue(mainStatus)!=text)SetWindowTextW(mainStatus,text.c_str());
    const bool running=engine->running();EnableWindow(mainConnect,!running);EnableWindow(mainDisconnect,running);EnableWindow(mainReset,running);
}
void showMain() {ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);}
void createMainControls() {
    const UINT dpi=GetDpiForWindow(window);const double scale=double(dpi)/96.;
    auto units=[&](int value){return int(value*scale+.5);};
    mainFont=CreateFontW(-units(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto child=[&](const wchar_t* type,const wchar_t* label,int x,int y,int width,int height,int id,DWORD style=0,DWORD extended=0) {
        HWND control=CreateWindowExW(extended,type,label,WS_CHILD|WS_VISIBLE|style,units(x),units(y),units(width),units(height),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(mainFont),TRUE);return control;
    };
    child(L"STATIC",L"HomePod 投射",24,16,500,26,0);
    child(L"STATIC",L"只投勾选的应用，其他程序照常使用电脑默认输出。",24,46,710,24,0);
    child(L"STATIC",L"接收器",24,85,90,24,0);
    mainReceivers=child(WC_COMBOBOXW,L"",116,79,454,180,UiReceivers,CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL);
    mainSearch=child(L"BUTTON",L"搜索 HomePod",584,79,150,32,Refresh,WS_TABSTOP);
    child(L"STATIC",L"手动 IPv4",24,123,90,24,0);
    mainIp=child(L"EDIT",L"",116,118,215,29,UiIp,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    child(L"STATIC",L"声卡名称",350,123,92,24,0);
    mainCable=child(L"EDIT",L"",446,118,288,29,UiCable,WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL);
    child(L"STATIC",L"勾选需要投射的应用（空选 = 不投任何声音）",24,163,560,25,0);
    child(L"BUTTON",L"刷新应用",624,156,110,31,UiRefreshApps,WS_TABSTOP);
    mainApps=child(WC_LISTVIEWW,L"",24,192,710,176,UiApps,LVS_REPORT|LVS_SHOWSELALWAYS|WS_BORDER|WS_TABSTOP);
    ListView_SetExtendedListViewStyle(mainApps,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(L"播放应用");column.cx=units(250);ListView_InsertColumn(mainApps,0,&column);
    column.pszText=const_cast<wchar_t*>(L"当前 Windows 输出设备");column.cx=units(430);ListView_InsertColumn(mainApps,1,&column);
    child(L"STATIC",L"播放到",24,388,90,24,0);
    mainOutput=child(WC_COMBOBOXW,L"",116,382,268,140,UiOutput,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(const auto* label:{L"仅 HomePod",L"HomePod + 电脑默认输出",L"仅电脑默认输出"})SendMessageW(mainOutput,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    child(L"STATIC",L"采集方式",407,388,86,24,0);
    mainCapture=child(WC_COMBOBOXW,L"",499,382,235,120,UiCapture,CBS_DROPDOWNLIST|WS_TABSTOP);
    for(const auto* label:{L"只采集勾选应用",L"虚拟声卡端点（诊断）"})SendMessageW(mainCapture,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    child(L"STATIC",L"先在 Windows 音量混合器将所选应用输出设为 CABLE Input。\r\n选择播放目标不会改变其他应用或系统默认设备。",24,426,710,43,0);
    child(L"STATIC",L"HomePod 音量",24,484,128,25,0);
    mainVolume=child(TRACKBAR_CLASSW,L"",153,474,470,38,UiVolume,WS_TABSTOP|TBS_AUTOTICKS);
    SendMessageW(mainVolume,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(mainVolume,TBM_SETTICFREQ,10,0);
    mainVolumeLabel=child(L"STATIC",L"",646,484,88,25,0);
    mainConnect=child(L"BUTTON",L"连接",24,522,104,34,Connect,WS_TABSTOP);
    mainDisconnect=child(L"BUTTON",L"断开",142,522,104,34,Disconnect,WS_TABSTOP);
    mainReset=child(L"BUTTON",L"重置音频链路",260,522,152,34,Reset,WS_TABSTOP);
    child(L"BUTTON",L"应用设置",426,522,132,34,UiApply,WS_TABSTOP);
    child(L"BUTTON",L"查看日志",572,522,162,34,Log,WS_TABSTOP);
    mainStatus=child(L"EDIT",L"",24,574,710,70,0,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP);
    child(L"STATIC",L"关闭窗口会隐藏到托盘。设备 / 应用改动需点“应用设置”。",24,658,520,24,0);
    mainAutostart=child(L"BUTTON",L"登录后启动",565,653,169,30,UiAutostart,WS_TABSTOP|BS_AUTOCHECKBOX);
    syncMainConfig();updateMainStatus();
}
void showSettings() {
    if(settingsWindow){ShowWindow(settingsWindow,SW_SHOW);SetForegroundWindow(settingsWindow);return;}
    settingsWindow=CreateWindowExW(WS_EX_TOOLWINDOW,L"HomePodTrayWindow",L"HomePod 与虚拟声卡设置",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,510,240,window,nullptr,GetModuleHandleW(nullptr),nullptr);
    auto child=[&](const wchar_t* kind,const wchar_t* label,int x,int y,int width,int height,int id,DWORD style=0) {
        return CreateWindowExW(0,kind,label,WS_CHILD|WS_VISIBLE|style,x,y,width,height,settingsWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    };
    child(L"STATIC",L"HomePod IPv4 地址",20,20,180,25,0);
    ipEdit=child(L"EDIT",wide(config.receiver).c_str(),205,17,265,28,30,WS_BORDER|ES_AUTOHSCROLL);
    child(L"STATIC",L"虚拟声卡名称包含",20,65,180,25,0);
    cableEdit=child(L"EDIT",wide(config.cable).c_str(),205,62,265,28,31,WS_BORDER|ES_AUTOHSCROLL);
    child(L"STATIC",L"请将所选应用显式路由到该声卡；默认设备保持不变。",20,107,455,40,0);
    child(L"BUTTON",L"保存",275,156,90,30,32);child(L"BUTTON",L"取消",380,156,90,30,33);
    ShowWindow(settingsWindow,SW_SHOW);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    if(taskbarCreated && message==taskbarCreated){updateTray(true);return 0;}
    if(message==WM_HSCROLL && hwnd==volumeWindow) {
        config.volume=int(SendMessageW(slider,TBM_GETPOS,0,0));engine->setVolume(config.volume);save();
        if(mainVolume)SendMessageW(mainVolume,TBM_SETPOS,TRUE,config.volume);
        if(mainVolumeLabel)SetWindowTextW(mainVolumeLabel,wide(std::to_string(config.volume)+" / 100").c_str());
        const auto title=wide("HomePod 音量："+std::to_string(config.volume)+" / 100（不重连）");SetWindowTextW(hwnd,title.c_str());return 0;
    }
    if(message==WM_HSCROLL && hwnd==window && reinterpret_cast<HWND>(lParam)==mainVolume) {
        config.volume=int(SendMessageW(mainVolume,TBM_GETPOS,0,0));engine->setVolume(config.volume);save();
        SetWindowTextW(mainVolumeLabel,wide(std::to_string(config.volume)+" / 100").c_str());
        if(slider)SendMessageW(slider,TBM_SETPOS,TRUE,config.volume);
        return 0;
    }
    if(message==WM_CLOSE) {
        if(hwnd==window){ShowWindow(window,SW_HIDE);return 0;}
        if(hwnd==volumeWindow){DestroyWindow(hwnd);volumeWindow=nullptr;slider=nullptr;return 0;}
        if(hwnd==settingsWindow){DestroyWindow(hwnd);settingsWindow=nullptr;return 0;}
    }
    if(message==WM_COMMAND && hwnd==settingsWindow) {
        const int command=LOWORD(wParam);
        if(command==32) {
            const auto ip=utf8(editValue(ipEdit)),cable=utf8(editValue(cableEdit));IN_ADDR address{};
            if(inet_pton(AF_INET,ip.c_str(),&address)!=1 || cable.empty()) {MessageBoxW(hwnd,L"请输入有效 IPv4 地址和声卡名称。",L"设置",MB_ICONWARNING);return 0;}
            const bool running=engine->running();config.receiver=ip;config.port=7000;config.cable=cable;save();restartIfRunning(running);syncMainConfig();
        }
        if(command==32||command==33){DestroyWindow(hwnd);settingsWindow=nullptr;}return 0;
    }
    if(message==TrayMessage){if(lParam==WM_LBUTTONUP)showMain();else if(lParam==WM_RBUTTONUP)popup();return 0;}
    if(message==DiscoveryMessage){discoveryBusy=false;refreshMainReceivers();return 0;}
    if(message==WM_TIMER){updateTray();updateMainStatus();return 0;}
    if(message==WM_COMMAND && hwnd==window) {
        const int command=LOWORD(wParam);const bool running=engine->running();
        if(command==UiReceivers && HIWORD(wParam)==CBN_SELCHANGE) {
            const int index=int(SendMessageW(mainReceivers,CB_GETCURSEL,0,0));
            std::lock_guard lock(stateMutex);
            if(index>=0 && size_t(index)<receivers.size())SetWindowTextW(mainIp,wide(receivers[size_t(index)].ip).c_str());return 0;
        }
        if(command==UiRefreshApps){refreshMainApps(true);return 0;}
        if(command==UiApply){applyMain(true);return 0;}
        if(command==UiAutostart){autostart=SendMessageW(mainAutostart,BM_GETCHECK,0,0)==BST_CHECKED;setAutostart();return 0;}
        if(command>=AppBase&&command<AppBase+180) {
            const size_t index=size_t(command-AppBase);if(index>=appNames.size())return 0;
            const auto it=std::find_if(config.selected.begin(),config.selected.end(),[&](const auto& name){return lower(name)==lower(appNames[index]);});
            if(it==config.selected.end())config.selected.push_back(appNames[index]);else config.selected.erase(it);
            save();restartIfRunning(running);syncMainConfig();
        } else if(command>=ReceiverBase&&command<ReceiverBase+100) {
            {std::lock_guard lock(stateMutex);const size_t index=size_t(command-ReceiverBase);if(index>=receivers.size())return 0;config.receiver=receivers[index].ip;config.port=receivers[index].port;}
            save();restartIfRunning(running);syncMainConfig();
        } else if(command>=OutputBase&&command<=OutputBase+2) {config.output=Output(command-OutputBase);save();restartIfRunning(running);syncMainConfig();}
        else if(command>=CaptureBase&&command<=CaptureBase+1) {
            if(command==CaptureBase+1 && MessageBoxW(window,L"端点模式会投射所有输出到虚拟声卡的应用，勾选列表暂不生效。请确认声卡上只有你需要的声音。",L"切换诊断采集方式",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return 0;
            config.capture=CaptureMode(command-CaptureBase);save();restartIfRunning(running);syncMainConfig();
        } else switch(command) {
            case Connect: if(!lParam || applyMain(false))engine->start(config);break;
            case Disconnect:stop();break;
            case Reset:stop();engine->start(config);logLine("User reset audio chain");break;
            case Volume:showVolume();break;
            case Settings:showSettings();break;
            case Refresh:refreshDiscovery();refreshMainReceivers();break;
            case Log:ShellExecuteW(window,L"open",(dataDir/L"HomePodTray.log").c_str(),nullptr,nullptr,SW_SHOW);break;
            case Autostart:autostart=!autostart;setAutostart();if(mainAutostart)SendMessageW(mainAutostart,BM_SETCHECK,autostart?BST_CHECKED:BST_UNCHECKED,0);break;
            case Shortcut:shortcut();break;
            case ShowMain:showMain();break;
            case Quit:shutdown=true;stop();if(discoveryThread.joinable())discoveryThread.join();DestroyWindow(window);break;
        }
        return 0;
    }
    if(message==WM_DESTROY && hwnd==window){NOTIFYICONDATAW icon{};icon.cbSize=sizeof(icon);icon.hWnd=window;icon.uID=1;Shell_NotifyIconW(NIM_DELETE,&icon);PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int) {
    SetProcessDPIAware();
    HANDLE single=CreateMutexW(nullptr,FALSE,L"Local\\HomePodTray-Rebuild-20261008");
    if(!single || GetLastError()==ERROR_ALREADY_EXISTS){
        if(HWND existing=FindWindowW(L"HomePodTrayWindow",L"HomePod 投射新版")){ShowWindow(existing,SW_SHOWNORMAL);SetForegroundWindow(existing);}
        if(single)CloseHandle(single);return 0;
    }
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com)){CloseHandle(single);return 1;}
    WSADATA winsock{};if(WSAStartup(MAKEWORD(2,2),&winsock)){CoUninitialize();CloseHandle(single);return 1;}
    try {
        PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))throw std::runtime_error("Cannot resolve LocalAppData");
        dataDir=std::filesystem::path(local)/L"HomePodTray-Rebuild";CoTaskMemFree(local);
        std::filesystem::create_directories(dataDir);load();logLine("HomePodTray Rebuild started; manual connect; isolated profile");
        engine=std::make_unique<AudioEngine>(logLine,[](const Status& value){std::lock_guard lock(stateMutex);status=value;});
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"HomePodTrayWindow";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_HOMEPOD));
        RegisterClassW(&cls);
        const double scale=double(GetDpiForSystem())/96.;
        const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
        RECT rectangle{0,0,int(760*scale+.5),int(690*scale+.5)};AdjustWindowRectEx(&rectangle,style,FALSE,WS_EX_CONTROLPARENT);
        window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"HomePod 投射新版",style,CW_USEDEFAULT,CW_USEDEFAULT,
            rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,instance,nullptr);
        if(!window)throw std::runtime_error("Cannot create tray window");
        createMainControls();ShowWindow(window,SW_SHOW);UpdateWindow(window);
        SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_HOMEPOD),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));
        // Used only when replacing a running connected build; normal startup stays manual.
        if(commandLine && std::wstring(commandLine)==L"--resume-session")engine->start(config);
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");updateTray(true);SetTimer(window,1,500,nullptr);
        MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
            if(message.message==WM_KEYDOWN && message.wParam==VK_ESCAPE && GetAncestor(message.hwnd,GA_ROOT)==window){ShowWindow(window,SW_HIDE);continue;}
            if(settingsWindow&&IsDialogMessageW(settingsWindow,&message))continue;
            if(volumeWindow&&IsDialogMessageW(volumeWindow,&message))continue;
            if(IsWindowVisible(window)&&IsDialogMessageW(window,&message))continue;
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        engine.reset();
    } catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"HomePod 投射新版启动失败",MB_ICONERROR);}
    if(discoveryThread.joinable())discoveryThread.join();if(mainFont)DeleteObject(mainFont);WSACleanup();CoUninitialize();CloseHandle(single);return 0;
}
