#include <windows.h>
#include <windowsx.h>

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <tlhelp32.h>
#include <psapi.h>
#include <set>
#include <cctype>
#include <iomanip>

/*
#include <cfgmgr32.h>
#include <setupapi.h>
#include <hidsdi.h>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.HumanInterfaceDevice.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#pragma comment(lib, "runtimeobject.lib")
*/

#include "ConnManage.h"
#include "KeyMapper.h"


#define WM_REFRESH_DEVICE_LIST (WM_USER + 110)

// 全局变量
KeyMapper g_keyMapper;
/*static winrt::Windows::Devices::Enumeration::DeviceWatcher g_bleWatcher{nullptr};*/

ConnManage connManage;
std::string lfFilename;
std::atomic<bool> g_isConnected(false);

#define RC_IDR_HELP 301
// 控件 ID
#define IDC_LIST_DEVICES    1001
#define IDC_COMBO_PROGRAM   1002
#define IDC_EDIT_CONFIG     1003
#define IDC_BTN_SAVE        1004
#define IDC_BTN_DEMO        1005
#define IDC_BTN_REFRESH     1006
#define IDC_LIST_KEYWORDS   1007
#define IDC_LIST_BTNS       1008
#define IDC_LIST_BTNSVAR    1009
#define IDC_LIST_FHS        1010
#define IDC_BTN_HELP        1011
#define IDC_BTN_CONN        1012


struct DeviceInfo {
    std::string name;
    std::wstring displayName;
    std::wstring devId;
    unsigned long long macAddress;
    bool isConnected;
};
struct DevBtnsVarInfo {
    std::string name;
    bool isKey;
    bool isChoosed;
};

std::string g_selectedProgram = "Default";
std::string showBtnCnfName = "▶";

std::vector<DeviceInfo> g_deviceList;
std::vector<std::string> devBtns;
std::vector<DevBtnsVarInfo> devBtnsVar = { {"_ct.", 1, 0}, {showBtnCnfName, 0, 0} };
std::map<std::string, bool> funVars;
std::vector<std::string> fhs = { "+", "-", "*", "/", "<", ">", "=", "!", "?", ":", "&&", "||", "(", ")", ",", ";", "[", "]" , "→", "_delay", "_new" };
std::vector<std::string> keys = KvInput::allKeys;


static HWND mainWnd;
static HWND hListDevices;
static HWND hListKeywords;
static HWND hListFhs;
static HWND hListBtns;
static HWND hListBtnsVar;
static HWND hComboProgram;
static HWND hEditConfig;

std::thread g_sendThread;
static HINSTANCE g_hInst;
void AsyncSendWrapper(std::vector<std::vector<KeyMapper::KeyExpr>> mappedKeys, const EvState state, int ruleInd) {
    // 执行实际的发送逻辑
    g_keyMapper.sendKeyEvent(mappedKeys, state);


}
// 短事件
void ProcessShortEvents(const EvState& state) {
    if (state.dsignCode == 0 && state.usignCode == 0) {
        return;
    }
    
    if (state.ttype == 0) {
        bool flg = g_keyMapper.freshSKeys(state);
        if (!flg && (state.gctTm < state.macIntev || state.macInd < 0)) {
            return;
        }
    }
    auto [mappedKeys, ruleInd] = g_keyMapper.matchSKeys(state);
    if (mappedKeys.size() > 0) {
        g_keyMapper.sendKeyEvent(mappedKeys, state);
    }
}

void showNewBtn(const EvState& state) {
    if (!funVars[showBtnCnfName]) {
        state.debugFlag = false;
        return;
    }
    state.debugFlag = true;

    const std::string& btn = state.nsbKvs[0];
    SimpleLogger::LOG_INFO("stick value: [{}]", mapToString(state.btnVal));
    SimpleLogger::LOG_INFO("down buttons: [{}]。up buttons: [{}]", mapToString(state.dsbKvs), mapToString(state.usbKvs));

    size_t btnpos = btn.find('@');
    std::string prebtn = (btnpos == std::string::npos) ? btn : btn.substr(0, btnpos);

    auto item = std::find_if(devBtns.begin(), devBtns.end(), [&](const std::string& str) {
        size_t pos = str.find('@');
        if (pos != std::string::npos) {
            return str == btn;
        }

        return str == prebtn;
    });
    
    if (item != devBtns.end()) {
        int index = static_cast<int>(std::distance(devBtns.begin(), item));
        int currentSel = SendMessage(hListBtns, LB_GETCURSEL, 0, 0);
        if (currentSel != index) {
            SendMessage(hListBtns, LB_SETCURSEL, index, 0);
            SendMessage(hListBtns, LB_SETTOPINDEX, index, 0);
            InvalidateRect(hListBtns, NULL, FALSE);
        }
    }
}

void handleChangeState(const EvState& state) {
    if (state.nsbKvs.size() > 0) {
        showNewBtn(state);
    }
    state.fresh();

    ProcessShortEvents(state);
}



void loadConfigToEdit(HWND hEdit, const std::string& filename) {
    std::ifstream file(filename);
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        std::wstring wContent = UTF8ToWString(content);
        SetWindowTextW(hEdit, wContent.c_str());
    }
    else {
        SetWindowTextW(hEdit, L"# 配置文件不存在，请在此编辑并保存\n");
    }
}

void saveConfigFromEdit(HWND hEdit, const std::string& filepath, const std::string& program) {
    std::string filename = filepath + "\\" + program + ".conf";

    std::string content = "";
    if (std::filesystem::exists(filename)) {
        int len = GetWindowTextLength(hEdit);
        std::vector<wchar_t> buffer(len + 1);
        GetWindowTextW(hEdit, buffer.data(), len + 1);
        std::wstring ws(buffer.data());

        content = WStringToUTF8(ws);
        content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
    }

    std::istringstream stream(content);
    std::string line;
    std::vector<std::string> rulesline;
    bool confFlg = false;

    std::map<std::string, std::map<std::string, std::string>> confs;
    connManage.getDefConf(confs);

    std::map<std::string, std::map<std::string, std::string>> tmpconfs;
    std::string cnnname = connManage.getAliasNm();
    std::string currentBlockName = cnnname;
    tmpconfs[currentBlockName] = std::map<std::string, std::string>();
    while (std::getline(stream, line)) {
        // 配置开始
        if (!line.empty() && line[0] == '[') {
            confFlg = true;
            line = line.substr(1, line.size());
        }
        bool confFlgTmp = confFlg;
        // 配置结束
        if (!line.empty() && line[line.size() - 1] == ']') {
            line = line.substr(0, line.size() - 1);
            confFlgTmp = false;
        }

        if (confFlg) {
            if (line.length() >= 6 &&
                line.substr(0, 3) == "###" &&
                line.substr(line.length() - 3) == "###") { // 新配置组
                std::string comment = trim(line.substr(3, line.length() - 6));
                currentBlockName = comment;
                if (tmpconfs.find(currentBlockName) == tmpconfs.end()) {
                    tmpconfs[currentBlockName] = std::map<std::string, std::string>();
                }
            }
            else if (!line.empty()) {
                auto [key, value] = parseKeyAndValue(line);
                tmpconfs[currentBlockName][key] = value;
            }
            confFlg = confFlgTmp;
        }
        else {
            rulesline.push_back(line);
        }
    }

    // 合并配置
    {
        std::map<std::string, std::string>& tmpconf = tmpconfs[cnnname];
        std::map<std::string, std::string>& conf = confs[cnnname];
        for (auto item : tmpconf) {
            conf[item.first] = item.second;
        }
    }
    std::vector<std::string> devNms = mysplit(filepath, '\\');
    for (std::string devnm : devNms) {
        std::map<std::string, std::string>& tmpconf = tmpconfs[devnm];
        std::map<std::string, std::string>& conf = confs[devnm];
        for (auto item : tmpconf) {
            conf[item.first] = item.second;
        }
    }

    
    int cnt = 1;
    for (std::string devnm : devNms) {
        std::map<std::string, std::string>& conf = confs[devnm];
        if (conf.find(devnm) != conf.end()) {
            if (isPositiveInteger(conf[devnm])) {
                conf[devnm] = std::to_string(cnt);
            }
        }
        cnt++;
    }

    std::string newContent = "[\n";
    {
        std::map<std::string, std::string>& conf = confs[cnnname];
        for (auto cf : conf) {
            newContent += cf.first + "=" + cf.second + "\n";
        }
    }
    for (std::string devnm : devNms) {
        newContent += "\n### " + devnm + " ###\n";
        std::map<std::string, std::string>& conf = confs[devnm];
        for (auto cf : conf) {
            newContent += cf.first + "=" + cf.second + "\n";
        }
    }
    newContent += "]\n";

    for (const auto& l : rulesline) {
        newContent += l + "\n";
    }

    std::filesystem::create_directories(filepath);

    std::ofstream file(filename);
    if (file.is_open()) {
        file << newContent;
        file.close();
    }

    std::wstring wContent = UTF8ToWString(newContent);
    SetWindowTextW(hEdit, wContent.c_str());
    if (lfFilename == filename) {
        g_keyMapper.loadConfig(lfFilename, connManage);
    }
}

struct WindowInfo {
    std::wstring processName;
    HWND hWnd;
};
std::map<DWORD, std::wstring> getProcessNameMap() {
    std::map<DWORD, std::wstring> pidMap;
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // 创建进程快照
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return pidMap;

    if (Process32First(hSnapshot, &pe32)) {
        do {
            std::wstring exeName(pe32.szExeFile);

            if (exeName.length() > 4) {
                std::wstring suffix = exeName.substr(exeName.length() - 4);
                std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::towlower);
                if (suffix == L".exe") {
                    exeName = exeName.substr(0, exeName.length() - 4);
                }
            }

            pidMap[pe32.th32ProcessID] = exeName;
        } while (Process32Next(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return pidMap;
}

std::string loadStringFromResource(HINSTANCE hInst, int resourceId, const char* resourceType) {
    HRSRC hRes = FindResourceA(hInst, MAKEINTRESOURCEA(resourceId), resourceType);
    if (!hRes) return "Error: Resource not found.";

    HGLOBAL hGlob = LoadResource(hInst, hRes);
    if (!hGlob) return "Error: Could not load resource.";

    LPVOID pRes = LockResource(hGlob);
    if (!pRes) return "Error: Could not lock resource.";

    DWORD size = SizeofResource(hInst, hRes);

    // 复制数据到 std::string
    std::string content(static_cast<char*>(pRes), size);

    return content;
}

// 2. 窗口枚举回调
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* pData = reinterpret_cast<std::pair<std::map<DWORD, std::wstring>*, std::vector<WindowInfo>*>*>(lParam);
    auto& pidMap = *(pData->first);
    auto& windows = *(pData->second);

    // 过滤不可见窗口
    if (!IsWindowVisible(hwnd)) return TRUE;

    // 过滤工具栏/托盘图标窗口
    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    // 过滤无效 PID 和本程序
    if (pid == 0 || pid == GetCurrentProcessId()) return TRUE;

    // 从映射表中查找进程名
    auto it = pidMap.find(pid);
    if (it == pidMap.end()) return TRUE;

    std::wstring procName = it->second;

    windows.push_back({ procName, hwnd });
    return TRUE;
}

void refreshAllVisiblePrograms(HWND hCombo) {
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Default");

    auto pidMap = getProcessNameMap();
    std::vector<WindowInfo> windows;
    auto data = std::make_pair(&pidMap, &windows);
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&data));

    // 去重并添加
    std::set<std::wstring> seenNames;
    seenNames.insert(L"Default");

    for (const auto& win : windows) {
        if (seenNames.find(win.processName) == seenNames.end()) {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)win.processName.c_str());
            seenNames.insert(win.processName);
        }
    }
    SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    /*
    char msgBuf[64];
    sprintf_s(msgBuf, "main message: %u\n", message);
    OutputDebugStringA(msgBuf);
    */

    switch (message) {
    case WM_CREATE: {
        // 创建左侧设备列表
        hListDevices = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER,
            10, 10, 280, 140, hWnd, (HMENU)IDC_LIST_DEVICES, NULL, NULL);

        hComboProgram = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL | WS_BORDER,
            10, 145, 150, 150, hWnd, (HMENU)IDC_COMBO_PROGRAM, NULL, NULL);
        refreshAllVisiblePrograms(hComboProgram);
        CreateWindowW(L"BUTTON", L"刷新设备", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            165, 145, 80, 22, hWnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);
        CreateWindowW(L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            250, 145, 40, 22, hWnd, (HMENU)IDC_BTN_CONN, NULL, NULL);

        CreateWindowW(L"BUTTON", L"保存配置", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            300, 10, 80, 22, hWnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"配置示例", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            390, 10, 80, 22, hWnd, (HMENU)IDC_BTN_DEMO, NULL, NULL);
        CreateWindowW(L"BUTTON", L"帮助", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            480, 10, 80, 22, hWnd, (HMENU)IDC_BTN_HELP, NULL, NULL);
        hEditConfig = CreateWindowW(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER,
            300, 35, 555, 515, hWnd, (HMENU)IDC_EDIT_CONFIG, NULL, NULL);






        hListBtnsVar = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER,
            10, 179, 140, 130, hWnd, (HMENU)IDC_LIST_BTNSVAR, NULL, NULL);
        for (const auto& kw : devBtnsVar) {
            SendMessageW(hListBtnsVar, LB_ADDSTRING, 0, (LPARAM)UTF8ToWString(kw.name).c_str());
        }
        hListFhs = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER,
            150, 179, 140, 130, hWnd, (HMENU)IDC_LIST_FHS, NULL, NULL);
        for (const auto& kw : fhs) {
            SendMessageW(hListFhs, LB_ADDSTRING, 0, (LPARAM)UTF8ToWString(kw).c_str());
        }

        hListBtns = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER,
            10, 308, 140, 250, hWnd, (HMENU)IDC_LIST_BTNS, NULL, NULL);
        hListKeywords = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | WS_VSCROLL | WS_BORDER,
            150, 308, 140, 250, hWnd, (HMENU)IDC_LIST_KEYWORDS, NULL, NULL);
        for (const auto& kw : keys) {
            SendMessageW(hListKeywords, LB_ADDSTRING, 0, (LPARAM)UTF8ToWString(kw).c_str());
        }

        

        DebugHexView::GetInstance().create(hWnd);

        // 初始刷新设备
        PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_REFRESH, BN_CLICKED), 0);
        connManage.startReadThread();

        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pDI = (LPDRAWITEMSTRUCT)lParam;

        HDC hdc = pDI->hDC;
        RECT rect = pDI->rcItem;
        int itemIndex = pDI->itemID;

        // 如果项无效，直接返回
        if (itemIndex == -1) break;
        HBRUSH hBlueBrush = CreateSolidBrush(RGB(180, 220, 240));

        if (pDI->CtlID == IDC_LIST_KEYWORDS) {
            if (itemIndex >= keys.size()) break;

            std::wstring text = UTF8ToWString(keys[itemIndex]);

            // 背景
            if (pDI->itemState & ODS_SELECTED) {
                FillRect(hdc, &rect, hBlueBrush);
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            else {
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            SetBkMode(hdc, TRANSPARENT);

            // 文字 (左侧)
            rect.left += 5;
            DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        }
        if (pDI->CtlID == IDC_LIST_FHS) {
            if (itemIndex >= fhs.size()) break;

            std::wstring text = UTF8ToWString(fhs[itemIndex]);

            // 背景
            if (pDI->itemState & ODS_SELECTED) {
                FillRect(hdc, &rect, hBlueBrush);
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            else {
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            SetBkMode(hdc, TRANSPARENT);

            // 文字 (左侧)
            rect.left += 5;
            DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (pDI->CtlID == IDC_LIST_BTNS) {
            if (itemIndex >= devBtns.size()) break;

            std::wstring text = UTF8ToWString(devBtns[itemIndex]);

            // 背景
            if (pDI->itemState & ODS_SELECTED) {
                FillRect(hdc, &rect, hBlueBrush);
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            else {
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            SetBkMode(hdc, TRANSPARENT);

            // 文字 (左侧)
            rect.left += 5;
            DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        }
        if (pDI->CtlID == IDC_LIST_BTNSVAR) {
            if (itemIndex >= devBtnsVar.size()) break;

            DevBtnsVarInfo& devBtnInfo = devBtnsVar[itemIndex];
            std::wstring text = UTF8ToWString(devBtnInfo.name);

            // 背景
            if (pDI->itemState & ODS_SELECTED) {
                FillRect(hdc, &rect, hBlueBrush);
                if (devBtnInfo.isChoosed) {
                    SetTextColor(hdc, RGB(0, 128, 0));
                }
                else {
                    SetTextColor(hdc, RGB(0, 0, 0));
                }
            }
            else if (devBtnInfo.isChoosed) {
                HBRUSH hGreenBrush = CreateSolidBrush(RGB(200, 255, 200));
                FillRect(hdc, &rect, hGreenBrush);
                SetTextColor(hdc, RGB(0, 128, 0));
            }
            else {
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
                SetTextColor(hdc, RGB(0, 0, 0));
            }
            SetBkMode(hdc, TRANSPARENT);

            // 文字 (左侧)
            rect.left += 20;
            DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            if (devBtnInfo.isChoosed) {
                const wchar_t* checkMark = L"✓";
                RECT checkRect = pDI->rcItem;
                checkRect.right = checkRect.left + 20;
                SetTextColor(hdc, RGB(0, 128, 0));
                DrawTextW(hdc, checkMark, -1, &checkRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
            if (!devBtnInfo.isKey) {
                funVars[devBtnInfo.name] = devBtnInfo.isChoosed;
            }
        }

        if (pDI->CtlID == IDC_LIST_DEVICES) {
            DeviceInfo& drawDev = g_deviceList[itemIndex];
            const std::wstring& text = drawDev.displayName;

            if (pDI->itemState & ODS_SELECTED) {
                FillRect(hdc, &rect, hBlueBrush);
                if (drawDev.isConnected) {
                    SetTextColor(hdc, RGB(0, 128, 0));
                }
                else {
                    SetTextColor(hdc, RGB(0, 0, 0));
                }
            }
            else if (drawDev.isConnected) {
                HBRUSH hGreenBrush = CreateSolidBrush(RGB(200, 255, 200));
                FillRect(hdc, &rect, hGreenBrush);
                SetTextColor(hdc, RGB(0, 128, 0));
            }
            else {
                FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH)); // 普通背景
                SetTextColor(hdc, RGB(0, 0, 0)); // 黑色文字
            }
            SetBkMode(hdc, TRANSPARENT);
            rect.left += 20;
            DrawTextW(hdc, text.c_str(), (int)text.length(), &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            if (drawDev.isConnected) {
                const wchar_t* checkMark = L"✓";
                RECT checkRect = pDI->rcItem;
                checkRect.right = checkRect.left + 20;
                SetTextColor(hdc, RGB(0, 128, 0));
                DrawTextW(hdc, checkMark, -1, &checkRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }

            // 绘制焦点矩形（如果项被选中且有焦点）
            if (pDI->itemState & ODS_FOCUS) {
                DrawFocusRect(hdc, &pDI->rcItem);
            }
        }
        
        return 0;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        switch (wmId) {
            case IDC_LIST_DEVICES: {
                if (wmEvent == LBN_SELCHANGE) {
                    int idx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
                    OutputDebugStringA(("select " + std::to_string(idx) + "\n").c_str());

                    if (idx != LB_ERR && idx < g_deviceList.size()) {
                        std::string filepath = connManage.devChooseNmMac(g_deviceList[idx].name, g_deviceList[idx].macAddress);
                        std::string filename = filepath + "\\" + g_selectedProgram + ".conf";
                        OutputDebugStringA(("loadConfigToEdit: " + filename + "\n").c_str());
                        // 加载配置
                        loadConfigToEdit(hEditConfig, filename);
                    }
                }

                if (wmEvent == LBN_DBLCLK) {
                    int idx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
                    SimpleLogger::LOG_INFO("LBN_DBLCLK: {}", idx);
                    if (idx == LB_ERR || idx >= g_deviceList.size()) {
                        MessageBox(hWnd, L"请先选择设备", L"错误", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    DeviceInfo& selectedDev = g_deviceList[idx];

                    if (selectedDev.isConnected) {
                        // 断开
                        connManage.devDisconnect(selectedDev.name, selectedDev.macAddress);
                        selectedDev.isConnected = false;
                    }
                    else {
                        // 连接
                        SimpleLogger::LOG_INFO("LBN_DBLCLK -> devConnect: {}", selectedDev.name);
                        bool iscon = connManage.devConnect(selectedDev.name, selectedDev.macAddress, selectedDev.devId);
                        selectedDev.isConnected = iscon;
                    }

                    InvalidateRect(hListDevices, NULL, TRUE);

                    SendMessage(hListBtns, LB_RESETCONTENT, 0, 0);

                    lfFilename = connManage.getNmMac() + "\\" + g_selectedProgram + ".conf";
                    g_keyMapper.loadConfig(lfFilename, connManage);

                    devBtns = connManage.getDevBtns(0);
                    if (devBtns.size() > 0) {
                        std::sort(devBtns.begin(), devBtns.end());
                        devBtns.erase(std::unique(devBtns.begin(), devBtns.end()), devBtns.end());
                    }
                    for (const auto& kw : devBtns) {
                        SendMessageW(hListBtns, LB_ADDSTRING, 0, (LPARAM)UTF8ToWString(kw).c_str());
                    }
                   
                    break;
                }
                break;
            }
            case IDC_LIST_BTNS: {
                if (wmEvent == LBN_DBLCLK) {
                    int idx = SendMessage(hListBtns, LB_GETCURSEL, 0, 0);
                    if (idx != LB_ERR && idx < devBtns.size()) {
                        std::string keyword = devBtns[idx];
                        std::wstring wKeyword = UTF8ToWString(keyword);

                        // 获取编辑框当前内容
                        int len = GetWindowTextLength(hEditConfig);
                        std::vector<wchar_t> buffer(len + 1);
                        GetWindowTextW(hEditConfig, buffer.data(), len + 1);
                        std::wstring currentText(buffer.data());

                        std::string pre;
                        for (DevBtnsVarInfo& varInfo : devBtnsVar) {
                            if (varInfo.isKey && varInfo.isChoosed) {
                                pre += varInfo.name;
                            }
                        }
                        if (!pre.empty()) {
                            wKeyword = UTF8ToWString(pre) + wKeyword;
                        }
                        wKeyword = L"{" + wKeyword + L"}";
                        std::wstring newText = currentText + wKeyword;
                        SetWindowTextW(hEditConfig, newText.c_str());

                        // 将光标移动到最后，方便继续输入
                        SendMessage(hEditConfig, EM_SETSEL, len + 1 + (int)wKeyword.length(), len + 1 + (int)wKeyword.length());
                        SetFocus(hEditConfig);
                    }
                }
                break;
            }
            case IDC_LIST_BTNSVAR: {
                if (wmEvent == LBN_DBLCLK) {
                    int idx = SendMessage(hListBtnsVar, LB_GETCURSEL, 0, 0);
                    if (idx != LB_ERR && idx < devBtnsVar.size()) {
                        devBtnsVar[idx].isChoosed = !devBtnsVar[idx].isChoosed;
                        InvalidateRect(hListBtnsVar, NULL, TRUE);
                    }
                }
                break;
            }
            case IDC_LIST_KEYWORDS: {
                if (wmEvent == LBN_DBLCLK) {
                    int idx = SendMessage(hListKeywords, LB_GETCURSEL, 0, 0);
                    if (idx != LB_ERR && idx < keys.size()) {
                        std::string keyword = keys[idx];
                        std::wstring wKeyword = UTF8ToWString(keyword);

                        // 获取编辑框当前内容
                        int len = GetWindowTextLength(hEditConfig);
                        std::vector<wchar_t> buffer(len + 1);
                        GetWindowTextW(hEditConfig, buffer.data(), len + 1);
                        std::wstring currentText(buffer.data());

                        std::wstring newText = currentText + wKeyword;
                        SetWindowTextW(hEditConfig, newText.c_str());

                        // 将光标移动到最后，方便继续输入
                        SendMessage(hEditConfig, EM_SETSEL, len + 1 + (int)wKeyword.length(), len + 1 + (int)wKeyword.length());
                        SetFocus(hEditConfig);
                    }
                }
                break;
            }
            case IDC_LIST_FHS: {
                if (wmEvent == LBN_DBLCLK) {
                    int idx = SendMessage(hListFhs, LB_GETCURSEL, 0, 0);
                    if (idx != LB_ERR && idx < fhs.size()) {
                        std::string keyword = fhs[idx];
                        std::wstring wKeyword = UTF8ToWString(keyword);

                        // 获取编辑框当前内容
                        int len = GetWindowTextLength(hEditConfig);
                        std::vector<wchar_t> buffer(len + 1);
                        GetWindowTextW(hEditConfig, buffer.data(), len + 1);
                        std::wstring currentText(buffer.data());

                        std::wstring newText = currentText + wKeyword;
                        SetWindowTextW(hEditConfig, newText.c_str());

                        // 将光标移动到最后，方便继续输入
                        SendMessage(hEditConfig, EM_SETSEL, len + 1 + (int)wKeyword.length(), len + 1 + (int)wKeyword.length());
                        SetFocus(hEditConfig);
                    }
                }
                break;
            }
            case IDC_COMBO_PROGRAM: {
                if (wmEvent == CBN_SELCHANGE) {
                    int idx = SendMessageW(hComboProgram, CB_GETCURSEL, 0, 0);
                    if (idx != CB_ERR) {
                        int len = SendMessageW(hComboProgram, CB_GETLBTEXTLEN, idx, 0);
                        if (len > 0) {
                            std::vector<wchar_t> buffer(len + 1);
                            SendMessageW(hComboProgram, CB_GETLBTEXT, idx, (LPARAM)buffer.data());
                            g_selectedProgram = WStringToUTF8(std::wstring(buffer.data()));

                            // 重新加载当前选中设备的配置
                            int devIdx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
                            if (devIdx != LB_ERR && devIdx < g_deviceList.size()) {
                                std::string filepath = connManage.devChooseNmMac(g_deviceList[devIdx].name, g_deviceList[devIdx].macAddress);
                                std::string filename = filepath + "\\" + g_selectedProgram + ".conf";
                                loadConfigToEdit(hEditConfig, filename);
                            }
                        }
                    }
                }
                break;
            }
            case IDC_BTN_REFRESH: {
                SendMessage(hListDevices, LB_RESETCONTENT, 0, 0);
                g_deviceList.clear();

                BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = { 0 };
                searchParams.dwSize = sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS);
                searchParams.fReturnAuthenticated = TRUE;
                searchParams.fReturnConnected = TRUE;
                searchParams.fIssueInquiry = TRUE;
                searchParams.fReturnUnknown = TRUE;
                searchParams.cTimeoutMultiplier = 3;

                BLUETOOTH_DEVICE_INFO deviceInfo;
                ZeroMemory(&deviceInfo, sizeof(BLUETOOTH_DEVICE_INFO));
                deviceInfo.dwSize = sizeof(BLUETOOTH_DEVICE_INFO);

                // 蓝牙设备
                HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);
                if (hFind == NULL) {
                    MessageBox(hWnd, L"ToyKeyMapper：未找到已配对的设备\n请确保设备已配对并在范围内", L"提示", MB_OK | MB_ICONWARNING);
                }
                else {
                    do {
                        std::wstring wideName(deviceInfo.szName);
                        std::string name(wideName.begin(), wideName.end());
                        std::string upperName = name;
                        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

                        DeviceInfo dev;
                        dev.name = upperName;
                        dev.macAddress = deviceInfo.Address.ullLong;
                        dev.displayName = deviceInfo.szName;
                        dev.isConnected = false;
                        g_deviceList.push_back(dev);
                        SimpleLogger::LOG_INFO("获取蓝牙设备: {}:{:012X}", upperName, deviceInfo.Address.ullLong);

                        SendMessageW(hListDevices, LB_ADDSTRING, 0, (LPARAM)dev.displayName.c_str());
                    } while (BluetoothFindNextDevice(hFind, &deviceInfo));
                    BluetoothFindDeviceClose(hFind);
                }

                /*
                // 搜索 HID 设备
                try {
                    if (g_bleWatcher && g_bleWatcher.Status() == winrt::Windows::Devices::Enumeration::DeviceWatcherStatus::Started) {
                        g_bleWatcher.Stop();
                    }

                    winrt::hstring selector = winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true);
                    winrt::Windows::Foundation::Collections::IVector<winrt::hstring> properties = winrt::single_threaded_vector<winrt::hstring>({
                        L"System.ItemNameDisplay"
                    });

                    g_bleWatcher = winrt::Windows::Devices::Enumeration::DeviceInformation::CreateWatcher(
                        selector,
                        properties,
                        winrt::Windows::Devices::Enumeration::DeviceInformationKind::Device
                    );

                    g_bleWatcher.Added([](winrt::Windows::Devices::Enumeration::DeviceWatcher const& sender,
                        winrt::Windows::Devices::Enumeration::DeviceInformation const& info) {
                            winrt::hstring nameHstr = info.Name();
                        std::wstring wName(nameHstr);

                        // 如果 Name 为空，尝试从属性获取
                        if (wName.empty()) {
                            auto props = info.Properties();
                            auto nameObj = props.Lookup(L"System.ItemNameDisplay");
                            if (nameObj) {
                                wName = winrt::unbox_value<winrt::hstring>(nameObj);
                            }
                        }

                        if (wName.empty()) return;

                        int len = WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), -1, NULL, 0, NULL, NULL);
                        std::string utf8Name(len, 0);
                        WideCharToMultiByte(CP_UTF8, 0, wName.c_str(), -1, &utf8Name[0], len, NULL, NULL);

                        winrt::hstring idHstr = info.Id();
                        std::wstring wId(idHstr);
                        std::hash<std::wstring> hasher;
                        unsigned long long pseudoMac = static_cast<unsigned long long>(hasher(wId));

                        DeviceInfo dev;
                        dev.name = "HID:" + utf8Name;
                        dev.displayName = wName;
                        dev.devId = wId;
                        dev.macAddress = pseudoMac;
                        dev.isConnected = false;

                        g_deviceList.push_back(dev);
                    });

                    g_bleWatcher.EnumerationCompleted([](winrt::Windows::Devices::Enumeration::DeviceWatcher const& sender,
                        winrt::Windows::Foundation::IInspectable const& args) {
                            PostMessage(mainWnd, WM_REFRESH_DEVICE_LIST, 0, 0);
                    });

                    g_bleWatcher.Start();
                    SetTimer(mainWnd, 1001, 5000, NULL);
                }
                catch (winrt::hresult_error const& ex) {
                    char msg[256];
                    sprintf_s(msg, "[Error] WinRT Enum Failed: HR=0x%08X, Msg=%s\n",
                        static_cast<unsigned int>(ex.code()),
                        winrt::to_string(ex.message()).c_str());
                    OutputDebugStringA(msg);
                }
                */

                break;
            }
            case IDC_BTN_SAVE: {
                int idx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR && idx < g_deviceList.size()) {
                    std::string filepath = connManage.devChooseNmMac(g_deviceList[idx].name, g_deviceList[idx].macAddress);
                    saveConfigFromEdit(hEditConfig, filepath, g_selectedProgram);
                    MessageBox(hWnd, L"配置已保存并重载", L"成功", MB_OK | MB_ICONINFORMATION);
                }
                break;
            }
            case IDC_BTN_DEMO: {
                int idx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR && idx < g_deviceList.size()) {
                    std::string defRule = connManage.getDefRule(g_deviceList[idx].name, g_deviceList[idx].macAddress);
                    std::wstring defRuleW = UTF8ToWString(defRule);

                    // 获取编辑框当前内容
                    int len = GetWindowTextLength(hEditConfig);
                    std::vector<wchar_t> buffer(len + 1);
                    GetWindowTextW(hEditConfig, buffer.data(), len + 1);
                    std::wstring currentText(buffer.data());

                    std::wstring newText = currentText + defRuleW;
                    SetWindowTextW(hEditConfig, newText.c_str());

                    // 将光标移动到最后，方便继续输入
                    SendMessage(hEditConfig, EM_SETSEL, len + 1 + (int)defRuleW.length(), len + 1 + (int)defRuleW.length());
                    SetFocus(hEditConfig);
                }
                break;
            }
            case IDC_BTN_HELP: {
                std::string mdContent = loadStringFromResource(g_hInst, RC_IDR_HELP, "HELP_MD");
                std::wstring wHelpText = UTF8ToWString(mdContent);

                MessageBoxW(hWnd, wHelpText.c_str(), L"帮助", MB_OK);
                break;
            }
            case IDC_BTN_CONN: {
                int idx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
                SimpleLogger::LOG_INFO("IDC_BTN_CONN: {}", idx);
                if (idx == LB_ERR || idx >= g_deviceList.size()) {
                    MessageBox(hWnd, L"请先选择设备", L"错误", MB_OK | MB_ICONERROR);
                    return 0;
                }
                DeviceInfo& selectedDev = g_deviceList[idx];

                if (selectedDev.isConnected) {
                    // 断开
                    connManage.devDisconnect(selectedDev.name, selectedDev.macAddress);
                    selectedDev.isConnected = false;
                }
                else {
                    // 连接
                    SimpleLogger::LOG_INFO("IDC_BTN_CONN -> devConnect: {}", selectedDev.name);
                    bool iscon = connManage.devConnect(selectedDev.name, selectedDev.macAddress, selectedDev.devId);
                    selectedDev.isConnected = iscon;
                }

                InvalidateRect(hListDevices, NULL, TRUE);

                SendMessage(hListBtns, LB_RESETCONTENT, 0, 0);
                devBtns = connManage.getDevBtns(0);
                if (devBtns.size() > 0) {
                    std::sort(devBtns.begin(), devBtns.end());
                    devBtns.erase(std::unique(devBtns.begin(), devBtns.end()), devBtns.end());
                }
                for (const auto& kw : devBtns) {
                    SendMessageW(hListBtns, LB_ADDSTRING, 0, (LPARAM)UTF8ToWString(kw).c_str());
                }

                lfFilename = connManage.getNmMac() + "\\" + g_selectedProgram + ".conf";
                g_keyMapper.loadConfig(lfFilename, connManage);
                break;
            }
        }

        break;
    }
    case WM_CONTEXTMENU: {
        if ((HWND)wParam == hListDevices) {
            // 获取当前选中的项
            int idx = SendMessage(hListDevices, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR && idx < g_deviceList.size()) {
                DeviceInfo& dev = g_deviceList[idx];

                // 调用 setDataView 切换状态
                bool isViewOn = connManage.setDataView(dev.name, dev.macAddress);
            }
        }
        return 0;
    }
    case WM_DESTROY: {
        connManage.devDisconnect("", 0);
        PostQuitMessage(0);
        break;
    }
    case WM_UPDATE_DEBUG_VIEW: {
        DebugHexView::GetInstance().refreshFromQueue();
        return 0;
    }
    case WM_REFRESH_DEVICE_LIST: {
        SendMessage(hListDevices, LB_RESETCONTENT, 0, 0);

        for (const auto& dev : g_deviceList) {
            SendMessageW(hListDevices, LB_ADDSTRING, 0, (LPARAM)dev.displayName.c_str());
        }
        return 0;
    }
    case WM_TIMER: {
        /*
        if (wParam == 1001) {
            if (g_bleWatcher && g_bleWatcher.Status() == winrt::Windows::Devices::Enumeration::DeviceWatcherStatus::Started) {
                g_bleWatcher.Stop();
            }
            KillTimer(hWnd, 1001);
        }
        */
        break;
    }
    /*case WM_INPUT: {
        OutputDebugStringA("WM_INPUT\n");
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize == 0) return 0;
    }*/
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;
    SimpleLogger::GetInstance().init("logs\\log.txt");

    connManage.setOnStateChanged(handleChangeState);
    const wchar_t CLASS_NAME[] = L"KeyMapConfigClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    mainWnd = CreateWindowExW(0, CLASS_NAME, L"按键映射工具", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 880, 600, NULL, NULL, hInstance, NULL);


    if (mainWnd == NULL) {
        return 0;
    }

    ShowWindow(mainWnd, nCmdShow);
    UpdateWindow(mainWnd);

    /*
    RAWINPUTDEVICE rid;
    ZeroMemory(&rid, sizeof(rid));
    rid.usUsagePage = 0x0C;
    rid.usUsage = 0x01;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = mainWnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        DWORD errorCode = GetLastError();
        OutputDebugStringA(("RegisterRawInputDevices Error (" + std::to_string(errorCode) + "): ").c_str());
        switch (errorCode) {
            case ERROR_INVALID_PARAMETER:
                OutputDebugStringA("ERROR_INVALID_PARAMETER\n");
                break;
            case ERROR_ACCESS_DENIED:
                OutputDebugStringA("ERROR_ACCESS_DENIED\n");
                break;
            case ERROR_NOT_ENOUGH_MEMORY:
                OutputDebugStringA("ERROR_NOT_ENOUGH_MEMORY\n");
                break;
            default:
                break;
        }
        
    }
    else {
        OutputDebugStringA("Raw Input Registered Successfully\n");
    }
    */

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}