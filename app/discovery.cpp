// SPDX-License-Identifier: Apache-2.0
#include "discovery.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>
namespace homepod {
namespace {
std::string dnsName(std::span<const unsigned char> bytes,size_t& cursor) {
    std::string name;size_t position=cursor;bool jumped=false;
    for(int hop=0;hop<64;++hop) {
        if(position>=bytes.size())throw std::runtime_error("DNS name bounds");
        const unsigned length=bytes[position++];
        if(length==0){if(!jumped)cursor=position;return name;}
        if((length&0xc0)==0xc0) {
            if(position>=bytes.size())throw std::runtime_error("DNS pointer bounds");
            const size_t target=((length&0x3f)<<8)|bytes[position++];
            if(!jumped)cursor=position;
            jumped=true;position=target;continue;
        }
        if(length>63 || position+length>bytes.size())throw std::runtime_error("DNS label bounds");
        if(!name.empty())name+='.';
        name.append(reinterpret_cast<const char*>(bytes.data()+position),length);position+=length;
    }
    throw std::runtime_error("DNS compression loop");
}
unsigned read16(std::span<const unsigned char> bytes,size_t position) {
    if(position+2>bytes.size())throw std::runtime_error("DNS integer bounds");
    return (unsigned(bytes[position])<<8)|bytes[position+1];
}
struct Service {std::string host,ip;unsigned short port=7000;};
}
std::vector<Receiver> discoverReceivers() {
    SOCKET socketHandle=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(socketHandle==INVALID_SOCKET)throw std::runtime_error("无法创建 mDNS 套接字");
    struct Guard {SOCKET s;~Guard(){closesocket(s);}} guard{socketHandle};
    sockaddr_in local{};local.sin_family=AF_INET;
    if(bind(socketHandle,reinterpret_cast<sockaddr*>(&local),sizeof(local)))throw std::runtime_error("无法绑定 mDNS 套接字");
    std::vector<in_addr> interfaces;
    ULONG size=16384;std::vector<BYTE> adapters(size);
    ULONG result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,
        nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapters.data()),&size);
    if(result==ERROR_BUFFER_OVERFLOW){adapters.resize(size);result=GetAdaptersAddresses(AF_INET,0,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapters.data()),&size);}
    if(result==NO_ERROR)for(auto* a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapters.data());a;a=a->Next) {
        if(a->OperStatus!=IfOperStatusUp || a->IfType==IF_TYPE_SOFTWARE_LOOPBACK)continue;
        for(auto* u=a->FirstUnicastAddress;u;u=u->Next)
            if(u->Address.lpSockaddr->sa_family==AF_INET)interfaces.push_back(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr);
    }
    auto query=[&](const std::string& name,unsigned type) {
        std::string packet(12,'\0');packet[5]=1;
        size_t cursor=0;
        while(cursor<name.size()) {size_t end=name.find('.',cursor);if(end==std::string::npos)end=name.size();packet+=char(end-cursor);packet+=name.substr(cursor,end-cursor);cursor=end+1;}
        packet+='\0';packet+=char(type>>8);packet+=char(type&255);packet+=char(0x80);packet+=char(1); // unicast response
        sockaddr_in destination{};destination.sin_family=AF_INET;destination.sin_port=htons(5353);
        inet_pton(AF_INET,"224.0.0.251",&destination.sin_addr);
        if(interfaces.empty())sendto(socketHandle,packet.data(),int(packet.size()),0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination));
        for(const auto& interfaceAddress:interfaces) {
            setsockopt(socketHandle,IPPROTO_IP,IP_MULTICAST_IF,reinterpret_cast<const char*>(&interfaceAddress),sizeof(interfaceAddress));
            sendto(socketHandle,packet.data(),int(packet.size()),0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination));
        }
    };
    std::map<std::string,Service> services;std::map<std::string,std::string> hosts;
    auto collect=[&](int milliseconds) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        while(std::chrono::steady_clock::now()<deadline) {
            fd_set readable;FD_ZERO(&readable);FD_SET(socketHandle,&readable);timeval timeout{0,100000};
            if(select(0,&readable,nullptr,nullptr,&timeout)<=0)continue;
            unsigned char buffer[9000];sockaddr_in from{};int fromSize=sizeof(from);
            const int received=recvfrom(socketHandle,reinterpret_cast<char*>(buffer),sizeof(buffer),0,reinterpret_cast<sockaddr*>(&from),&fromSize);
            if(received<12)continue;
            try {
                std::span<const unsigned char> bytes(buffer,size_t(received));size_t cursor=12;
                const unsigned questions=read16(bytes,4),records=read16(bytes,6)+read16(bytes,8)+read16(bytes,10);
                for(unsigned i=0;i<questions;++i){dnsName(bytes,cursor);cursor+=4;}
                for(unsigned i=0;i<records;++i) {
                    const auto owner=dnsName(bytes,cursor);const unsigned type=read16(bytes,cursor),length=read16(bytes,cursor+8);cursor+=10;
                    if(cursor+length>bytes.size())break;
                    if(type==12 && (owner=="_raop._tcp.local" || owner=="_airplay._tcp.local")) {
                        size_t position=cursor;const auto instance=dnsName(bytes,position);
                        char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&from.sin_addr,ip,sizeof(ip));
                        services[instance].ip=ip;
                    } else if(type==33 && length>=7) {
                        size_t position=cursor+6;auto& service=services[owner];service.host=dnsName(bytes,position);
                        service.port=static_cast<unsigned short>(read16(bytes,cursor+4));
                    } else if(type==1 && length==4) {
                        char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,bytes.data()+cursor,ip,sizeof(ip));hosts[owner]=ip;
                    }
                    cursor+=length;
                }
            }catch(const std::exception&) {} // Ignore malformed network input; bounded parser.
        }
    };
    for(int round=0;round<2;++round){query("_raop._tcp.local",12);query("_airplay._tcp.local",12);collect(700);}
    std::vector<std::string> instances;for(const auto& [name,service]:services)instances.push_back(name);
    for(const auto& name:instances)query(name,33);collect(700);
    std::set<std::string> requested;for(const auto& [name,service]:services)if(!service.host.empty())requested.insert(service.host);
    for(const auto& name:requested)query(name,1);collect(700);
    std::vector<Receiver> receivers;
    for(const auto& [name,service]:services) {
        if(name.find("._raop._tcp.local")==std::string::npos && name.find("._airplay._tcp.local")==std::string::npos)continue;
        const auto found=hosts.find(service.host);const auto ip=found!=hosts.end()?found->second:service.ip;if(ip.empty())continue;
        std::string label=name;const auto at=label.find('@');if(at!=std::string::npos)label.erase(0,at+1);
        const auto dot=label.find("._");if(dot!=std::string::npos)label.resize(dot);
        auto duplicate=std::find_if(receivers.begin(),receivers.end(),[&](const auto& r){return r.ip==ip;});
        if(duplicate==receivers.end())receivers.push_back({label,ip,service.port});
    }
    return receivers;
}
}
