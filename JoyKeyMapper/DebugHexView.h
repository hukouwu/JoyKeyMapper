#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <sstream>
#include <mutex>

#define WM_UPDATE_DEBUG_VIEW (WM_USER + 101)

class DebugHexView {
public:
    struct ViewEntry {
        std::vector<uint8_t> currdata;
        std::vector<uint8_t> lastData;
        std::map<std::string, std::string> vdata;
    };

    static DebugHexView& GetInstance() {
        static DebugHexView instance;
        return instance;
    }

    void create(HWND mainWnd) {
        if (!m_hWnd) {
            initWindow();
        }
        
        hMainWnd = mainWnd;
    }
    bool show(ViewEntry viewEntry) {
        if (!isOpen) {
            return isOpen;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            _viewEntry = std::move(viewEntry);
        }
        PostMessage(hMainWnd, WM_UPDATE_DEBUG_VIEW, 0, 0);
        return isOpen;
    }

    void refreshFromQueue() {
        ShowWindow(m_hWnd, SW_SHOW);
        SetForegroundWindow(m_hWnd);
        InvalidateRect(m_hWnd, NULL, FALSE);
        UpdateWindow(m_hWnd);
    }

    void open() {
        isOpen = true;
    }

private:
    HWND m_hWnd = NULL;
    HWND hMainWnd = NULL;

    // 展示数据
    std::mutex m_mutex;
    ViewEntry _viewEntry;
    bool isOpen = false;

    HFONT m_hFont = NULL;

    DebugHexView() {
        m_hFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            FIXED_PITCH | FF_MODERN, "Consolas");
    }

    ~DebugHexView() {
        if (m_hFont) DeleteObject(m_hFont);
    }

    void initWindow() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WndProcStatic;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"DebugHexViewClass";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);

        // 如果类已存在，RegisterClassW 会失败，这是正常的
        RegisterClassW(&wc);

        m_hWnd = CreateWindowExW(
            WS_EX_TOPMOST,
            L"DebugHexViewClass",
            L"Realtime Hex View",
            WS_OVERLAPPEDWINDOW,
            100, 100, 700, 600,
            NULL, NULL,
            GetModuleHandle(NULL),
            this
        );

        if (!m_hWnd) {
            MessageBoxA(NULL, "Failed to create Debug Window", "Error", MB_OK);
        }
    }

    static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        /*
        char msgBuf[128];
        sprintf_s(msgBuf, "DebugHexView Msg: 0x%04X\n", msg);
        OutputDebugStringA(msgBuf);
        */

        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }

        DebugHexView* pThis = (DebugHexView*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (pThis) {
            return pThis->wndProc(hWnd, msg, wParam, lParam);
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    LRESULT wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rect;
            GetClientRect(hWnd, &rect);

            ViewEntry localViewEntry;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                localViewEntry = _viewEntry;
            }

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            FillRect(memDC, &rect, (HBRUSH)(COLOR_WINDOW + 1));
            SelectObject(memDC, m_hFont);

            int hexStartX = 10;
            int hexStartY = 10;
            int x = hexStartX;
            int y = hexStartY;
            const int lineHeight = 20;
            const int charWidth = 8; // Consolas 14px 大约宽 8-9px

            // --- 绘制 Hex ---
            TextOutA(memDC, x, y, "Hex Data:", 9);
            y += lineHeight;
            int maxHexY = y;

            for (size_t i = 0; i < localViewEntry.currdata.size(); ++i) {
                char buf[4];
                sprintf_s(buf, "%02X ", localViewEntry.currdata[i]);

                // 判断变化
                bool changed = false;
                if (i < localViewEntry.lastData.size()) {
                    changed = (localViewEntry.currdata[i] != localViewEntry.lastData[i]);
                }
                else {
                    changed = true;
                }

                if (changed) {
                    SetTextColor(memDC, RGB(255, 0, 0)); // 红色表示变化
                }
                else {
                    SetTextColor(memDC, RGB(0, 0, 0));
                }

                TextOutA(memDC, x, y, buf, 3);

                x += charWidth * 3;
                if ((i + 1) % 16 == 0) {
                    x = hexStartX;
                    y += lineHeight;
                    maxHexY = y;
                }
            }
            if (x > hexStartX) {
                maxHexY = y + lineHeight;
            }

            // --- 绘制 KV ---
            int kvStartX = hexStartX + (16 * 3 * charWidth) + 20;
            int kvStartY = 10;

            x = kvStartX;
            y = kvStartY;

            TextOutA(memDC, x, y, "Parsed Values:", 14);
            y += lineHeight * 1;

            SetTextColor(memDC, RGB(0, 0, 0));
            for (const auto& pair : localViewEntry.vdata) {
                std::string line = pair.first + ": " + pair.second;
                TextOutA(memDC, x, y, line.c_str(), (int)line.length());
                y += lineHeight;
                if (y > rect.bottom - 20) break;
            }

            BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return 0;
        case WM_CLOSE:
            ShowWindow(m_hWnd, SW_HIDE);
            isOpen = false;
            return 0;
        case WM_DESTROY:
            ShowWindow(m_hWnd, SW_HIDE);
            isOpen = false;
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
};