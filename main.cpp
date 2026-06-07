#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <commdlg.h>
#include <vector>
#include <string>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#define WM_TRAYICON (WM_USER + 100)
#define ID_EXIT 1001
#define ID_COLOR 2001
#define ID_IMAGE_FILE 2002
#define ID_SCALE_FIT 2010
#define ID_SCALE_NONE 2011
#define ID_SCALE_STRETCH 2012

using namespace Gdiplus;

HANDLE g_hInstanceMutex = NULL;
std::vector<HWND> g_privacyWindows;
UINT_PTR g_topmostTimer = 0;
NOTIFYICONDATAA g_nid = {0};
HWND g_hTrayWnd = NULL;
HINSTANCE g_hInstance = NULL;

enum WallpaperMode { MODE_COLOR, MODE_IMAGE };
enum ImageScaleMode { SCALE_FIT = 0, SCALE_NONE = 1, SCALE_STRETCH = 2 };

struct Settings {
    WallpaperMode mode = MODE_COLOR;
    COLORREF color = RGB(0, 0, 0);
    std::wstring imagePath;
    ImageScaleMode scaleMode = SCALE_FIT;
} g_settings;

Image* g_wallpaperImage = NULL;

wchar_t g_exePath[MAX_PATH] = {0};
wchar_t g_configPath[MAX_PATH] = {0};
wchar_t g_imageCachePath[MAX_PATH] = {0};

// 中文字符串以 UTF-8 字节形式存储（运行时 MultiByteToWideChar 转宽字符）
static const char* S_MENU_BG          = "\xe6\x9b\xb4\xe6\x94\xb9\xe8\x83\x8c\xe6\x99\xaf";
static const char* S_MENU_COLOR       = "\xe7\xba\xaf\xe8\x89\xb2\xe8\x83\x8c\xe6\x99\xaf";
static const char* S_MENU_IMG         = "\xe8\x87\xaa\xe5\xae\x9a\xe4\xb9\x89\xe5\x9b\xbe\xe7\x89\x87";
static const char* S_MENU_PICK        = "\xe9\x80\x89\xe6\x8b\xa9\xe5\x9b\xbe\xe7\x89\x87";
static const char* S_MENU_SCALE       = "\xe7\xbc\xa9\xe6\x94\xbe\xe6\xa8\xa1\xe5\xbc\x8f";
static const char* S_MENU_FIT         = "\xe5\xa1\xab\xe5\x85\x85";
static const char* S_MENU_NONE        = "\xe5\xb1\x85\xe4\xb8\xad";
static const char* S_MENU_STRETCH     = "\xe6\x8b\x89\xe4\xbc\xb8";
static const char* S_MENU_EXIT        = "\xe6\x96\xad\xe5\xbc\x80\xe8\xbf\x9e\xe6\x8e\xa5\xef\xbc\x88\xe9\x80\x80\xe5\x87\xba\xe9\x9a\x90\xe7\xa7\x81\xe5\xb1\x8f\xe6\xa8\xa1\xe5\xbc\x8f\xef\xbc\x89";
static const char* S_TIP              = "\xe9\x9a\x90\xe7\xa7\x81\xe5\xb1\x8f\xe4\xbf\x9d\xe6\x8a\xa4";
static const char* S_FILTER_IMG       = "\xe5\x9b\xbe\xe7\x89\x87\xe6\x96\x87\xe4\xbb\xb6";
static const char* S_FILTER_IMG_PAT   = "*.jpg;*.jpeg;*.png;*.bmp;*.gif";
static const char* S_FILTER_ALL       = "\xe6\x89\x80\xe6\x9c\x89\xe6\x96\x87\xe4\xbb\xb6";
static const char* S_FILTER_ALL_PAT   = "*.*";

void Utf8ToWide(const char* utf8, wchar_t* dst, int dstSize) {
    if (!utf8 || !dst || dstSize <= 0) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, dstSize);
}

void CalcFitRect(int imgW, int imgH, int scrW, int scrH, RECT* out) {
    double scaleX = (double)scrW / imgW;
    double scaleY = (double)scrH / imgH;
    double scale = (scaleX < scaleY) ? scaleX : scaleY;
    int newW = (int)(imgW * scale);
    int newH = (int)(imgH * scale);
    out->left = (scrW - newW) / 2;
    out->top = (scrH - newH) / 2;
    out->right = out->left + newW;
    out->bottom = out->top + newH;
}

void PaintPrivacyWindow(HWND hWnd, HDC hdc) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    if (g_settings.mode == MODE_COLOR) {
        HBRUSH br = CreateSolidBrush(g_settings.color);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
    } else if (g_settings.mode == MODE_IMAGE && g_wallpaperImage) {
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        UINT iw = g_wallpaperImage->GetWidth();
        UINT ih = g_wallpaperImage->GetHeight();
        Graphics g(hdc);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

        if (g_settings.scaleMode == SCALE_NONE) {
            int dx = (w - (int)iw) / 2;
            int dy = (h - (int)ih) / 2;
            g.DrawImage(g_wallpaperImage, (REAL)dx, (REAL)dy, (REAL)iw, (REAL)ih);
        } else if (g_settings.scaleMode == SCALE_FIT) {
            RECT fit;
            CalcFitRect((int)iw, (int)ih, w, h, &fit);
            g.DrawImage(g_wallpaperImage, (REAL)fit.left, (REAL)fit.top,
                        (REAL)(fit.right - fit.left), (REAL)(fit.bottom - fit.top));
        } else {
            g.DrawImage(g_wallpaperImage, 0, 0, w, h);
        }
    } else {
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
}

LRESULT CALLBACK PrivacyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            PaintPrivacyWindow(hWnd, (HDC)wParam);
            return 1;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void RefreshAllWindows() {
    for (HWND h : g_privacyWindows) {
        InvalidateRect(h, NULL, TRUE);
        UpdateWindow(h);
    }
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEXA mi;
    mi.cbSize = sizeof(MONITORINFOEXA);
    GetMonitorInfoA(hMonitor, &mi);

    int x = mi.rcMonitor.left;
    int y = mi.rcMonitor.top;
    int width = mi.rcMonitor.right - mi.rcMonitor.left;
    int height = mi.rcMonitor.bottom - mi.rcMonitor.top;

    HWND hWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED |
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        "PrivacyScreenClass",
        NULL,
        WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, width, height,
        NULL, NULL, g_hInstance, NULL);

    if (hWnd) {
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
        // WDA_EXCLUDEFROMCAPTURE：物理屏可见，但屏幕捕获软件（Sunshine/录屏等）看不到
        // 远程串流时远程用户看到的是没保护的屏幕，本地用户被保护
        SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE);
        g_privacyWindows.push_back(hWnd);
    }
    return TRUE;
}

void LoadWallpaperImage(const wchar_t* path) {
    // 释放旧图片
    if (g_wallpaperImage) { delete g_wallpaperImage; g_wallpaperImage = NULL; }
    if (!path || !path[0]) return;

    // 从文件读到内存，再用 IStream 包装，FromStream 加载。
    // 这样不锁文件，文件可以被外部自由删除/覆盖。
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) { CloseHandle(hFile); return; }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, fileSize);
    if (!hMem) { CloseHandle(hFile); return; }
    void* pMem = GlobalLock(hMem);
    DWORD read = 0;
    BOOL ok = ReadFile(hFile, pMem, fileSize, &read, NULL);
    GlobalUnlock(hMem);
    CloseHandle(hFile);
    if (!ok || read != fileSize) { GlobalFree(hMem); return; }

    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK || !pStream) {
        GlobalFree(hMem);
        return;
    }
    // CreateStreamOnHGlobal(TRUE) 表示 GlobalFree 由 IStream 负责
    g_wallpaperImage = Image::FromStream(pStream, FALSE);
    pStream->Release();
    if (g_wallpaperImage && g_wallpaperImage->GetLastStatus() != Ok) {
        delete g_wallpaperImage; g_wallpaperImage = NULL;
    }
}

void GetModuleDir() {
    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);
    wchar_t* p = wcsrchr(g_exePath, L'\\');
    if (p) p[1] = 0;
    wsprintfW(g_configPath, L"%sDesktopPrivate.ini", g_exePath);
    wsprintfW(g_imageCachePath, L"%sDesktopPrivatePic", g_exePath);
}

void SaveSettings() {
    FILE* f = _wfopen(g_configPath, L"w, ccs=UTF-8");
    if (!f) return;
    fwprintf(f, L"[Wallpaper]\n");
    fwprintf(f, L"Mode=%d\n", g_settings.mode == MODE_COLOR ? 0 : 1);
    fwprintf(f, L"Color=%06X\n", (unsigned int)g_settings.color);
    fwprintf(f, L"ScaleMode=%d\n", (int)g_settings.scaleMode);
    fwprintf(f, L"ImagePath=%s\n", g_settings.imagePath.c_str());
    fclose(f);
}

void LoadSettings() {
    g_settings.mode = MODE_COLOR;
    g_settings.color = RGB(0, 0, 0);
    g_settings.scaleMode = SCALE_FIT;
    g_settings.imagePath.clear();

    FILE* f = _wfopen(g_configPath, L"r, ccs=UTF-8");
    if (!f) return;

    wchar_t line[1024];
    while (fgetws(line, 1024, f)) {
        wchar_t* eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = 0;
        wchar_t *key = line, *val = eq + 1;
        wchar_t* nl = wcschr(val, L'\n');
        if (nl) *nl = 0;

        if (wcscmp(key, L"Mode") == 0)
            g_settings.mode = (val[0] == L'1') ? MODE_IMAGE : MODE_COLOR;
        else if (wcscmp(key, L"Color") == 0) {
            unsigned int c;
            if (swscanf(val, L"%x", &c) == 1) g_settings.color = (COLORREF)c;
        }
        else if (wcscmp(key, L"ScaleMode") == 0) {
            int m;
            if (swscanf(val, L"%d", &m) == 1) g_settings.scaleMode = (ImageScaleMode)m;
        }
        else if (wcscmp(key, L"ImagePath") == 0 && val[0]) {
            g_settings.imagePath = val;
            // 找缓存图片
            const wchar_t* exts[] = {L".jpg",L".png",L".bmp",L".jpeg",L".gif"};
            for (int i = 0; i < 5; i++) {
                wchar_t test[MAX_PATH];
                wsprintfW(test, L"%s%s", g_imageCachePath, exts[i]);
                WIN32_FIND_DATAW fd;
                HANDLE hf = FindFirstFileW(test, &fd);
                if (hf != INVALID_HANDLE_VALUE) { FindClose(hf); LoadWallpaperImage(test); break; }
            }
        }
    }
    fclose(f);
}

// 复制图片到缓存：先删除所有可能的扩展名变体再复制
bool CopyImageToCache(const wchar_t* srcPath) {
    const wchar_t* exts[] = {L".jpg",L".png",L".bmp",L".jpeg",L".gif"};
    for (int i = 0; i < 5; i++) {
        wchar_t oldPath[MAX_PATH];
        wsprintfW(oldPath, L"%s%s", g_imageCachePath, exts[i]);
        DeleteFileW(oldPath);
    }
    const wchar_t* ext = wcsrchr(srcPath, L'.');
    if (!ext) ext = L".img";
    wchar_t dst[MAX_PATH];
    wsprintfW(dst, L"%s%s", g_imageCachePath, ext);
    return CopyFileW(srcPath, dst, FALSE) != 0;
}

LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON && LOWORD(lParam) == WM_RBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);

        HMENU hMenu = CreatePopupMenu();
        HMENU hBg = CreatePopupMenu();
        HMENU hImg = CreatePopupMenu();
        HMENU hScale = CreatePopupMenu();

        wchar_t buf[64];
        Utf8ToWide(S_MENU_BG, buf, 64);
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hBg, buf);

        Utf8ToWide(S_MENU_COLOR, buf, 64);
        AppendMenuW(hBg, MF_STRING, ID_COLOR, buf);

        Utf8ToWide(S_MENU_IMG, buf, 64);
        AppendMenuW(hBg, MF_POPUP, (UINT_PTR)hImg, buf);

        Utf8ToWide(S_MENU_PICK, buf, 64);
        AppendMenuW(hImg, MF_STRING, ID_IMAGE_FILE, buf);

        Utf8ToWide(S_MENU_SCALE, buf, 64);
        AppendMenuW(hImg, MF_POPUP, (UINT_PTR)hScale, buf);

        // 缩放子菜单：当前选项前加 ●
        const char* scaleLabels[3] = {S_MENU_FIT, S_MENU_NONE, S_MENU_STRETCH};
        UINT scaleIds[3] = {ID_SCALE_FIT, ID_SCALE_NONE, ID_SCALE_STRETCH};
        for (int i = 0; i < 3; i++) {
            wchar_t tmp[16];
            Utf8ToWide(scaleLabels[i], tmp, 16);
            wchar_t item[32];
            if ((int)g_settings.scaleMode == i) {
                wsprintfW(item, L"\x25cf %s", tmp);  // ● + 空格 + 标签
            } else {
                lstrcpyW(item, tmp);
            }
            AppendMenuW(hScale, MF_STRING, scaleIds[i], item);
        }

        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

        Utf8ToWide(S_MENU_EXIT, buf, 64);
        AppendMenuW(hMenu, MF_STRING, ID_EXIT, buf);

        SetForegroundWindow(hWnd);
        TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);
        return 0;
    }

    if (msg == WM_COMMAND) {
        UINT id = LOWORD(wParam);

        if (id == ID_EXIT) {
            DestroyWindow(hWnd);
            return 0;
        }

        if (id == ID_COLOR) {
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = hWnd;
            cc.rgbResult = g_settings.color;
            COLORREF custom[16] = {0};
            cc.lpCustColors = custom;
            cc.Flags = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&cc)) {
                g_settings.mode = MODE_COLOR;
                g_settings.color = cc.rgbResult;
                SaveSettings();
                RefreshAllWindows();
            }
            return 0;
        }

        if (id == ID_IMAGE_FILE) {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t file[MAX_PATH] = {0};

            // 手工拼接 filter 宽字符串（避开 L"\0" 字节序列问题）
            wchar_t wImgLabel[32], wAllLabel[32];
            wchar_t filterBuf[256];
            Utf8ToWide(S_FILTER_IMG, wImgLabel, 32);
            Utf8ToWide(S_FILTER_ALL, wAllLabel, 32);
            int pos = 0;
            for (const wchar_t* s = wImgLabel; *s && pos < 250; s++) filterBuf[pos++] = *s;
            filterBuf[pos++] = 0;
            for (const char* s = S_FILTER_IMG_PAT; *s && pos < 250; s++) filterBuf[pos++] = (wchar_t)*s;
            filterBuf[pos++] = 0;
            for (const wchar_t* s = wAllLabel; *s && pos < 250; s++) filterBuf[pos++] = *s;
            filterBuf[pos++] = 0;
            for (const char* s = S_FILTER_ALL_PAT; *s && pos < 250; s++) filterBuf[pos++] = (wchar_t)*s;
            filterBuf[pos++] = 0;
            filterBuf[pos++] = 0;

            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = filterBuf;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                if (CopyImageToCache(file)) {
                    g_settings.mode = MODE_IMAGE;
                    g_settings.imagePath = file;
                    // 加载新图片
                    wchar_t extBuf[MAX_PATH];
                    const wchar_t* ext = wcsrchr(file, L'.');
                    if (ext) wsprintfW(extBuf, L"%s%s", g_imageCachePath, ext);
                    else extBuf[0] = 0;
                    LoadWallpaperImage(extBuf);
                    SaveSettings();
                    RefreshAllWindows();
                }
            }
            return 0;
        }

        if (id >= ID_SCALE_FIT && id <= ID_SCALE_STRETCH) {
            g_settings.scaleMode = (ImageScaleMode)(id - ID_SCALE_FIT);
            SaveSettings();
            RefreshAllWindows();
            return 0;
        }
    }

    if (msg == WM_DESTROY) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }

    // 50ms topmost 维护（主线程的 WM_TIMER，不再用线程池回调）
    if (msg == WM_TIMER && wParam == 1) {
        for (HWND hwnd : g_privacyWindows) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    GetModuleDir();

    GdiplusStartupInput gsi;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gsi, NULL);

    g_hInstanceMutex = CreateMutexA(NULL, TRUE, "PrivacyScreen_SingleInstance_Mutex");
    if (!g_hInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_hInstanceMutex) CloseHandle(g_hInstanceMutex);
        GdiplusShutdown(gdiplusToken);
        return 0;
    }

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

    LoadSettings();

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = PrivacyWndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "PrivacyScreenClass";
    RegisterClassExA(&wc);

    WNDCLASSEXA trayWc = {0};
    trayWc.cbSize = sizeof(WNDCLASSEXA);
    trayWc.lpfnWndProc = TrayWndProc;
    trayWc.hInstance = hInstance;
    trayWc.lpszClassName = "TrayIconClass";
    RegisterClassExA(&trayWc);

    g_hTrayWnd = CreateWindowExA(0, "TrayIconClass", NULL, 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    wchar_t wTip[64];
    Utf8ToWide(S_TIP, wTip, 64);
    char tipAnsi[128];
    WideCharToMultiByte(CP_ACP, 0, wTip, -1, tipAnsi, sizeof(tipAnsi), NULL, NULL);

    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = g_hTrayWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    lstrcpyA(g_nid.szTip, tipAnsi);
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    // 多屏适配：枚举所有显示器，每块屏创建一个黑屏窗口
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)hInstance);

    // 50ms topmost timer（用 WM_TIMER 消息，在主线程跑，避开线程池 race）
    g_topmostTimer = SetTimer(g_hTrayWnd, 1, 50, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    KillTimer(NULL, g_topmostTimer);
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    for (HWND hwnd : g_privacyWindows) {
        DestroyWindow(hwnd);
    }
    if (g_wallpaperImage) { delete g_wallpaperImage; g_wallpaperImage = NULL; }
    UnregisterClassA("PrivacyScreenClass", hInstance);
    UnregisterClassA("TrayIconClass", hInstance);

    if (g_hInstanceMutex) {
        CloseHandle(g_hInstanceMutex);
        g_hInstanceMutex = NULL;
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
