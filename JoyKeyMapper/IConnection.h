#pragma once
#ifndef _WINSOCKAPI_
#include <winsock2.h>
#include <ws2bth.h>
#include <ws2tcpip.h>
#include <ws2def.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <windows.h>

#include <bluetoothapis.h>
#include <algorithm>
#include <iostream>
#include <thread>
#include <functional>
#include <vector>
#include <string>
#include <cmath>
#include <set>
#include <unordered_set>
#include <iomanip>
#include <cstdint>

#include <string>
#include <functional>
#include "Util.h"
#include "EvState.h"

#include "DebugHexView.h"

// 抽象基类
class IConnection {
public:
    using DeviceMatcher = std::function<bool(const std::string&)>;
    using ConnectionFactory = std::function<std::unique_ptr<IConnection>()>;

    struct DriverInfo {
        std::string name;
        DeviceMatcher matcher;
        ConnectionFactory factory;
    };
    struct RangeEntry {
        int min;
        int max;
        int fixRate;
        int fixDate;
        float scale;
    };

    inline static std::vector<DriverInfo> m_drivers;
public:
    virtual ~IConnection() = default;

    // 连接设备
    virtual bool devConnect(std::string name, unsigned long long macAddress, std::wstring devId) = 0;
    // 断开连接
    virtual void devDisconnect(std::string name, unsigned long long macAddress) = 0;

    // 设置回调
    virtual void setOnStateChanged(std::function<void(const EvState&)> callback) = 0;
    virtual void setOnConnectionChanged(std::function<void(bool)> callback) = 0;
    virtual std::string getNmMac() = 0;
    virtual std::string getAliasNm() = 0;
    virtual unsigned long long getMac() = 0;
    virtual bool setDataView(std::string name, unsigned long long macAddress) = 0;
    virtual std::string devChooseNmMac(std::string name, unsigned long long macAddress) = 0;
    virtual int readLoop(EvState& state) = 0;
    virtual void reloadConf(std::map<std::string, std::map<std::string, std::string>>& confs) = 0;
    virtual void getDefConf(std::map<std::string, std::map<std::string, std::string>>& defConf) = 0;
    virtual std::string getDefRule(std::string name, unsigned long long macAddress) = 0;
    virtual std::vector<std::string> getDevBtns(int cnt) = 0;

    virtual int offSignBase(int off) = 0;
    virtual void indexSign(const std::set<std::string>& nameSet, SignCode& retDSign, SignCode& retUSign) = 0;

    static RangeEntry parseRangeConfig(const std::string& configStr, const std::string& defConfigStr) {
        if (configStr.empty()) {
            return parseRangeConfig(defConfigStr, "0,0,0,0");
        }
        RangeEntry entry = { 0, 0, 0, 0 }; // 默认值
        std::stringstream ss(configStr);
        std::string token;
        int i = 0;

        while (std::getline(ss, token, ',')) {
            token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());

            if (token.empty()) continue;

            try {
                int val = std::stoi(token, nullptr, 0);
                switch (i) {
                case 0: entry.min = val; break;
                case 1: entry.max = val; break;
                case 2: entry.fixRate = val; break;
                case 3: entry.fixDate = val; break;
                }
                i++;
            }
            catch (...) {
                // 解析错误，保持默认值
            }
            if (i >= 4) break;
        }
        float absFixDate = std::abs(entry.fixDate);
        entry.scale = absFixDate / (entry.max - entry.min);
        return entry;
    }


    inline static std::unordered_set<std::wstring> connectedDev;
};
