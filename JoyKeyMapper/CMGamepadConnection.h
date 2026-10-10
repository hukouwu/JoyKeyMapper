#pragma once

#define WIN32_LEAN_AND_MEAN



#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")

#include "IConnection.h"
#include "EvState.h"

class CMGamepadConnection : public IConnection {
public:
    static constexpr const char* DEVICE_NAME = "CMGAMEPAD";
private:
    std::string _name;
    std::string _aliasName;
    unsigned long long _macAddress;
    std::string _nmMac;
    bool _dataView = true;
    int _offSignBase;

    std::vector<uint8_t> lastData;

    SOCKET m_socket = INVALID_SOCKET;
    bool m_isRunning = false;
    std::vector<uint8_t> m_frameBuffer;
    uint32_t oriStickData;
    std::map<std::string, std::string> vdata;
    int obstMod = 0;

    std::function<void(const EvState&)> m_onStateChanged;
    std::function<void(bool)> m_onConnectionChanged;

    // 数据协议部分
    static constexpr uint8_t HEADER_1 = 0xFF;
    static constexpr uint8_t HEADER_2 = 0x09;
    static constexpr uint8_t HEADER_3 = 0x82;
    static constexpr uint8_t TAIL = 0x55;
    static constexpr int FRAME_SIZE = 12;

    // 每个字节段的按键数量 (8 bits)
    static constexpr size_t BITS_PER_BYTE = 8;
    inline static std::vector<std::string> btns = {
        "LB",
        "LT",
        "UP",
        "DOWN",
        "LEFT",
        "RIGHT",
        "RB",
        "RT",
        "A",
        "B",
        "X",
        "Y",
        "GAMEKEY",
        "SELECT",
        "START",
        "POWER",
        "LSL",
        "LSR",
        "LSU",
        "LSD",
        "RSL",
        "RSR",
        "RSU",
        "RSD"
    };

    std::map<std::string, RangeEntry> stkRange;

    static int findFrameStart(const std::vector<uint8_t>& buffer, int offset = 0) {
        for (size_t i = offset; i < buffer.size() - 2; ++i) {
            if (buffer[i] == HEADER_1 && buffer[i + 1] == HEADER_2 && buffer[i + 2] == HEADER_3) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    static bool validateChecksum(const uint8_t* data) {
        int sum = 0;
        for (int i = 2; i < 10; ++i) sum += data[i] & 0xFF;
        uint8_t expected = static_cast<uint8_t>((4096 - (sum & 0xFF)) & 0xFF);
        return expected == (data[10] & 0xFF);
    }

    static bool validateFrame(const uint8_t* data) {
        if (data[0] != HEADER_1) return false;
        if (data[1] != HEADER_2) return false;
        if (data[2] != HEADER_3) return false;
        if (data[11] != TAIL) return false;
        return validateChecksum(data);
    }

    int offSignBase(int off) override {
        _offSignBase = off;
        return off + btns.size();
    }
    bool parse(const uint8_t* data, EvState& state) {
        if (!validateFrame(data)) return false;

        vdata.clear();
        int btnSignCode = (data[4] << 8) | data[3];
        int32_t stickData = (data[8] << 24) | (data[7] << 16) | (data[6] << 8) | data[5];
        
        std::ostringstream ossBtn;
        ossBtn << "0x" << std::hex << std::uppercase << btnSignCode;
        vdata["button"] = ossBtn.str();

        std::ostringstream ossStick;
        ossStick << "0x" << std::hex << std::uppercase << stickData;
        vdata["stick"] = ossStick.str();

        if (btnSignCode != 0) {
            std::cout << "signCode " << std::hex << btnSignCode << std::endl;
        }
        state.btnVal[_aliasName + ".stick"] = std::format("0x{:08X}", stickData);;
        // 按钮检测
        parseDownBtn(btnSignCode, state);
        // 摇杆
        parseDownStick(stickData, state);
        // 释放的按键
        parseUpBtn(state);

        state.IsCharging = (data[9] == 0xFF);
        state.Battery = state.IsCharging ? 100 : (data[9] & 0xFF);
        return true;
    }
    void parseDownBtn(uint32_t signCode, EvState& state) {
        int bitCode = 1;
        SignCode sign = (1 << _offSignBase);
        for (int i = 0; i < 16; i++) {
            std::string& btn = btns[i];
            if ((signCode & bitCode) != 0) {
                state.dsbKvs[btn] = 1;
                state.dsbKvs[btn + "@" + _aliasName] = state.dsbKvs[btn];
                state.dsignCode |= sign;
                vdata[btn] = "1";
            }
            sign <<= 1;
            bitCode <<= 1;
        }
    }
    void parseDownStick(uint32_t stickData, EvState& state) {
        if (stickData == oriStickData) {
            return;
        }

        uint8_t byteStick;
        SignCode sign = 0x010000;
        for (int i = 16; i < btns.size(); i++) {
            std::string& btn = btns[i];
            byteStick = static_cast<uint8_t>(stickData);
            const RangeEntry range = stkRange[btn];
            if (range.min <= byteStick && byteStick <= range.max) {
                state.dsbKvs[btn] = (byteStick * range.fixRate + range.fixDate) * range.scale;
                state.dsbKvs[btn + "@" + _aliasName] = state.dsbKvs[btn];
                state.dsignCode |= sign;
                vdata[btn] = std::to_string(state.dsbKvs[btn]);
            }
            if (i % 2 == 1) {
                stickData >>= 8;
            }
            sign <<= 1;
        }
    }
    void parseUpBtn(EvState& state) {
        SignCode signCode = state.dsignCode;
        SignCode lastdSignCode = state.lastdSignCode;

        SignCode diffSignCode = signCode ^ lastdSignCode;
        if ((diffSignCode & signCode) != 0) {
            state.setDownType();
        }
        SignCode upSignCode = diffSignCode & lastdSignCode;
        if (upSignCode != 0) {
            state.setUpType();
        }

        SignCode sign = 1;
        for (int i = 0; i < btns.size(); i++) {

            if ((diffSignCode & sign) != 0) {
                if ((upSignCode & sign) != 0) {
                    std::string btn = btns[i] + "-";
                    state.usbKvs[btn] = 1;
                    state.usignCode |= sign;
                    vdata[btn] = "-1";
                }
                else {
                    std::string btn = btns[i];
                    state.nsbKvs.push_back(btn + "@" + _aliasName);
                }
            }
            sign <<= 1;
        }
    }
    // 通过signCode查找按键
    void seekSign(SignCode dsignCode, SignCode usignCode,
        std::map<std::string, int>& sbKvs, const EvState& state) {
        SignCode sign = 1;
        for (int i = 0; i < btns.size(); i++) {
            if ((dsignCode & sign) != 0) {
                std::string& btn = btns[i];
                auto it = state.dsbKvs.find(btn);
                if (it != state.dsbKvs.end()) {
                    sbKvs[btn] = it->second;
                }
            }
            sign <<= 1;
        }
        sign = 1;
        for (int i = 0; i < btns.size(); i++) {
            if ((usignCode & sign) != 0) {
                std::string btn = btns[i] + "-";
                auto it = state.usbKvs.find(btn);
                if (it != state.usbKvs.end()) {
                    sbKvs[btn] = it->second;
                }
            }
            sign <<= 1;
        }
    }
    void indexSign(const std::set<std::string>& nameSet, SignCode& retDSign, SignCode& retUSign) override {
        

        for (auto name : nameSet) {
            SignCode sign = (1 << _offSignBase);
            bool found = false;
            for (int i = 0; i < btns.size(); i++) {
                std::string& btn = btns[i];
                if (btn == name) {
                    retDSign |= sign;
                    found = true;
                    break;
                }
                sign <<= 1;
            }
            if (found) continue;
            sign = 1;
            for (int i = 0; i < btns.size(); i++) {
                std::string btn = btns[i] + "-";
                if (btn == name) {
                    retUSign |= sign;
                    found = true;
                    break;
                }
                sign <<= 1;
            }

        }
    }

public:
    ~CMGamepadConnection() {
        devDisconnect("", 0);
    }
    std::string getAliasNm() override {
        return _aliasName;
    }
    std::string getNmMac() override {
        return _nmMac;
    }
    unsigned long long getMac() override {
        return _macAddress;
    }
    void reloadConf(std::map<std::string, std::map<std::string, std::string>>& confs) override {
        std::map<std::string, std::map<std::string, std::string>> defConfs;
        getDefConf(defConfs);
        std::map<std::string, std::string>& defConf = defConfs[_nmMac];

        std::map<std::string, std::string>& conf = confs[_nmMac];
        _aliasName = conf[_nmMac];

        auto it = conf.find("obstMod");
        if (it != conf.end() && !it->second.empty()) {
            try {
                obstMod = std::stoi(it->second);
            }
            catch (...) {
                obstMod = 0;
            }
        }
        else {
            obstMod = 0;
        }

        stkRange["LSL"] = parseRangeConfig(conf["LSL"], defConf["LSL"]);
        stkRange["LSR"] = parseRangeConfig(conf["LSR"], defConf["LSR"]);
        stkRange["LSU"] = parseRangeConfig(conf["LSU"], defConf["LSU"]);
        stkRange["LSD"] = parseRangeConfig(conf["LSD"], defConf["LSD"]);
        stkRange["RSL"] = parseRangeConfig(conf["RSL"], defConf["RSL"]);
        stkRange["RSR"] = parseRangeConfig(conf["RSR"], defConf["RSR"]);
        stkRange["RSU"] = parseRangeConfig(conf["RSU"], defConf["RSU"]);
        stkRange["RSD"] = parseRangeConfig(conf["RSD"], defConf["RSD"]);
    }
    void getDefConf(std::map<std::string, std::map<std::string, std::string>>& defConfs) override {
        std::map<std::string, std::string> defConf;
        defConf[_nmMac] = "0";
        defConf["obstMod"] = "0";
        defConf["LSL"] = "0,127,-1,128";
        defConf["LSR"] = "129,255,1,-128";
        defConf["LSU"] = "0,127,-1,128";
        defConf["LSD"] = "129,255,1,-128";
        defConf["RSL"] = "0,127,-1,128";
        defConf["RSR"] = "129,255,1,-128";
        defConf["RSU"] = "0,127,-1,128";
        defConf["RSD"] = "129,255,1,-128";
        defConfs[_nmMac] = defConf;
    }
    std::string getDefRule(std::string name, unsigned long long macAddress) override {

        return
R"xxx(
#左键
{LT}→MOUSE_LBT
#右键
{LB-}→MOUSE_RBT
#鼠标移动
{LSR}||{LSL}||{LSD}||{LSU}→MOUSE_MOV( ({LSR}+{LSL})>200?({LSR}-{LSL})/2:({LSR}-{LSL})/20, ({LSD}+{LSU})>200?({LSD}-{LSU})/2:({LSD}-{LSU})/20 )
{LB}&&{LSR}→MOUSE_HRO(120)
{LB}&&{LSL}→MOUSE_HRO(-120)
{LB}&&{LSD}→MOUSE_VRO(-120)
{LB}&&{LSU}→MOUSE_VRO(120)
{LT}&&({LSR}||{LSL}||{LSD}||{LSU})→MOUSE_LBT:(1000,{LT-})+MOUSE_MOV( ({LSR}-{LSL})/15, ({LSD}-{LSU})/15 )
)xxx";

    }
    std::vector<std::string> getDevBtns(int cnt) override {
        return btns;
    }
    std::string devChooseNmMac(std::string name, unsigned long long macAddress) override {
        return name + "_" + std::to_string(macAddress);
    }
    bool setDataView(std::string name, unsigned long long macAddress) override {
        _dataView = !_dataView;
        return _dataView;
    }
    void setOnStateChanged(std::function<void(const EvState&)> callback) override {
        m_onStateChanged = callback;
    }
    void setOnConnectionChanged(std::function<void(bool)> callback) override {
        m_onConnectionChanged = callback;
    }

    bool devConnect(std::string name, unsigned long long macAddress, std::wstring devId) override {
        _name = name;
        _macAddress = macAddress;
        _nmMac = _name + "_" + std::to_string(_macAddress);

        SimpleLogger::LOG_INFO("CMGamepadConnection.devConnect");
        BLUETOOTH_DEVICE_INFO deviceInfo;
        ZeroMemory(&deviceInfo, sizeof(BLUETOOTH_DEVICE_INFO));
        deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);
        deviceInfo.Address.ullLong = macAddress;
        std::wstring wName(name.begin(), name.end());
        wcsncpy_s(deviceInfo.szName, wName.c_str(), BTH_MAX_NAME_SIZE);

        SimpleLogger::LOG_INFO("bef CMGamepadConnection.tryDirectRfcomm");
        if (tryDirectRfcomm(deviceInfo)) {
            SimpleLogger::LOG_INFO("aft CMGamepadConnection.tryDirectRfcomm");
            readFirst();
            return true;
        }

        return false;
    }

    void devDisconnect(std::string name, unsigned long long macAddress) override {
        m_isRunning = false;

        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }

        m_frameBuffer.clear();
    }

private:
    

    bool tryDirectRfcomm(const BLUETOOTH_DEVICE_INFO& deviceInfo) {
        SimpleLogger::LOG_INFO("ent CMGamepadConnection.tryDirectRfcomm");
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            SimpleLogger::LOG_INFO("CMGamepadConnection.tryDirectRfcomm WSAStartup return");
            return false;
        }

        for (int channel = 1; channel <= 30; ++channel) {
            SimpleLogger::LOG_INFO("CMGamepadConnection.tryDirectRfcomm bef socket");
            m_socket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
            if (m_socket == INVALID_SOCKET) continue;

            SOCKADDR_BTH sockAddr = { 0 };
            sockAddr.addressFamily = AF_BTH;
            sockAddr.btAddr = deviceInfo.Address.ullLong;
            sockAddr.port = static_cast<USHORT>(channel);

            SimpleLogger::LOG_INFO("CMGamepadConnection.tryDirectRfcomm 尝试RFCOMM通道 {}", channel);

            if (connect(m_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr)) == 0) {
                SimpleLogger::LOG_INFO("CMGamepadConnection.tryDirectRfcomm connect sucess");
                if (m_onConnectionChanged) {
                    SimpleLogger::LOG_INFO("CMGamepadConnection.tryDirectRfcomm m_onConnectionChanged");
                    m_onConnectionChanged(true);
                }

                return true;
            }
            else {
                int errCode = WSAGetLastError();
                std::string errMsg = "";
                switch (errCode) {
                case WSAENETUNREACH:
                    errMsg = "NETUNREACH(设备可能未配对或服务未启动)";
                    break;
                case WSAETIMEDOUT:
                    errMsg = "TIMEDOUT(信号弱或设备无响应)";
                    break;
                case WSAECONNREFUSED:
                    errMsg = "CONNREFUSED(通道占用或服务拒绝)";
                    break;
                case WSAEADDRNOTAVAIL:
                    errMsg = "ADDRNOTAVAIL(MAC地址无效)";
                    break;
                default:
                    errMsg = "UNKNOWN_ERROR";
                    break;
                }
                SimpleLogger::LOG_INFO("CMGamepadConnection.tryDirectRfcomm connect failed on channel {}: Error Code {}， Error Message {}", channel, errCode, errMsg);

            }

            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }

        WSACleanup();
        return false;
    }

    bool readFirst() {
        SimpleLogger::LOG_INFO("CMGamepadConnection.readFirst");
        uint8_t buffer[256];

        // 设置非阻塞模式
        unsigned long mode = 1; // 0 为阻塞模式，1 为非阻塞模式
        ioctlsocket(m_socket, FIONBIO, &mode);
        int bytesRead = recv(m_socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        int error = WSAGetLastError();

        if (bytesRead > 0) {
            m_frameBuffer.insert(m_frameBuffer.end(), buffer, buffer + bytesRead);

            int startIndex = findFrameStart(m_frameBuffer);
            if (startIndex == -1) {
                SimpleLogger::LOG_INFO("CMGamepadConnection.readFirst 未找到帧头，清空缓冲区");
                m_frameBuffer.clear();
                return false;
            }
            if (startIndex > 0) {
                SimpleLogger::LOG_INFO("CMGamepadConnection.readFirst 移除{}字节垃圾数据", startIndex);
                m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.begin() + startIndex);
            }

            if (m_frameBuffer.size() < FRAME_SIZE) {
                SimpleLogger::LOG_INFO("CMGamepadConnection.readFirst 数据不足，等待更多数据");
                return false;
            }
            uint8_t frame[FRAME_SIZE];
            std::copy(m_frameBuffer.begin(), m_frameBuffer.begin() + FRAME_SIZE, frame);
            m_frameBuffer.clear();

            oriStickData = (frame[8] << 24) | (frame[7] << 16) | (frame[6] << 8) | frame[5];
        }
        else if (bytesRead == 0) {
            SimpleLogger::LOG_INFO("CMGamepadConnection.readFirst 连接断开（远程关闭）");
            return false;
        }
        else if (error != WSAEWOULDBLOCK) {
            SimpleLogger::LOG_INFO("CMGamepadConnection.readFirst recv失败: {}", error);
            return false;
        }

        return true;
    }

    int readLoop(EvState& state) override {
        uint8_t buffer[256];

        // 设置非阻塞模式
        unsigned long mode = obstMod; // 0 为阻塞模式，1 为非阻塞模式
        ioctlsocket(m_socket, FIONBIO, &mode);
        int bytesRead = recv(m_socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
        int error = WSAGetLastError();

        if (bytesRead > 0) {
            //LOG_INFO("读取到数据");
            processData(buffer, bytesRead, state);
            return 1;
        }
        else if (bytesRead == 0) {
            SimpleLogger::LOG_INFO("CMGamepadConnection.readLoop 数据为空");
            return 0;
        }
        else if (error == WSAEWOULDBLOCK) {
            //LOG_INFO("CMGamepadConnection.readLoop 没有数据");
            return 2;
        }
        else {
            SimpleLogger::LOG_INFO("CMGamepadConnection.readLoop 获取失败");
            return -1;
        }

    }

    void processData(const uint8_t* data, int length, EvState& state) {
        m_frameBuffer.insert(m_frameBuffer.end(), data, data + length);

        while (m_frameBuffer.size() >= FRAME_SIZE) {
            int startIndex = findFrameStart(m_frameBuffer);
            if (startIndex == -1) {
                std::cerr << "[警告] 未找到帧头，清空缓冲区" << std::endl;
                m_frameBuffer.clear();
                break;
            }

            if (startIndex > 0) {
                std::cout << "[调试] 移除 " << startIndex << " 字节垃圾数据" << std::endl;
                m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.begin() + startIndex);
            }

            if (m_frameBuffer.size() < FRAME_SIZE) {
                std::cout << "[调试] 数据不足，等待更多数据" << std::endl;
                break;
            }

            uint8_t frame[FRAME_SIZE];
            std::copy(m_frameBuffer.begin(), m_frameBuffer.begin() + FRAME_SIZE, frame);
            m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.begin() + FRAME_SIZE);

            if (parse(frame, state)) {
                if (_dataView) {
                    std::vector<uint8_t> currentFrame(frame, frame + FRAME_SIZE);
                    DebugHexView::ViewEntry viewEntry;
                    viewEntry.currdata = currentFrame;
                    viewEntry.lastData = lastData;
                    viewEntry.vdata = vdata;
                    _dataView = DebugHexView::GetInstance().show(viewEntry);
                    lastData = currentFrame;
                    return;
                }

                /*if (m_onStateChanged) {
                    m_onStateChanged(state);
                }*/
            }
            else {
                std::cerr << "[警告] 帧解析失败" << std::endl;
            }
        }

        if (m_frameBuffer.size() > 512) {
            std::cerr << "[警告] 缓冲区过大，清空" << std::endl;
            m_frameBuffer.clear();
        }
    }

};


static bool autoRegister1 = []() {
    IConnection::m_drivers.push_back({
        CMGamepadConnection::DEVICE_NAME,
        [](const std::string& name) {
            return name.find(CMGamepadConnection::DEVICE_NAME) != std::string::npos;
        },
        []() -> std::unique_ptr<IConnection> {
            return std::make_unique<CMGamepadConnection>();
        }
        });

    std::cout << "[信息] 自动注册 " << CMGamepadConnection::DEVICE_NAME << " 驱动" << std::endl;
    return true;
    }();

