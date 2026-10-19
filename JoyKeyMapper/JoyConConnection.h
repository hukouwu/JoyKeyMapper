#pragma once

#define WIN32_LEAN_AND_MEAN


#include <hidsdi.h>
#include <setupapi.h>
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")

#include "IConnection.h"
#include "EvState.h"

class JoyConConnection : public IConnection {
public:
    static void registerSelf() {
        g_deviceRegistrars.push_back([]() -> bool {
            SimpleLogger::LOG_INFO("JoyConConnection autoRegister");
            GUID hidGuid;
            HidD_GetHidGuid(&hidGuid);

            HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (deviceInfoSet == INVALID_HANDLE_VALUE) {
                SimpleLogger::LOG_INFO("JoyConConnection autoRegister failed. Error: {}", GetLastError());
                return false;
            }

            SP_DEVINFO_DATA deviceInfoData;
            deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
            for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); ++i) {

                SP_DEVICE_INTERFACE_DATA interfaceData;
                interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
                for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, &deviceInfoData, &hidGuid, i, &interfaceData); ++i) {
                    DWORD requiredSize = 0;
                    SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, NULL, 0, &requiredSize, NULL);

                    std::vector<BYTE> detailBuffer(requiredSize);
                    PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)detailBuffer.data();
                    detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                    if (!SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, detailData, requiredSize, NULL, NULL)) {
                        SimpleLogger::LOG_ERROR("JoyConConnection autoRegister SetupDiGetDeviceInterfaceDetail failed for index {}", i);
                        continue;
                    }

                    SimpleLogger::LOG_INFO("尝试打开设备路径: {}", WStringToUTF8(detailData->DevicePath));
                    // 打开句柄
                    HANDLE hDevice = CreateFile(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                    if (hDevice == INVALID_HANDLE_VALUE) {
                        SimpleLogger::LOG_ERROR("JoyConConnection autoRegister CreateFile failed for index {}. Error: {}", i, GetLastError());
                        continue;
                    }

                    // 检查是否是 Nintendo 设备
                    HIDD_ATTRIBUTES attributes;
                    attributes.Size = sizeof(HIDD_ATTRIBUTES);
                    if (!HidD_GetAttributes(hDevice, &attributes)) {
                        SimpleLogger::LOG_ERROR("JoyConConnection autoRegister HidD_GetAttributes failed. Closing handle.");
                        CloseHandle(hDevice);
                        continue;
                    }

                    SimpleLogger::LOG_INFO("JoyConConnection autoRegister Found HID Device: VID=0x{:04X}, PID=0x{:04X}", attributes.VendorID, attributes.ProductID);
                    if (attributes.VendorID == 0x057E && (attributes.ProductID == 0x2006 || attributes.ProductID == 0x2007 || attributes.ProductID == 0x200E)) {
                        std::wstring wDevicePath = detailData->DevicePath;
                        std::string instanceId = "";
                        size_t lastHash = wDevicePath.find_last_of(L'#');
                        size_t secondLastHash = wDevicePath.find_last_of(L'#', lastHash - 1);
                        std::wstring instW = wDevicePath.substr(secondLastHash + 1, lastHash - secondLastHash - 1);
                        instanceId = WStringToUTF8(instW);
                        std::hash<std::string> hasher;
                        unsigned long long hmac = hasher(instanceId);
                        
                        auto it = std::find_if(IConnection::m_drivers.begin(), IConnection::m_drivers.end(),
                            [&](const IConnection::DriverInfo& driver) {
                                return driver.mac == hmac;
                            });
                        if (it != IConnection::m_drivers.end()) {
                            it->fresh = true;
                        }
                        else {
                            std::string deviceName = "Joy-Con";

                            WCHAR friendlyName[256] = { 0 };
                            DWORD dataType;
                            DWORD requiredSize;
                            if (SetupDiGetDeviceRegistryPropertyW(
                                deviceInfoSet,
                                &deviceInfoData,
                                SPDRP_FRIENDLYNAME,
                                &dataType,
                                (PBYTE)friendlyName,
                                sizeof(friendlyName),
                                &requiredSize)) {
                                deviceName = WStringToUTF8(friendlyName);
                            }
                            else {
                                if (attributes.ProductID == 0x2006) {
                                    deviceName = "Joy-Con(L)";
                                }
                                else if (attributes.ProductID == 0x2007) {
                                    deviceName = "Joy-Con(R)";
                                }
                                else if (attributes.ProductID == 0x200E) {
                                    deviceName = "Joy-Con(Pair)";
                                }
                            }
                            SimpleLogger::LOG_INFO("JoyConConnection autoRegister Found HID Device Name: {}", deviceName);

                            IConnection::DriverInfo driverInfo;
                            if (attributes.ProductID == 0x2006) {
                                driverInfo.att = 1;
                            }
                            else if (attributes.ProductID == 0x2007) {
                                driverInfo.att = 2;
                            }
                            else if (attributes.ProductID == 0x200E) {
                                driverInfo.att = 3;
                            }
                            driverInfo.name = deviceName;
                            driverInfo.path = WStringToUTF8(detailData->DevicePath);
                            driverInfo.mac = hmac;
                            SimpleLogger::LOG_INFO("JoyConConnection autoRegister ust Mac: {}", driverInfo.mac);

                            driverInfo.nameMac = driverInfo.name + "_" + std::to_string(driverInfo.mac);

                            driverInfo.factory = []() -> std::unique_ptr<IConnection> {
                                return std::make_unique<JoyConConnection>();
                            };
                            driverInfo.fresh = true;

                            IConnection::m_drivers.push_back(driverInfo);
                        }
                    }

                    CloseHandle(hDevice);
                }
            }

            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            SimpleLogger::LOG_INFO("JoyConConnection autoRegister end.");
            return true;
        });
    }
private:
    std::string _aliasName;
    std::string _nmMac;
    bool _dataView = true;
    int _offSignBase;
    std::wstring _currentDevicePath;

    int _index = -1;
    HANDLE m_hDevice = INVALID_HANDLE_VALUE;
    DWORD m_inputReportLength = 0;
    std::vector<uint8_t> lastData;

    SOCKET m_socket = INVALID_SOCKET;
    bool m_isRunning = false;
    std::vector<uint8_t> m_frameBuffer;
    uint64_t oriStickData;
    std::map<std::string, std::string> vdata;

    std::function<void(const EvState&)> m_onStateChanged;
    std::function<void(bool)> m_onConnectionChanged;

    inline static std::vector<std::string> btns = {
        "DOWN",
        "UP",
        "RIGHT",
        "LEFT",
        "SR_L",
        "SL_L",
        "L",
        "ZL",
        "Y",
        "X",
        "B",
        "A",
        "SR_R",
        "SL_R",
        "R",
        "ZR",
        "MINUS",
        "PLUS",
        "RSB",
        "LSB",
        "HOME",
        "CAPTURE",
        "C1",
        "C2",
        "LSL",
        "LSR",
        "LSU",
        "LSD",
        "RSL",
        "RSR",
        "RSU",
        "RSD"
    };
    inline static std::vector<std::string> lbtns = {
        "DOWN",
        "UP",
        "RIGHT",
        "LEFT",
        "SR_L",
        "SL_L",
        "L",
        "ZL",
        "MINUS",
        "LSB",
        "CAPTURE",
        "C1",
        "LSL",
        "LSR",
        "LSU",
        "LSD"
    };
    inline static std::vector<std::string> rbtns = {
        "Y",
        "X",
        "B",
        "A",
        "SR_R",
        "SL_R",
        "R",
        "ZR",
        "PLUS",
        "RSB",
        "HOME",
        "C2",
        "RSL",
        "RSR",
        "RSU",
        "RSD"
    };

    
    std::map<std::string, RangeEntry> stkRange;


    int offSignBase(int off) override {
        _offSignBase = off;
        return off + btns.size();
    }
    bool parse(const uint8_t* data, EvState& state) {
        vdata.clear();
        int btnSignCode = (data[4] << 16) | (data[3] << 8) | data[5];
        int64_t rightStickData = (data[11] << 16) | (data[10] << 8) | data[9];
        int64_t leftStickData = (data[8] << 16) | (data[7] << 8) | data[6];
        int64_t stickData = (rightStickData << 24) | leftStickData;
        
        std::ostringstream ossBtn;
        ossBtn << "0x" << std::hex << std::uppercase << btnSignCode;
        vdata["button"] = ossBtn.str();

        std::ostringstream ossStick;
        ossStick << "0x" << std::hex << std::uppercase << stickData;
        vdata["stick"] = ossStick.str();
        state.btnVal[_aliasName + ".stick"] = std::format("0x{:016X}", stickData);
        
        // 按钮检测
        parseDownBtn(btnSignCode, state);
        // 摇杆
        parseDownStick(stickData, state);
        // 释放的按键
        parseUpBtn(state);

        /*if (state.signCode != 0) {
            std::cout << "signCode " << std::hex << state.signCode << std::endl;
            std::cout << "，lastSignCode " << std::hex << state.lastSignCode << std::endl;
        }*/

        state.IsCharging = (data[9] == 0xFF);
        state.Battery = state.IsCharging ? 100 : (data[9] & 0xFF);
        return true;
    }
    void parseDownBtn(uint32_t signCode, EvState& state) {
        int bitCode = 1;
        SignCode sign = (1 << _offSignBase);
        for (int i = 0; i < 24; i++) {
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
    void parseDownStick(uint64_t stickData, EvState& state) {
        if (stickData == oriStickData) {
            return;
        }
        uint16_t byteStick;
        SignCode sign = 0x01000000;
        for (int i = 24; i < btns.size(); i++) {
            std::string& btn = btns[i];
            byteStick = static_cast<uint16_t>(stickData) & 0xFFF;
            auto it = stkRange.find(btn);
            if (it != stkRange.end()) {
                const RangeEntry range = it->second;
                if (range.min <= byteStick && byteStick <= range.max) {
                    state.dsbKvs[btn] = (byteStick * range.fixRate + range.fixDate) * range.scale;
                    state.dsbKvs[btn + "@" + _aliasName] = state.dsbKvs[btn];
                    state.dsignCode |= sign;
                    if (_dataView) {
                        vdata[btn] = std::format("{:X}", state.dsbKvs[btn]);
                    }
                }
            }
            if (i % 2 == 1) {
                stickData >>= 12;
            }
            sign <<= 1;
        }
    }
    void parseUpBtn(EvState& state) {
        SignCode signCode = state.dsignCode;
        SignCode lastdSignCode = state.lastdSignCode;

        SignCode diffSignCode = signCode ^ lastdSignCode;
        if ((diffSignCode & state.dsignCode) != 0) {
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
    ~JoyConConnection() {
        devDisconnect(_index);
    }
    std::string getAliasNm() override {
        return _aliasName;
    }
    std::string getNmMac() override {
        return _nmMac;
    }
    int getIndex() override {
        return _index;
    }
    unsigned long long getMac() override {
        return IConnection::m_drivers[_index].mac;
    }
    bool setDataView(int index) override {
        _dataView = !_dataView;
        return _dataView;
    }
    void reloadConf(std::map<std::string, std::map<std::string, std::string>>& confs) override {
        if (_index < 0) {
            return;
        }

        std::map<std::string, std::map<std::string, std::string>> defConfs;
        getDefConf(defConfs, _index);
        std::map<std::string, std::string>& defConf = defConfs[_nmMac];

        std::map<std::string, std::string>& conf = confs[_nmMac];
        _aliasName = conf[_nmMac];
        if (IConnection::m_drivers[_index].att & 1) {
            stkRange["LSL"] = parseRangeConfig(conf["LSL"], defConf["LSL"]);
            stkRange["LSR"] = parseRangeConfig(conf["LSR"], defConf["LSR"]);
            stkRange["LSU"] = parseRangeConfig(conf["LSU"], defConf["LSU"]);
            stkRange["LSD"] = parseRangeConfig(conf["LSD"], defConf["LSD"]);
        }
        if (IConnection::m_drivers[_index].att & 2) {
            stkRange["RSL"] = parseRangeConfig(conf["RSL"], defConf["RSL"]);
            stkRange["RSR"] = parseRangeConfig(conf["RSR"], defConf["RSR"]);
            stkRange["RSU"] = parseRangeConfig(conf["RSU"], defConf["RSU"]);
            stkRange["RSD"] = parseRangeConfig(conf["RSD"], defConf["RSD"]);
        }
        
    }
    void getDefConf(std::map<std::string, std::map<std::string, std::string>>& defConfs, int index) override {
        IConnection::DriverInfo& driInfo = IConnection::m_drivers[index];

        std::map<std::string, std::string> defConf;
        defConf[driInfo.nameMac] = "0";
        if (driInfo.att & 1) {
            defConf["LSL"] = "0,0x7e0,-1,0x800";
            defConf["LSR"] = "0x810,0xff0,1,-0x800";
            defConf["LSU"] = "0x810,0xff0,1,-0x800";
            defConf["LSD"] = "0,0x7e0,-1,0x800";
        }
        if (driInfo.att & 2) {
            defConf["RSL"] = "0,0x7e0,-1,0x800";
            defConf["RSR"] = "0x810,0xff0,1,-0x800";
            defConf["RSU"] = "0x810,0xff0,1,-0x800";
            defConf["RSD"] = "0,0x7e0,-1,0x800";
        }
        defConfs[driInfo.nameMac] = defConf;
    }
    std::string getDefRule(int index) override {
        if (IConnection::m_drivers[index].att & 1) {
            return
                R"xxx(
#左键
{ZL}→MOUSE_LBT
#右键
{L-}→MOUSE_RBT
#鼠标移动
{LSR}||{LSL}||{LSD}||{LSU}→MOUSE_MOV( ({LSR}+{LSL})>1800?({LSR}-{LSL})/50:({LSR}-{LSL})/200, ({LSD}+{LSU})>1800?({LSD}-{LSU})/50:({LSD}-{LSU})/200 )
{L}&&{LSR}→MOUSE_HRO(120)
{L}&&{LSL}→MOUSE_HRO(-120)
{L}&&{LSD}→MOUSE_VRO(-120)
{L}&&{LSU}→MOUSE_VRO(120)
{ZL}&&({LSR}||{LSL}||{LSD}||{LSU})→MOUSE_LBT:(1000,{ZL-})+MOUSE_MOV( ({LSR}-{LSL})/150, ({LSD}-{LSU})/150 )
)xxx";
        }

        if (IConnection::m_drivers[index].att & 2) {
            return
                R"xxx(
#左键
{ZR}→MOUSE_LBT
#右键
{R-}→MOUSE_RBT
#鼠标移动
{RSR}||{RSL}||{RSD}||{RSU}→MOUSE_MOV( ({RSR}+{RSL})>1800?({RSR}-{RSL})/50:({RSR}-{RSL})/200, ({RSD}+{RSU})>1800?({RSD}-{RSU})/50:({RSD}-{RSU})/200 )
{R}&&{RSR}→MOUSE_HRO(120)
{R}&&{RSL}→MOUSE_HRO(-120)
{R}&&{RSD}→MOUSE_VRO(-120)
{R}&&{RSU}→MOUSE_VRO(120)
{ZR}&&({RSR}||{RSL}||{RSD}||{RSU})→MOUSE_LBT:(1000,{ZR-})+MOUSE_MOV( ({RSR}-{RSL})/150, ({RSD}-{RSU})/150 )
)xxx";
        }
    }

    std::vector<std::string> getDevBtns(int index) override {
        if (IConnection::m_drivers[index].att == 1) {
            return lbtns;
        }
        if (IConnection::m_drivers[index].att == 2) {
            return rbtns;
        }
        return btns;
    }
    void setOnStateChanged(std::function<void(const EvState&)> callback) override {
        m_onStateChanged = callback;
    }
    void setOnConnectionChanged(std::function<void(bool)> callback) override {
        m_onConnectionChanged = callback;
    }

    HANDLE openJoyConHandle() {
        // 打开句柄
        HANDLE hDevice = CreateFile(UTF8ToWString(IConnection::m_drivers[_index].path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            
        HIDD_ATTRIBUTES attributes;
        attributes.Size = sizeof(HIDD_ATTRIBUTES);

        // 获取 HID 预解析数据
        PHIDP_PREPARSED_DATA PreparsedData = NULL;
        if (HidD_GetPreparsedData(hDevice, &PreparsedData)) {
            HIDP_CAPS Caps;
            memset(&Caps, 0, sizeof(Caps));
            if (HidP_GetCaps(PreparsedData, &Caps) == HIDP_STATUS_SUCCESS) {
                // 保存输入报告的长度
                m_inputReportLength = Caps.InputReportByteLength;
                SimpleLogger::LOG_INFO("JoyConConnection.openJoyConHandle Input Report Length: {} bytes", m_inputReportLength);
            }
            else {
                SimpleLogger::LOG_INFO("JoyConConnection.openJoyConHandle HidP_GetCaps failed");
            }
            HidD_FreePreparsedData(PreparsedData);
        }
        else {
            SimpleLogger::LOG_INFO("JoyConConnection.openJoyConHandle HidD_GetPreparsedData failed");
            CloseHandle(hDevice);
            return INVALID_HANDLE_VALUE;
        }

        
        return hDevice;
    }

    bool devConnect(int index) override {
        _index = index;
        _nmMac = IConnection::m_drivers[_index].name + "_" + std::to_string(IConnection::m_drivers[_index].mac);

        SimpleLogger::LOG_INFO("JoyConConnection.devConnect");
        m_hDevice = openJoyConHandle();
        if (m_hDevice == INVALID_HANDLE_VALUE) {
            return false;
        }

        SimpleLogger::LOG_INFO("JoyConConnection.devConnect end");
        m_isRunning = readFirst();

        if (m_onConnectionChanged) {
            m_onConnectionChanged(true);
        }

        return true;
    }

    bool devDisconnect(int index) override {
        if (_index != index) {
            return false;
        }
        m_isRunning = false;

        if (m_hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hDevice);
            m_hDevice = INVALID_HANDLE_VALUE;
        }

        if (!_currentDevicePath.empty()) {
            _currentDevicePath.clear();
        }

        if (m_onConnectionChanged) {
            m_onConnectionChanged(false);
        }
        return true;
    }

private:
    bool readFirst() {
        if (m_inputReportLength == 0) {
            return false;
        }

        std::vector<uint8_t> buffer(m_inputReportLength + 10);
        DWORD bytesRead = 0;
        BOOL success = ReadFile(m_hDevice, buffer.data(), m_inputReportLength, &bytesRead, NULL);

        if (!success) {
            DWORD err = GetLastError();
            char msg[128];
            sprintf_s(msg, "[错误] ReadFile 失败, Error: %lu\n", err);
            OutputDebugStringA(msg);

            if (err == 995) return false; // 正常断开
            return false;
        }

        if (bytesRead != m_inputReportLength) {
            return false;
        }

        uint8_t* data = buffer.data();
        oriStickData = (data[11] << 40) | (data[10] << 32) | (data[9] << 24) | (data[8] << 16) | (data[7] << 8) | data[6];
        return true;
    }
    
    int readLoop(EvState& state) override {
        std::vector<uint8_t> buffer(m_inputReportLength + 10);
        DWORD bytesRead = 0;
        BOOL success = ReadFile(m_hDevice, buffer.data(), m_inputReportLength, &bytesRead, NULL);

        if (!success) {
            DWORD err = GetLastError();
            char msg[128];
            SimpleLogger::LOG_ERROR("JoyConConnection.readLoop Error: {}", err);

            if (err == 995) return -1;
            m_isRunning = false;
            return -1;
        }

        if (bytesRead != m_inputReportLength) {
            return  -1;
        }

        //LOG_INFO("JoyConConnection.readLoop bef parse");
        parse(buffer.data(), state);
        //LOG_INFO("JoyConConnection.readLoop aft parse");
        if (_dataView) {
            std::vector<uint8_t> currentFrame(buffer.begin(), buffer.begin() + bytesRead);
            DebugHexView::ViewEntry viewEntry;
            viewEntry.currdata = currentFrame;
            viewEntry.lastData = lastData;
            viewEntry.vdata = vdata;
            _dataView = DebugHexView::GetInstance().show(viewEntry);
            lastData = currentFrame;
        }

        /*if (m_onStateChanged) {
           m_onStateChanged(state);
        }*/

        return 1;
    }


};


static struct JoyConRegistrarInit {
    JoyConRegistrarInit() {
        JoyConConnection::registerSelf();
    }
} joyConRegInit;
