#pragma once
#include <map>
#include <string>
#include <unordered_set>
#include <windows.h>
#include <optional>
#include <algorithm>
#include <cctype>

class KvInput {
public:
    inline static const std::vector<std::string> allKeys = {
            "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "MOUSE_LBT", "MOUSE_RBT","MOUSE_MBT", "MOUSE_MOV", "MOUSE_ABM", "MOUSE_VRO", "MOUSE_HRO",
            "ENTER", "ESC", "SPACE", "TAB",
            "BKSP", "UP", "DOWN", "LEFT", "RIGHT",
            "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
            "CTRL", "SHIFT", "ALT",
            "INS", "DEL", "HOME", "END",
            "PGUP", "PGDN"
    };
    static std::string ToUpper(const std::string& str) {
        std::string upper = str;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        return upper;
    }

    /**
     * @brief 解析按下事件的 INPUT 结构
     * @param keyName 按键名称 (如 "a", "ENTER", "MOUSE_LBT")
     * @param propVars 参数列表 (对于键盘通常是 VK code 或忽略; 对于鼠标可能是 dx, dy, wheel delta)
     * @return std::optional<INPUT> 构造好的输入事件，如果键名未知则返回 nullopt
     */
    static std::tuple <int, std::optional<INPUT>> parseDownInput(const std::string& keyName, const std::vector<double>& propVars) {
        std::string upperKey = ToUpper(keyName);
        if (downedKeys.find(upperKey) != downedKeys.end()) {
            return std::make_tuple(0, std::nullopt);
        }
        
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD; // 默认为键盘

        // --- 键盘处理 ---
        if (isKeyboardKey(upperKey)) {
            WORD vk = getVkCode(upperKey);
            if (vk == 0) return std::make_tuple(0, std::nullopt);

            input.type = INPUT_KEYBOARD;
            input.ki.wVk = vk;
            input.ki.wScan = 0;
            input.ki.dwFlags = 0; // 0 表示按下
            input.ki.time = 0;
            input.ki.dwExtraInfo = 0;
            return std::make_tuple(1000, input);
        }

        // --- 鼠标处理 ---
        else if (upperKey == "MOUSE_LBT") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(1000, input);
        }
        else if (upperKey == "MOUSE_RBT") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(1000, input);
        }
        else if (upperKey == "MOUSE_MBT") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(1000, input);
        }
        else if (upperKey == "MOUSE_MOV") {
            input.type = INPUT_MOUSE;
            long dx = 0;
            long dy = 0;
            if (propVars.size() > 0) dx = static_cast<long>(propVars[0]);
            if (propVars.size() > 1) dy = static_cast<long>(propVars[1]);

            input.mi.dx = dx;
            input.mi.dy = dy;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(0, input);
        }
        else if (upperKey == "MOUSE_ABM") {
            input.type = INPUT_MOUSE;

            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);

            double rawX = (propVars.size() > 0) ? propVars[0] : screenWidth / 2.0;
            double rawY = (propVars.size() > 1) ? propVars[1] : screenHeight / 2.0;
            long normalizedX = static_cast<long>((rawX * 65536.0) / screenWidth);
            long normalizedY = static_cast<long>((rawY * 65536.0) / screenHeight);

            input.mi.dx = normalizedX;
            input.mi.dy = normalizedY;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(0, input);
        }
        else if (upperKey == "MOUSE_VRO") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;

            long delta = 0;
            if (propVars.size() > 0) delta = static_cast<long>(propVars[0]);
            else delta = 120;

            input.mi.mouseData = static_cast<DWORD>(delta);
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(0, input);
        }
        else if (upperKey == "MOUSE_HRO") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;

            long delta = 0;
            if (propVars.size() > 0) delta = static_cast<long>(propVars[0]);
            else delta = 120;

            input.mi.mouseData = static_cast<DWORD>(delta);
            input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return std::make_tuple(0, input);
        }

        return std::make_tuple(0, std::nullopt);
    }

    /**
     * @brief 解析释放事件的 INPUT 结构
     */
    static std::optional<INPUT> parseUpInput(const std::string& keyName, const std::vector<double>& propVars) {
        std::string upperKey = ToUpper(keyName);
        INPUT input = { 0 };

        // --- 键盘处理 ---
        if (isKeyboardKey(upperKey)) {
            WORD vk = getVkCode(upperKey);
            if (vk == 0) return std::nullopt;

            input.type = INPUT_KEYBOARD;
            input.ki.wVk = vk;
            input.ki.wScan = 0;
            input.ki.dwFlags = KEYEVENTF_KEYUP; // 释放标志
            input.ki.time = 0;
            input.ki.dwExtraInfo = 0;
            return input;
        }

        // --- 鼠标处理 ---
        else if (upperKey == "MOUSE_LBT") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return input;
        }
        else if (upperKey == "MOUSE_RBT") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return input;
        }
        else if (upperKey == "MOUSE_MBT") {
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
            return input;
        }

        // 鼠标移动和滚轮通常没有"释放"概念，返回 nullopt 或忽略
        return std::nullopt;
    }

    static void setDown(std::string kNm) {
        std::string upperKey = ToUpper(kNm);
        downedKeys.insert(upperKey);
    }
    static void setUp(std::string kNm) {
        std::string upperKey = ToUpper(kNm);
        downedKeys.erase(upperKey);
    }

private:
    // 简单的键盘映射表
    static WORD getVkCode(const std::string& upperKey) {
        if (upperKey.length() == 1 && std::isalnum(upperKey[0])) {
            return static_cast<WORD>(upperKey[0]);
        }

        if (upperKey == "ENTER" || upperKey == "RETURN") return VK_RETURN;
        if (upperKey == "ESCAPE" || upperKey == "ESC") return VK_ESCAPE;
        if (upperKey == "SPACE") return VK_SPACE;
        if (upperKey == "TAB") return VK_TAB;
        if (upperKey == "BACKSPACE" || upperKey == "BKSP") return VK_BACK;

        if (upperKey == "UP") return VK_UP;
        if (upperKey == "DOWN") return VK_DOWN;
        if (upperKey == "LEFT") return VK_LEFT;
        if (upperKey == "RIGHT") return VK_RIGHT;

        if (upperKey == "F1") return VK_F1;
        if (upperKey == "F2") return VK_F2;
        if (upperKey == "F3") return VK_F3;
        if (upperKey == "F4") return VK_F4;
        if (upperKey == "F5") return VK_F5;
        if (upperKey == "F6") return VK_F6;
        if (upperKey == "F7") return VK_F7;
        if (upperKey == "F8") return VK_F8;
        if (upperKey == "F9") return VK_F9;
        if (upperKey == "F10") return VK_F10;
        if (upperKey == "F11") return VK_F11;
        if (upperKey == "F12") return VK_F12;

        if (upperKey == "CTRL" || upperKey == "CONTROL") return VK_CONTROL;
        if (upperKey == "SHIFT") return VK_SHIFT;
        if (upperKey == "ALT") return VK_MENU;

        if (upperKey == "INSERT" || upperKey == "INS") return VK_INSERT;
        if (upperKey == "DELETE" || upperKey == "DEL") return VK_DELETE;
        if (upperKey == "HOME") return VK_HOME;
        if (upperKey == "END") return VK_END;
        if (upperKey == "PAGEUP" || upperKey == "PGUP") return VK_PRIOR;
        if (upperKey == "PAGEDOWN" || upperKey == "PGDN") return VK_NEXT;

        return 0;
    }

    static bool isKeyboardKey(const std::string& upperKey) {
        // 简单判断：如果是单字符字母数字，或者是已知的功能键名
        if (upperKey.length() == 1 && std::isalnum(upperKey[0])) return true;

        static const std::set<std::string> specialKeys = {
            "ENTER", "RETURN", "ESCAPE", "ESC", "SPACE", "TAB",
            "BACKSPACE", "BKSP", "UP", "DOWN", "LEFT", "RIGHT",
            "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
            "CTRL", "CONTROL", "SHIFT", "ALT",
            "INSERT", "INS", "DELETE", "DEL", "HOME", "END",
            "PAGEUP", "PGUP", "PAGEDOWN", "PGDN"
        };
        
        return specialKeys.find(upperKey) != specialKeys.end();
    }

    inline static std::unordered_set<std::string> downedKeys;
};