#include <windows.h>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#define WM_TRAYICON (WM_USER + 100)
#define ID_EXIT 1001

// 全局互斥体句柄（用于单实例检测）
HANDLE g_hInstanceMutex = NULL;

std::vector<HWND> g_privacyWindows;
UINT_PTR g_topmostTimer = 0;
NOTIFYICONDATA g_nid = {0};

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect((HDC)wParam, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return 1;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_EXIT, "断开连接（退出隐私屏模式）");
                
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, 
                              pt.x, pt.y, 0, hWnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
            
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_EXIT) {
                DestroyWindow(hWnd);
            }
            return 0;
            
        case WM_DESTROY:
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
            
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEX mi;
    mi.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfoA(hMonitor, &mi);
    
    int x = mi.rcMonitor.left;
    int y = mi.rcMonitor.top;
    int width = mi.rcMonitor.right - mi.rcMonitor.left;
    int height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    
    HWND hWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | 
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_COMPOSITED,
        "PrivacyScreenClass",
        NULL,
        WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, width, height,
        NULL, NULL, (HINSTANCE)dwData, NULL
    );
    
    if (hWnd) {
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
        SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE);
        g_privacyWindows.push_back(hWnd);
    }
    
    return TRUE;
}

VOID CALLBACK TopmostTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    for (HWND hwnd : g_privacyWindows) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, 
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // ===================== 单实例检测（静默退出） =====================
    // 创建全局互斥体（去掉Global\前缀，普通权限即可运行，当前用户会话内单实例）
    g_hInstanceMutex = CreateMutexA(
        NULL,               // 默认安全属性
        TRUE,               // 初始拥有权
        "PrivacyScreen_SingleInstance_Mutex"  // 唯一命名
    );
    
    // 检查互斥体是否已存在（即是否已有实例运行）
    if (g_hInstanceMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        // 释放句柄后直接静默退出，无任何弹窗
        if (g_hInstanceMutex) CloseHandle(g_hInstanceMutex);
        return 0;
    }
    // ==========================================================

    // 开启Per-Monitor V2 DPI感知
    HMODULE hUser32 = LoadLibraryA("user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextFunc pSetDpiAwarenessContext = 
            (SetProcessDpiAwarenessContextFunc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        
        if (pSetDpiAwarenessContext) {
            pSetDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        FreeLibrary(hUser32);
    }
    
    // 注册隐私窗口类
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "PrivacyScreenClass";
    RegisterClassExA(&wc);
    
    // 注册托盘窗口类
    WNDCLASSEXA trayWc = {0};
    trayWc.cbSize = sizeof(WNDCLASSEXA);
    trayWc.lpfnWndProc = TrayWndProc;
    trayWc.hInstance = hInstance;
    trayWc.lpszClassName = "TrayIconClass";
    RegisterClassExA(&trayWc);
    
    // 创建托盘窗口
    HWND hTrayWnd = CreateWindowExA(0, "TrayIconClass", NULL, 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    
    // 设置系统托盘图标
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hTrayWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    lstrcpyA(g_nid.szTip, "Sunshine隐私屏");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
    
    // 枚举显示器并创建隐私窗口
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)hInstance);
    
    // 50ms定时器维持置顶
    g_topmostTimer = SetTimer(NULL, 0, 50, TopmostTimerProc);
    
    // 消息循环
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    
    // 释放资源
    KillTimer(NULL, g_topmostTimer);
    for (HWND hwnd : g_privacyWindows) {
        DestroyWindow(hwnd);
    }
    UnregisterClassA("PrivacyScreenClass", hInstance);
    UnregisterClassA("TrayIconClass", hInstance);

    // 释放互斥体
    if (g_hInstanceMutex) {
        CloseHandle(g_hInstanceMutex);
        g_hInstanceMutex = NULL;
    }
    
    return 0;
}
