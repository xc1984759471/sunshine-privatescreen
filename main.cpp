#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <commdlg.h>
#include <vector>
#include <string>
#include <bcrypt.h>
#include <wincred.h>
#include <commctrl.h>
#include <stdio.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "credui.lib")
#pragma comment(lib, "crypt32.lib")

#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#define WM_TRAYICON (WM_USER + 100)
#define ID_EXIT 1001
#define ID_COLOR 2001
#define ID_IMAGE_FILE 2002
#define ID_SCALE_FIT 2010
#define ID_SCALE_NONE 2011
#define ID_SCALE_STRETCH 2012
#define HOTKEY_EXIT_ID 3001
#define TIMER_HOTKEY_RESET 3002

using namespace Gdiplus;

HANDLE g_hInstanceMutex = NULL;
std::vector<HWND> g_privacyWindows;
UINT_PTR g_topmostTimer = 0;
NOTIFYICONDATAA g_nid = {0};
HWND g_hTrayWnd = NULL;
HINSTANCE g_hInstance = NULL;
bool g_showingPasswordDialog = false;  // 是否正在显示密码对话框

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
wchar_t g_configPath[MAX_PATH] = {0};       // 密码配置:%APPDATA%\DesktopPrivate\DesktopPrivate.ini
wchar_t g_wallpaperConfigPath[MAX_PATH] = {0}; // 壁纸配置:exe 同目录 DesktopPrivate.ini
wchar_t g_imageCachePath[MAX_PATH] = {0};

// ========== 新增:热键连按计数 ==========
int g_hotkeyPressCount = 0;
DWORD g_lastHotkeyTime = 0;
const int HOTKEY_TRIGGER_COUNT = 5;
const DWORD HOTKEY_TIMEOUT_MS = 5000;

// ========== 新增:密码相关 ==========
std::vector<BYTE> g_encryptedPassword;
std::vector<BYTE> g_passwordIV;

// ========== 新增:密码失败锁定(防暴力破解) ==========
// 规则:累计输错 >=3 次,每次错误锁定 1 分钟;累计 >=6 次,每次错误锁定 3 分钟。
// 锁定期间无论输入正确与否都不放行;只有锁定解除后输入正确密码才能退出。
// 计数仅在"验证成功"时清零;关闭对话框或重启程序都不清零(状态写入隐藏文件)。
static int           g_pwdFailCount = 0;              // 累计连续输错次数
static ULONGLONG     g_lockUntilFT  = 0;              // 锁定截止时刻(FILETIME UTC, 100ns)
static wchar_t       g_lockFilePath[MAX_PATH] = {0};  // 锁定状态持久化文件
static const ULONGLONG FT_PER_SEC     = 10000000ULL;  // 1 秒 = 1000万 个 100ns
static const int     LOCK_STAGE1_FAIL = 3;            // 第 3 次错误起
static const int     LOCK_STAGE2_FAIL = 6;            // 第 6 次错误起
static const int     LOCK_STAGE1_SEC  = 60;           // 1 分钟
static const int     LOCK_STAGE2_SEC  = 180;          // 3 分钟

// 中文字符串以 UTF-8 字节形式存储(运行时 MultiByteToWideChar 转宽字符)
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

// 新增中文字符串
static const char* S_HOTKEY_TIP_PREFIX    = "\xe5\x86\x8d\xe6\x8c\x89";  // "再按"
static const char* S_HOTKEY_TIP_SUFFIX    = "\xe6\xac\xa1\xe5\x8d\xb3\xe5\x8f\xaf\xe5\xbc\xba\xe5\x88\xb6\xe9\x80\x80\xe5\x87\xba\xe9\x9a\x90\xe7\xa7\x81\xe5\xb1\x8f\xe6\xa8\xa1\xe5\xbc\x8f";  // "次即可强制退出隐私屏模式"
static const char* S_SET_PASSWORD_TITLE   = "\xe8\xae\xbe\xe7\xbd\xae\xe5\xaf\x86\xe7\xa0\x81";
static const char* S_PASSWORD_LABEL       = "\xe8\xbe\x93\xe5\x85\xa5\xe5\xaf\x86\xe7\xa0\x81\xef\xbc\x9a";
static const char* S_CONFIRM_LABEL        = "\xe5\x86\x8d\xe6\xac\xa1\xe8\xbe\x93\xe5\x85\xa5\xe5\xaf\x86\xe7\xa0\x81\xef\xbc\x9a";
static const char* S_INPUT_PASSWORD_TITLE = "\xe8\xbe\x93\xe5\x85\xa5\xe5\xaf\x86\xe7\xa0\x81";
static const char* S_OK_BTN               = "\xe7\xa1\xae\xe5\xae\x9a";
static const char* S_CANCEL               = "\xe5\x8f\x96\xe6\xb6\x88";
static const char* S_FORGOT_PASSWORD      = "\xe5\xbf\x98\xe8\xae\xb0\xe5\xaf\x86\xe7\xa0\x81";
static const char* S_PASSWORD_MISMATCH    = "\xe4\xb8\xa4\xe6\xac\xa1\xe8\xbe\x93\xe5\x85\xa5\xe7\x9a\x84\xe5\xaf\x86\xe7\xa0\x81\xe4\xb8\x8d\xe4\xb8\x80\xe8\x87\xb4\xef\xbc\x8c\xe8\xaf\xb7\xe9\x87\x8d\xe6\x96\xb0\xe8\xae\xbe\xe7\xbd\xae";
static const char* S_PASSWORD_EMPTY       = "\xe5\xaf\x86\xe7\xa0\x81\xe4\xb8\x8d\xe8\x83\xbd\xe4\xb8\xba\xe7\xa9\xba";
static const char* S_PASSWORD_ERROR       = "\xe5\xaf\x86\xe7\xa0\x81\xe9\x94\x99\xe8\xaf\xaf";
static const char* S_PASSWORD_TOO_SHORT   = "密码至少要8位";
static const char* S_PASSWORD_TOO_LONG    = "密码最多128位";

static const char* S_ERR_TITLE     = "\xe9\x94\x99\xe8\xaf\xaf";
// 锁定状态改由面板红色倒计时表达,不再弹出"已锁定X分钟"弹窗,
// 故移除 S_LOCK_TITLE / S_LOCK_1MIN / S_LOCK_3MIN / S_LOCK_REJECT 四个过时文案。
static const char* S_LOCK_HINT     = "\xe5\xb7\xb2\xe9\x94\x81\xe5\xae\x9a\xef\xbc\x8c\xe5\x89\xa9\xe4\xbd\x99";

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

void PaintWallpaper(HDC hdc, int w, int h) {
    RECT rc = {0, 0, w, h};
    if (g_settings.mode == MODE_COLOR) {
        HBRUSH br = CreateSolidBrush(g_settings.color);
        FillRect(hdc, &rc, br); DeleteObject(br);
    } else if (g_settings.mode == MODE_IMAGE && g_wallpaperImage) {
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        UINT iw = g_wallpaperImage->GetWidth();
        UINT ih = g_wallpaperImage->GetHeight();
        Graphics g(hdc);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        if (g_settings.scaleMode == SCALE_NONE) {
            int dx = (w - (int)iw) / 2; int dy = (h - (int)ih) / 2;
            g.DrawImage(g_wallpaperImage, (REAL)dx, (REAL)dy, (REAL)iw, (REAL)ih);
        } else if (g_settings.scaleMode == SCALE_FIT) {
            RECT fit; CalcFitRect((int)iw, (int)ih, w, h, &fit);
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
        SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE);
        g_privacyWindows.push_back(hWnd);
    }
    return TRUE;
}

void LoadWallpaperImage(const wchar_t* path) {
    if (g_wallpaperImage) { delete g_wallpaperImage; g_wallpaperImage = NULL; }
    if (!path || !path[0]) return;

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

    // 壁纸配置:在 exe 同目录(可被用户编辑)
    wsprintfW(g_wallpaperConfigPath, L"%sDesktopPrivate.ini", g_exePath);
    wsprintfW(g_imageCachePath, L"%sDesktopPrivatePic", g_exePath);

    // 密码配置:在 %APPDATA%\DesktopPrivate\(隐藏,防止篡改)
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        wsprintfW(g_configPath, L"%s\\DesktopPrivate\\DesktopPrivate.ini", appDataPath);
        // 新增:锁定状态文件(同目录,隐藏+系统属性,防止重启程序绕过锁定)
        wsprintfW(g_lockFilePath, L"%s\\DesktopPrivate\\DesktopPrivate.lock", appDataPath);

        // 确保 DesktopPrivate 目录存在
        wchar_t dirPath[MAX_PATH];
        wsprintfW(dirPath, L"%s\\DesktopPrivate", appDataPath);
        CreateDirectoryW(dirPath, NULL);  // 可能已存在,忽略错误
    } else {
        // 回退到 exe 同目录
        wsprintfW(g_configPath, L"%sDesktopPrivate.ini", g_exePath);
        wsprintfW(g_lockFilePath, L"%sDesktopPrivate.lock", g_exePath);  // 新增
    }
}

void SaveWallpaperSettings() {
    // 壁纸配置写在 exe 同目录(可被用户编辑)
    FILE* f = _wfopen(g_wallpaperConfigPath, L"w, ccs=UTF-16LE");
    if (!f) return;

    // 先输出 BOM (UTF-16LE)
    BYTE bom[] = {0xFF, 0xFE};
    fwrite(bom, 1, 2, f);

    fwprintf(f, L"[Wallpaper]\n");
    fwprintf(f, L"Mode=%d\n", g_settings.mode == MODE_COLOR ? 0 : 1);
    fwprintf(f, L"Color=%06X\n", (unsigned int)g_settings.color);
    fwprintf(f, L"ScaleMode=%d\n", (int)g_settings.scaleMode);
    fwprintf(f, L"ImagePath=%s\n", g_settings.imagePath.c_str());

    fclose(f);
}

void SavePasswordSettings() {
    if (g_configPath[0] == 0) {
        MessageBoxA(NULL, "SavePassword: g_configPath is EMPTY!", "BUG", MB_OK | MB_ICONERROR); return;
    }
    HANDLE hFile = CreateFileW(g_configPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wchar_t msg[512];
        wsprintfW(msg, L"SavePassword FAILED:\n%s\nError=%d", g_configPath, GetLastError());
        MessageBoxW(NULL, msg, L"BUG", MB_OK | MB_ICONERROR); return;
    }
    BYTE bom[2] = {0xFF, 0xFE};
    DWORD written = 0;
    WriteFile(hFile, bom, 2, &written, NULL);

    if (!g_encryptedPassword.empty() && !g_passwordIV.empty()) {
        std::vector<BYTE> combined;
        combined.insert(combined.end(), g_passwordIV.begin(), g_passwordIV.end());
        combined.insert(combined.end(), g_encryptedPassword.begin(), g_encryptedPassword.end());
        DWORD base64Len = 0;
        CryptBinaryToStringW((BYTE*)combined.data(), (DWORD)combined.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);
        if (base64Len > 0) {
            wchar_t* base64Str = new wchar_t[base64Len];
            CryptBinaryToStringW((BYTE*)combined.data(), (DWORD)combined.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, base64Str, &base64Len);
            wchar_t sec[16] = L"[Security]\n";
            WriteFile(hFile, sec, wcslen(sec)*2, &written, NULL);
            wchar_t pre[24] = L"EncryptedPassword=";
            WriteFile(hFile, pre, wcslen(pre)*2, &written, NULL);
            WriteFile(hFile, base64Str, wcslen(base64Str)*2, &written, NULL);
            wchar_t nl[2] = L"\n";
            WriteFile(hFile, nl, 4, &written, NULL);
            delete[] base64Str;
        }
    }
    CloseHandle(hFile);
}

// Base64解码辅助函数
std::vector<BYTE> Base64Decode(const wchar_t* base64Str) {
    std::vector<BYTE> result;
    DWORD decodedLen = 0;
    if (!CryptStringToBinaryW(base64Str, 0, CRYPT_STRING_BASE64, NULL, &decodedLen, NULL, NULL))
        return result;

    result.resize(decodedLen);
    if (!CryptStringToBinaryW(base64Str, 0, CRYPT_STRING_BASE64, result.data(), &decodedLen, NULL, NULL)) {
        result.clear();
    }
    return result;
}

void LoadWallpaperSettings() {
    // 壁纸配置从 exe 同目录读取
    g_settings.mode = MODE_COLOR;
    g_settings.color = RGB(0, 0, 0);
    g_settings.scaleMode = SCALE_FIT;
    g_settings.imagePath.clear();

    FILE* f = _wfopen(g_wallpaperConfigPath, L"r, ccs=UTF-16LE");
    if (!f) return;

    wchar_t line[2048];
    while (fgetws(line, 2048, f)) {
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

void LoadPasswordSettings() {
    g_encryptedPassword.clear();
    g_passwordIV.clear();

    HANDLE hFile = CreateFileW(g_configPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD size = GetFileSize(hFile, NULL);
    if (size == 0 || size > 65534) { CloseHandle(hFile); return; }

    BYTE* buf = new BYTE[size + 2];
    memset(buf, 0, size + 2);
    DWORD read = 0;
    ReadFile(hFile, buf, size, &read, NULL);
    CloseHandle(hFile);

    // UTF-16LE -> wchar_t
    wchar_t* wbuf = (wchar_t*)buf;
    int wlen = size / 2;
    wchar_t* line = new wchar_t[wlen + 2];
    int lineLen = 0;
    for (int i = 0; i < wlen; i++) {
        if (wbuf[i] == L'\n' || wbuf[i] == L'\r') {
            line[lineLen] = 0;
            if (lineLen > 0) {
                wchar_t* eq = wcschr(line, L'=');
                if (eq) {
                    *eq = 0; wchar_t* key = line; wchar_t* val = eq + 1;
                    if (wcscmp(key, L"EncryptedPassword") == 0 && val[0]) {
                        std::vector<BYTE> combined = Base64Decode(val);
                        if (combined.size() >= 16) {
                            g_passwordIV.assign(combined.begin(), combined.begin() + 16);
                            g_encryptedPassword.assign(combined.begin() + 16, combined.end());
                        }
                    }
                }
            }
            lineLen = 0;
        } else {
            line[lineLen++] = wbuf[i];
        }
    }
    delete[] line;
    delete[] buf;
}

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
    bool success = CopyFileW(srcPath, dst, FALSE) != 0;

    // 【安全增强】设置图片缓存文件为隐藏属性
    if (success) {
        SetFileAttributesW(dst, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    }

    return success;
}

// ========== 新增:AES加密相关函数 ==========

// 从机器标识派生AES密钥
bool DeriveAESKey(std::vector<BYTE>& key) {
    char username[256] = {0};
    char computername[256] = {0};
    DWORD size;

    size = sizeof(username);
    if (!GetUserNameA(username, &size)) return false;

    size = sizeof(computername);
    if (!GetComputerNameA(computername, &size)) return false;

    // 拼接 username + computername
    std::string combined = std::string(username) + "@" + std::string(computername);

    // SHA-256 哈希作为AES密钥
    BCRYPT_ALG_HANDLE hHashAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&hHashAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status != 0) return false;

    DWORD hashLen = 0, resultLen = 0;
    BCryptGetProperty(hHashAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLen, sizeof(DWORD), &resultLen, 0);

    key.resize(hashLen);

    status = BCryptCreateHash(hHashAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (status == 0) {
        status = BCryptHashData(hHash, (PBYTE)combined.c_str(), (ULONG)combined.length(), 0);
        if (status == 0) {
            status = BCryptFinishHash(hHash, key.data(), hashLen, 0);
        }
        BCryptDestroyHash(hHash);
    }

    BCryptCloseAlgorithmProvider(hHashAlg, 0);
    return status == 0;
}

// AES-CBC 加密
bool EncryptPassword(const std::wstring& password, std::vector<BYTE>& ciphertext, std::vector<BYTE>& iv) {
    if (password.empty()) return false;

    std::vector<BYTE> key;
    if (!DeriveAESKey(key)) return false;

    BCRYPT_ALG_HANDLE hAesAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (status != 0) return false;

    // 设置CBC模式
    BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC,
                      (ULONG)(wcslen(BCRYPT_CHAIN_MODE_CBC) + 1) * sizeof(wchar_t), 0);

    // 生成随机IV
    iv.resize(16);
    BCryptGenRandom(NULL, iv.data(), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    // 密钥对象
    status = BCryptGenerateSymmetricKey(hAesAlg, &hKey, NULL, 0, key.data(), (ULONG)key.size(), 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAesAlg, 0);
        return false;
    }

    // 转换密码为UTF-8字节
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1, NULL, 0, NULL, NULL);
    std::vector<BYTE> plaintext(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1, (char*)plaintext.data(), utf8Len, NULL, NULL);
    plaintext.resize(utf8Len - 1);  // 去掉末尾null

    // PKCS7填充到16字节边界
    int padLen = 16 - (plaintext.size() % 16);
    plaintext.insert(plaintext.end(), padLen, (BYTE)padLen);

    // 加密
    DWORD cipherLen = 0, resultLen = 0;
    BCryptEncrypt(hKey, NULL, 0, NULL, 0, 0, NULL, 0, &cipherLen, BCRYPT_BLOCK_PADDING);
    ciphertext.resize(cipherLen);

    // 【修复】BCryptEncrypt 会就地修改传入的 iv 参数
    // 我们传一份拷贝给 BCryptEncrypt,不动原始 iv
    std::vector<BYTE> ivForEncrypt = iv;  // 拷贝

    status = BCryptEncrypt(hKey, plaintext.data(), (ULONG)plaintext.size(), NULL,
                          ivForEncrypt.data(), (ULONG)ivForEncrypt.size(), ciphertext.data(), cipherLen, &resultLen, 0);

    // iv 保持原值不变,不需要恢复

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAesAlg, 0);

    if (status == 0) {
        ciphertext.resize(resultLen);
        return true;
    }
    return false;
}

// AES-CBC 解密
bool DecryptPassword(const std::vector<BYTE>& ciphertext, const std::vector<BYTE>& iv, std::wstring& password) {
    if (ciphertext.empty() || iv.size() != 16) return false;

    std::vector<BYTE> key;
    if (!DeriveAESKey(key)) return false;

    BCRYPT_ALG_HANDLE hAesAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&hAesAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (status != 0) return false;

    BCryptSetProperty(hAesAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC,
                      (ULONG)(wcslen(BCRYPT_CHAIN_MODE_CBC) + 1) * sizeof(wchar_t), 0);

    status = BCryptGenerateSymmetricKey(hAesAlg, &hKey, NULL, 0, key.data(), (ULONG)key.size(), 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAesAlg, 0);
        return false;
    }

    DWORD plaintextLen = 1024, resultLen = 0;  // 直接分配足够大,避免探测问题

    std::vector<BYTE> plaintext(plaintextLen);

    // 【修复】BCryptDecrypt 会就地修改传入的 iv 参数
    std::vector<BYTE> ivForDecrypt = iv;  // 拷贝

    status = BCryptDecrypt(hKey, (PBYTE)ciphertext.data(), (ULONG)ciphertext.size(), NULL,
                          ivForDecrypt.data(), (ULONG)ivForDecrypt.size(), plaintext.data(), plaintextLen, &resultLen, BCRYPT_BLOCK_PADDING);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAesAlg, 0);

    if (status == 0 && resultLen > 0) {
        // 转换UTF-8到宽字符
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, (char*)plaintext.data(), resultLen, NULL, 0);
        password.resize(wideLen);
        MultiByteToWideChar(CP_UTF8, 0, (char*)plaintext.data(), resultLen, &password[0], wideLen);
        return true;
    }
    return false;
}

// ========== 新增:密码对话框 ==========

#define ID_PASSWORD_EDIT    4001
#define ID_CONFIRM_EDIT     4002
#define ID_OK_BUTTON        4003
#define ID_CANCEL_BUTTON    4004
#define ID_FORGOT_LINK      4005
#define ID_LOCK_TIMER       4006   // 新增:锁定倒计时定时器
#define ID_LOCK_HINT        4007   // 新增:锁定提示文本(STATIC)
#define ID_LINK_RESUME_TIMER 4008  // 新增:倒计时结束后延迟恢复"忘记密码"链接

struct PasswordDialogData {
    bool isSetupMode;  // true=设置密码, false=验证密码
    std::wstring password;
    std::wstring confirmPassword;
    bool forgotClicked;
    bool success;
    bool showForgotHint;   // 退出前弹出"忘记密码"提示
    bool showVerifyError;  // 验证密码时,退出前弹出"密码错误"提示
};

HWND g_hPasswordDlg = NULL;  // 当前密码对话框句柄(防止重复创建)
PasswordDialogData* g_pwdData = NULL;
volatile bool g_showingChildDialog = false;  // MessageBox等子对话框激活期间跳过IsDialogMessageW
static DWORD g_modalClosedTick = 0;          // 最近一次模态弹窗关闭时刻(过滤点击穿透)
static DWORD g_lockReleasedTick = 0;         // 锁定解除时刻(解锁后短暂不响应链接点击)
#define LINK_RESUME_DELAY_MS 2000           // 倒计时结束后链接恢复响应前的等待(毫秒)

// 前向声明(ShowPasswordDialog 先定义,这些函数后定义)
static volatile bool g_monitoringActive = false;
static void HideAllPrivacyWindows();
static void ShowAllPrivacyWindows();
static DWORD WINAPI ZOrderMonitorThread_Fullscreen(LPVOID lpParam);

// ================================================================
// 单窗口全屏登录界面:所有控件直接画在全屏 TOPMOST 窗口内,
// 没有遮罩+对话框的两层结构,彻底消除 Z 序竞争问题。
// 外观:深灰背景 + 正中央白色面板(带阴影) + 面板内所有控件
// ================================================================

// 圆角矩形 helper
static void DrawRoundedRect(HDC hdc, int x, int y, int w, int h, int radius, COLORREF fill) {
    HBRUSH hBrush = CreateSolidBrush(fill);
    HRGN hRgn = CreateRoundRectRgn(x, y, x + w, y + h, radius, radius);
    FillRgn(hdc, hRgn, hBrush);
    DeleteObject(hBrush);
    DeleteObject(hRgn);
}

// ========== 新增:密码失败锁定 工具函数 ==========
// 时间基准用 FILETIME(UTC,100ns)绝对时间,才能持久化并防止重启绕过。
static ULONGLONG NowAsFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((ULONGLONG)ft.dwHighDateTime << 32) | (ULONGLONG)ft.dwLowDateTime;
}
static bool IsLockedNow() {
    return g_lockUntilFT != 0 && NowAsFileTime() < g_lockUntilFT;
}
static ULONGLONG LockRemainingSeconds() {
    ULONGLONG now = NowAsFileTime();
    if (g_lockUntilFT == 0 || now >= g_lockUntilFT) return 0;
    return (g_lockUntilFT - now + FT_PER_SEC - 1) / FT_PER_SEC;  // 向上取整
}

// 把"累计失败次数 + 锁定截止时刻"写入隐藏文件,防止结束进程/重启绕过锁定
static void SaveLockState() {
    if (g_lockFilePath[0] == 0) return;
    HANDLE h = CreateFileW(g_lockFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    unsigned int hi = (unsigned int)(g_lockUntilFT >> 32);
    unsigned int lo = (unsigned int)(g_lockUntilFT & 0xFFFFFFFFULL);
    char buf[128];
    int len = wsprintfA(buf, "%d %08X%08X", g_pwdFailCount, hi, lo);
    DWORD written = 0;
    WriteFile(h, buf, len, &written, NULL);
    CloseHandle(h);
}
static void LoadLockState() {
    g_pwdFailCount = 0;
    g_lockUntilFT  = 0;
    if (g_lockFilePath[0] == 0) return;
    HANDLE h = CreateFileW(g_lockFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[128] = {0};
    DWORD read = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &read, NULL);
    CloseHandle(h);
    int cnt = 0;
    unsigned int hi = 0, lo = 0;
    if (sscanf(buf, "%d %8X%8X", &cnt, &hi, &lo) == 3) {
        if (cnt > 0) g_pwdFailCount = cnt;   // 计数不清零:达到6次后重启仍按3分钟档
        ULONGLONG until = ((ULONGLONG)hi << 32) | (ULONGLONG)lo;
        if (until > NowAsFileTime()) g_lockUntilFT = until;  // 只恢复尚未到期的锁定
    }
}
static void ClearLockState() {
    g_pwdFailCount = 0;
    g_lockUntilFT  = 0;
    if (g_lockFilePath[0] != 0) {
        SetFileAttributesW(g_lockFilePath, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(g_lockFilePath);
    }
}

static void UpdateLockHint(HWND hHint) {
    if (!hHint || !IsWindow(hHint)) return;
    ULONGLONG sec = LockRemainingSeconds();
    wchar_t prefix[64], text[128];
    Utf8ToWide(S_LOCK_HINT, prefix, 64);
    wsprintfW(text, L"%s %02u:%02u", prefix,
              (unsigned int)(sec / 60), (unsigned int)(sec % 60));
    SetWindowTextW(hHint, text);
    // 立即擦除并重绘:倒计时为透明静态文本,仅 SetWindowTextW 不会擦掉旧数字,
    // 每秒新数字会叠在旧字上。配合 WM_CTLCOLORSTATIC 的实心白底先擦净再画新字。
    RedrawWindow(hHint, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
}
// "重置密码"提示框:只由一次真实、当前有效的鼠标点击触发。
// 倒计时结束的那个时刻链接仍是隐藏且禁用的;刚解锁与刚关弹窗的冷却期内也拒绝。
// 因此"倒计时结束"这个事件本身不可能带出任何提示框。
static void TryShowResetPasswordHint(HWND hWnd, HWND hLink, HWND hEdit) {
    if (!hLink || !IsWindow(hLink)) return;
    if (g_showingChildDialog) return;
    if (IsLockedNow() || !IsWindowEnabled(hLink) || !IsWindowVisible(hLink)) return;
    DWORD now = GetTickCount();
    if (g_lockReleasedTick && (now - g_lockReleasedTick) < LINK_RESUME_DELAY_MS) return;
    if (g_modalClosedTick && (now - g_modalClosedTick) < 800) return;
    POINT pt; if (!GetCursorPos(&pt)) return;
    RECT rc; if (!GetWindowRect(hLink, &rc)) return;
    if (!PtInRect(&rc, pt)) return;
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t msg[1024];
    wsprintfW(msg,
        L"为保护隐私安全，请在远程断开连接后运行以下命令重置密码:\n\n& \"%s\" --reset",
        exePath);
    g_showingChildDialog = true;
    MessageBoxW(hWnd, msg, L"重置密码", MB_OK | MB_ICONINFORMATION);
    g_showingChildDialog = false;
    g_modalClosedTick = GetTickCount();
    if (hEdit && IsWindow(hEdit)) SetFocus(hEdit);
}

static void ApplyLockUI(HWND hWnd, HWND hEdit, HWND hBtnOK, HWND hLink, HWND hHint) {
    EnableWindow(hEdit, FALSE);
    EnableWindow(hBtnOK, FALSE);
    // 新增:锁定瞬间清空输入框残留,避免锁定期间仍显示上次输错的密码星号
    if (hEdit && IsWindow(hEdit)) SetWindowTextW(hEdit, L"");
    HWND hEdit2 = GetDlgItem(hWnd, ID_CONFIRM_EDIT);
    if (hEdit2 && IsWindow(hEdit2)) { EnableWindow(hEdit2, FALSE); SetWindowTextW(hEdit2, L""); }
    // 锁定期间直接隐藏"忘记密码"链接:既不显示也不接收任何点击,
    // 避免锁定期间的点击在倒计时结束后被排队消息派发成"忘记密码"。
    KillTimer(hWnd, ID_LINK_RESUME_TIMER);
    if (hLink && IsWindow(hLink)) {
        EnableWindow(hLink, FALSE);
        ShowWindow(hLink, SW_HIDE);
    }
    if (hHint && IsWindow(hHint)) {
        ShowWindow(hHint, SW_SHOW);
        UpdateLockHint(hHint);
    }
    SetFocus(hWnd);
    KillTimer(hWnd, ID_LOCK_TIMER);
    SetTimer(hWnd, ID_LOCK_TIMER, 250, NULL);
}
static void ClearLockUI(HWND hWnd, HWND hEdit, HWND hBtnOK, HWND hLink, HWND hHint) {
    KillTimer(hWnd, ID_LOCK_TIMER);
    g_lockReleasedTick = GetTickCount();      // 记录解锁时刻
    EnableWindow(hEdit, TRUE);
    EnableWindow(hBtnOK, TRUE);
    HWND hEdit2 = GetDlgItem(hWnd, ID_CONFIRM_EDIT);
    if (hEdit2 && IsWindow(hEdit2)) EnableWindow(hEdit2, TRUE);
    // 倒计时结束的这一刻不恢复链接:保持隐藏 + 禁用,
    // 改为启动一个延迟定时器;只有等到用户重新主动交互之后才重新显示。
    // 这样"倒计时结束"这个事件本身绝不可能带出任何提示框。
    if (hLink && IsWindow(hLink)) {
        EnableWindow(hLink, FALSE);
        ShowWindow(hLink, SW_HIDE);
    }
    KillTimer(hWnd, ID_LINK_RESUME_TIMER);
    SetTimer(hWnd, ID_LINK_RESUME_TIMER, LINK_RESUME_DELAY_MS, NULL);
    if (hHint && IsWindow(hHint)) {
        // 先取倒计时文字占用的矩形,隐藏后强制擦除重绘该区域,
        // 否则红色"已锁定,剩余 00:01"会以残影形式留在白色面板上。
        RECT rc; GetWindowRect(hHint, &rc);
        POINT p1 = { rc.left, rc.top }, p2 = { rc.right, rc.bottom };
        ScreenToClient(hWnd, &p1); ScreenToClient(hWnd, &p2);
        RECT rcCli = { p1.x, p1.y, p2.x, p2.y };
        SetWindowTextW(hHint, L"");
        ShowWindow(hHint, SW_HIDE);
        RedrawWindow(hWnd, &rcCli, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_NOCHILDREN);
    }
    SetWindowTextW(hEdit, L"");
    SetFocus(hEdit);
}

// 登记一次密码错误:累计次数 -> 按档位锁定 -> 持久化 -> 提示 -> 切换UI
static void RegisterPasswordFailure(HWND hWnd, HWND hEdit, HWND hBtnOK, HWND hLink, HWND hHint) {
    g_pwdFailCount++;
    int lockSec = 0;
    if (g_pwdFailCount >= LOCK_STAGE2_FAIL)       lockSec = LOCK_STAGE2_SEC;  // 第6次起:每次错误锁3分钟
    else if (g_pwdFailCount == LOCK_STAGE1_FAIL)  lockSec = LOCK_STAGE1_SEC;  // 仅第3次:锁1分钟(第4、5次不锁)
    if (lockSec > 0)
        g_lockUntilFT = NowAsFileTime() + (ULONGLONG)lockSec * FT_PER_SEC;
    SaveLockState();

    // 锁定分支不弹任何提示框:锁定状态由面板红色倒计时 + 控件禁用表达,
    // 也避免弹窗关闭时的点击穿透误触发"忘记密码"链接。
    // 未锁定(第1、2、4、5次)沿用原有"密码错误"提示。
    if (lockSec == 0) {
        wchar_t msg[128], errCap[64];
        Utf8ToWide(S_ERR_TITLE, errCap, 64);
        Utf8ToWide(S_PASSWORD_ERROR, msg, 128);
        g_showingChildDialog = true;
        MessageBoxW(hWnd, msg, errCap, MB_OK | MB_ICONERROR);
        g_showingChildDialog = false;
        g_modalClosedTick = GetTickCount();   // 抑制弹窗关闭瞬间的点击穿透
    }

    if (lockSec > 0) ApplyLockUI(hWnd, hEdit, hBtnOK, hLink, hHint);
    else { SetWindowTextW(hEdit, L""); SetFocus(hEdit); }
}

LRESULT CALLBACK PasswordWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 静态:控件句柄 + DPI 缩放
    static HWND hEdit1 = NULL, hEdit2 = NULL, hBtnOK = NULL, hBtnCancel = NULL, hLink = NULL;
    static HWND hLockHint = NULL;   // 新增:锁定倒计时提示文本
    static HFONT hFont = NULL, hLinkFont = NULL;
    static float g_scale = 1.0f;
    static int panelX = 0, panelY = 0, panelW = 0, panelH = 0;  // 白色面板位置(动态)
    static PasswordDialogData* g_pData = NULL;

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            g_pData = (PasswordDialogData*)cs->lpCreateParams;

            // DPI
            HDC hdc = GetDC(NULL);
            int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(NULL, hdc);
            g_scale = dpi / 96.0f;
            if (g_scale < 1.0f) g_scale = 1.0f;
            if (g_scale > 2.5f) g_scale = 2.5f;

            // 面板尺寸(随 DPI 缩放)
            int pw = (int)(400 * g_scale);
            int ph = g_pData->isSetupMode ? (int)(280 * g_scale) : (int)(240 * g_scale);
            // 居中
            panelW = pw; panelH = ph;
            panelX = (GetSystemMetrics(SM_CXSCREEN) - pw) / 2;
            panelY = (GetSystemMetrics(SM_CYSCREEN) - ph) / 2;

            // 字体
            int fontSize = (int)(16 * g_scale);
            hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            hLinkFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

            wchar_t buf[128];
            int cx = panelX;  // 面板内坐标(相对于窗口)
            int cy = panelY;

            // 标题
            int titleH = (int)(28 * g_scale);
            Utf8ToWide(g_pData->isSetupMode ? S_SET_PASSWORD_TITLE : S_INPUT_PASSWORD_TITLE, buf, 128);
            HWND hTitle = CreateWindowW(L"STATIC", buf,
                                        WS_VISIBLE | WS_CHILD | SS_CENTER,
                                        cx, cy + (int)(15 * g_scale), pw, titleH,
                                        hWnd, NULL, g_hInstance, NULL);
            SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);

            // 密码标签
            int labelH = (int)(20 * g_scale);
            int editH = (int)(30 * g_scale);
            int editW = (int)(280 * g_scale);
            int editX = cx + (pw - editW) / 2;
            int y = cy + (int)(55 * g_scale);
            Utf8ToWide(S_PASSWORD_LABEL, buf, 128);
            CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD,
                          editX, y, editW, labelH, hWnd, NULL, g_hInstance, NULL);
            y += labelH + (int)(5 * g_scale);
            hEdit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_VISIBLE | WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL,
                                     editX, y, editW, editH, hWnd, (HMENU)ID_PASSWORD_EDIT, g_hInstance, NULL);
            SendMessageW(hEdit1, EM_SETLIMITTEXT, 128, 0);

            if (g_pData->isSetupMode) {
                // 确认密码
                Utf8ToWide(S_CONFIRM_LABEL, buf, 128);
                CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD,
                              editX, y + editH + (int)(15 * g_scale), editW, labelH,
                              hWnd, NULL, g_hInstance, NULL);
                hEdit2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         WS_VISIBLE | WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL,
                                         editX, y + editH + labelH + (int)(20 * g_scale), editW, editH,
                                         hWnd, (HMENU)ID_CONFIRM_EDIT, g_hInstance, NULL);
                SendMessageW(hEdit2, EM_SETLIMITTEXT, 128, 0);
                y = y + editH * 2 + labelH + (int)(40 * g_scale);
            } else {
                // 忘记密码链接
                Utf8ToWide(S_FORGOT_PASSWORD, buf, 128);
                // 不使用 SS_NOTIFY:该控件永远不产生 STN_CLICKED 的 WM_COMMAND,
                // 提示框改由父窗口在一次完整的鼠标按下-抬起命中测试后弹出。
                // 这样任何排队/穿透/残留的 WM_COMMAND 都不可能带出"重置密码"框。
                hLink = CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD,
                                      editX, y + editH + (int)(5 * g_scale),
                                      (int)(100 * g_scale), labelH,
                                      hWnd, (HMENU)ID_FORGOT_LINK, g_hInstance, NULL);
                SendMessageW(hLink, WM_SETFONT, (WPARAM)hLinkFont, TRUE);
                y += editH + (int)(40 * g_scale);
            }

            // 按钮
            int btnW = (int)(90 * g_scale);
            int btnH = (int)(32 * g_scale);
            int btnGap = (int)(20 * g_scale);
            int totalBtnW = btnW * 2 + btnGap;
            int btnX = cx + (pw - totalBtnW) / 2;
            Utf8ToWide(S_OK_BTN, buf, 128);
            hBtnOK = CreateWindowW(L"BUTTON", buf, WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                                  btnX, y, btnW, btnH, hWnd, (HMENU)ID_OK_BUTTON, g_hInstance, NULL);
            Utf8ToWide(S_CANCEL, buf, 128);
            hBtnCancel = CreateWindowW(L"BUTTON", buf, WS_VISIBLE | WS_CHILD,
                                       btnX + btnW + btnGap, y, btnW, btnH,
                                       hWnd, (HMENU)ID_CANCEL_BUTTON, g_hInstance, NULL);

            // 设置字体
            if (hFont) {
                EnumChildWindows(hWnd, [](HWND hChild, LPARAM lp) -> BOOL {
                    SendMessageW(hChild, WM_SETFONT, (WPARAM)lp, TRUE);
                    return TRUE;
                }, (LPARAM)hFont);
            }
            // 新增:锁定倒计时提示文本(默认隐藏)
            hLockHint = CreateWindowW(L"STATIC", L"",
                                      WS_CHILD | SS_CENTER,
                                      cx + (int)(18 * g_scale), cy + ph - (int)(42 * g_scale),
                                      pw - (int)(36 * g_scale), (int)(22 * g_scale),
                                      hWnd, (HMENU)ID_LOCK_HINT, g_hInstance, NULL);
            if (hFont) SendMessageW(hLockHint, WM_SETFONT, (WPARAM)hFont, TRUE);

            SetFocus(hEdit1);

            // 新增:若仍处于锁定期(例如关闭对话框后重新触发热键),直接恢复锁定状态
            if (IsLockedNow()) {
                ApplyLockUI(hWnd, hEdit1, hBtnOK, hLink, hLockHint);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            if (hCtrl == hLink) {
                SetTextColor(hdc, RGB(0, 102, 204));
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)GetStockObject(NULL_BRUSH);
            } else if (hCtrl == hLockHint) {
                // 倒计时文本:返回与白色面板同色的实心刷,每次刷新先擦净控件矩形,
                // 避免透明背景下多次 SetWindowTextW 的数字层层叠加。
                SetTextColor(hdc, RGB(190, 30, 30));
                SetBkMode(hdc, OPAQUE);
                SetBkColor(hdc, RGB(255, 255, 255));
                return (INT_PTR)GetStockObject(WHITE_BRUSH);
            } else {
                SetTextColor(hdc, RGB(60, 60, 60));
                SetBkMode(hdc, TRANSPARENT);
                return (INT_PTR)GetStockObject(NULL_BRUSH);  // 不画背景，让 WM_ERASEBKGND 处理
            }
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc; GetClientRect(hWnd, &rc);

            // 1. 壁纸背景(复用隐私屏设置)
            PaintWallpaper(hdc, rc.right, rc.bottom);

            // 2. 暗色叠加层(全屏半透明暗色,增强遮挡)
            HBRUSH hDark = CreateSolidBrush(RGB(0, 0, 0));
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 180, 0};
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
            FillRect(hdcMem, &rc, hDark);
            AlphaBlend(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, rc.right, rc.bottom, bf);
            SelectObject(hdcMem, hOld); DeleteObject(hBmp); DeleteDC(hdcMem); DeleteObject(hDark);

            // 3. 白色面板(带圆角+阴影)
            HBRUSH hShadow = CreateSolidBrush(RGB(10, 10, 10));
            DrawRoundedRect(hdc, panelX + 4, panelY + 4, panelW, panelH, (int)(12 * g_scale), RGB(10, 10, 10));
            DeleteObject(hShadow);
            DrawRoundedRect(hdc, panelX, panelY, panelW, panelH, (int)(12 * g_scale), RGB(255, 255, 255));

            return 1;
        }

        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            if (id == ID_OK_BUTTON || (id == ID_PASSWORD_EDIT && HIWORD(wParam) == 0)) {
                // 新增:锁定期间一律拒绝验证(即使输入正确密码也不放行),防止绕过
                if (IsLockedNow()) {
                    // 锁定期间一律静默拒绝(不弹提示框),仅刷新倒计时
                    UpdateLockHint(hLockHint);
                    return 0;
                }
                wchar_t pwd1[256] = {0}, pwd2[256] = {0};
                GetWindowTextW(hEdit1, pwd1, 256);
                int pwdLen = (int)wcslen(pwd1);
                if (pwdLen == 0) {
                    wchar_t msg[128]; Utf8ToWide(S_PASSWORD_EMPTY, msg, 128);
                    g_showingChildDialog = true;
                    MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING); g_showingChildDialog = false;
                    g_modalClosedTick = GetTickCount();
                    SetFocus(hEdit1); return 0;
                }
                if (pwdLen < 8) {
                    wchar_t msg[128]; Utf8ToWide(S_PASSWORD_TOO_SHORT, msg, 128);
                    g_showingChildDialog = true;
                    MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING); g_showingChildDialog = false;
                    g_modalClosedTick = GetTickCount();
                    SetFocus(hEdit1); return 0;
                }
                if (pwdLen > 128) {
                    wchar_t msg[128]; Utf8ToWide(S_PASSWORD_TOO_LONG, msg, 128);
                    g_showingChildDialog = true;
                    MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING); g_showingChildDialog = false;
                    g_modalClosedTick = GetTickCount();
                    SetFocus(hEdit1); return 0;
                }
                if (g_pData->isSetupMode) {
                    GetWindowTextW(hEdit2, pwd2, 256);
                    if (wcscmp(pwd1, pwd2) != 0) {
                        wchar_t msg[128]; Utf8ToWide(S_PASSWORD_MISMATCH, msg, 128);
                        g_showingChildDialog = true;
                        MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING); g_showingChildDialog = false;
                    g_modalClosedTick = GetTickCount();
                        SetWindowTextW(hEdit1, L""); SetWindowTextW(hEdit2, L""); SetFocus(hEdit1); return 0;
                    }
                    g_pData->password = pwd1;
                } else {
                    std::wstring storedPassword;
                    if (DecryptPassword(g_encryptedPassword, g_passwordIV, storedPassword)) {
                        if (wcscmp(pwd1, storedPassword.c_str()) == 0) {
                            g_pData->password = pwd1;
                            g_pData->success = true;
                            ClearLockState();   // 新增:验证成功,清除失败计数与锁定状态
                            DestroyWindow(hWnd); return 0;
                        }
                    }
                    // 新增:登记失败次数并按规则锁定(锁定期间即使输对密码也无法解锁退出)
                    RegisterPasswordFailure(hWnd, hEdit1, hBtnOK, hLink, hLockHint);
                    return 0;
                }
                g_pData->success = true;
                DestroyWindow(hWnd); return 0;
            }
            if (id == ID_CANCEL_BUTTON) {
                g_pData->success = false;
                DestroyWindow(hWnd); return 0;
            }
            if (id == ID_FORGOT_LINK) {
                // 链接已去掉 SS_NOTIFY,正常情况下不会产生 WM_COMMAND。
                // 即便有残留/穿透消息到达这里,也一律拒绝:提示框只由下面的
                // WM_PARENTNOTIFY(WM_LBUTTONDOWN) 命中测试路径弹出。
                return 0;
            }
            break;
        }

        case WM_DESTROY: {
            KillTimer(hWnd, ID_LOCK_TIMER);   // 新增
            KillTimer(hWnd, ID_LINK_RESUME_TIMER);   // 新增
            if (hFont) { DeleteObject(hFont); hFont = NULL; }
            if (hLinkFont) { DeleteObject(hLinkFont); hLinkFont = NULL; }
            hLockHint = NULL;                 // 新增
            g_pData = NULL;
            return 0;
        }

        case WM_PARENTNOTIFY: {
            // 鼠标按下(子控件转发给父窗口)才可能弹提示;
            // HIWORD(wParam) 在鼠标消息下不保证是控件 ID,故不做该判断,
            // 由 TryShowResetPasswordHint 内的矩形命中测试决定。
            if (LOWORD(wParam) == WM_LBUTTONDOWN)
                TryShowResetPasswordHint(hWnd, hLink, hEdit1);
            break;
        }

        case WM_LBUTTONDOWN: {
            // 链接隐藏时鼠标消息落到父窗口;TryShow 会因链接不可见直接拒绝。
            // 双通道保证链接可见时点击一定能生效。
            TryShowResetPasswordHint(hWnd, hLink, hEdit1);
            break;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_RETURN) {
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_OK_BUTTON, 0), 0); return 0;
            }
            if (wParam == VK_ESCAPE) {
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_CANCEL_BUTTON, 0), 0); return 0;
            }
            break;
        }

        case WM_TIMER: {   // 新增:锁定倒计时
            if (wParam == ID_LOCK_TIMER) {
                if (!IsLockedNow()) {
                    ClearLockUI(hWnd, hEdit1, hBtnOK, hLink, hLockHint);  // 到点自动解除
                } else {
                    UpdateLockHint(hLockHint);
                }
                return 0;
            }
            if (wParam == ID_LINK_RESUME_TIMER) {
                // 延迟期满且当前不在锁定期,才重新显示"忘记密码"链接。
                KillTimer(hWnd, ID_LINK_RESUME_TIMER);
                if (!IsLockedNow() && hLink && IsWindow(hLink)) {
                    ShowWindow(hLink, SW_SHOW);
                    EnableWindow(hLink, TRUE);
                    InvalidateRect(hLink, NULL, TRUE);
                }
                g_lockReleasedTick = 0;   // 冷却结束,此后用户主动点击才生效
                return 0;
            }
            break;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ShowPasswordDialog:单窗口全屏版(彻底消除 Z 序竞争)
bool ShowPasswordDialog(bool isSetupMode, std::wstring& outPassword, bool& outForgotClicked, HWND hParentPrivacy = NULL) {
    if (g_hPasswordDlg && IsWindow(g_hPasswordDlg)) {
        SetForegroundWindow(g_hPasswordDlg); return false;
    }

    PasswordDialogData data;
    data.isSetupMode = isSetupMode;
    data.forgotClicked = false;
    data.success = false;
    data.showForgotHint = false;
    data.showVerifyError = false;

    // 注册窗口类
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = PasswordWndProc;
    wc.hInstance = g_hInstance;
    wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(20, 20, 20));
    wc.lpszClassName = L"PasswordFullscreenClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    // DPI 缩放
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    float scale = dpi / 96.0f;
    if (scale < 1.0f) scale = 1.0f;

    // 全屏窗口尺寸(虚拟屏幕,支持多显示器)
    int scrX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int scrY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int scrW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int scrH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // 步骤 1:隐藏所有隐私屏
    HideAllPrivacyWindows();

    // 步骤 2:创建全屏 TOPMOST 窗口(背景+面板一体化,无 Z 序竞争)
    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST,
                                L"PasswordFullscreenClass", NULL,
                                WS_POPUP | WS_VISIBLE,
                                scrX, scrY, scrW, scrH,
                                NULL, NULL, g_hInstance, &data);
    g_hPasswordDlg = hWnd;
    if (!hWnd) {
        ShowAllPrivacyWindows();
        UnregisterClassW(L"PasswordFullscreenClass", g_hInstance);
        return false;
    }

    // 立即激活
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
    SetFocus(hWnd);

    // 启动监控线程:10ms 轮询确保焦点始终在密码窗口
    g_monitoringActive = true;
    HANDLE hMonitorThread = CreateThread(NULL, 0, ZOrderMonitorThread_Fullscreen, (LPVOID)hWnd, 0, NULL);

    // 消息循环:子对话框(MessageBox等)激活期间跳过IsDialogMessageW,避免劫持其焦点消息
    MSG msg;
    while (IsWindow(hWnd) && GetMessageW(&msg, NULL, 0, 0)) {
        if (!g_showingChildDialog && !IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 清理
    g_monitoringActive = false;
    if (hMonitorThread) { WaitForSingleObject(hMonitorThread, INFINITE); CloseHandle(hMonitorThread); }
    g_hPasswordDlg = NULL;
    ShowAllPrivacyWindows();
    UnregisterClassW(L"PasswordFullscreenClass", g_hInstance);

    outPassword = data.password;
    outForgotClicked = data.forgotClicked;
    return data.success;
}

// 隐藏所有隐私屏窗口
static void HideAllPrivacyWindows() {
    for (HWND h : g_privacyWindows) {
        if (IsWindow(h)) ShowWindow(h, SW_HIDE);
    }
}

// 恢复所有隐私屏窗口显示
static void ShowAllPrivacyWindows() {
    for (HWND h : g_privacyWindows) {
        if (IsWindow(h)) ShowWindow(h, SW_SHOW);
    }
}

// 监控线程:10ms 轮询抢焦点
static DWORD WINAPI ZOrderMonitorThread_Fullscreen(LPVOID lpParam) {
    HWND hWnd = (HWND)lpParam;
    while (g_monitoringActive) {
        if (!IsWindow(hWnd)) break;
        HWND fg = GetForegroundWindow();
        if (fg != hWnd && !g_showingChildDialog) {
            SetForegroundWindow(hWnd);
            SetActiveWindow(hWnd);
        }
        Sleep(10);
    }
    return 0;
}

// ========== 新增:Windows凭据验证(系统样式) ==========

bool VerifyWindowsCredential() {
    // 标记已由 HandlePasswordFlow 设置,这里不再重复设置

    // 【修复】临时降低隐私屏窗口层级,让系统凭据对话框显示在上面
    for (HWND hPrivacy : g_privacyWindows) {
        SetWindowPos(hPrivacy, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    CREDUI_INFOW cui = {0};
    cui.cbSize = sizeof(CREDUI_INFOW);
    cui.hwndParent = NULL;
    cui.pszMessageText = L"验证 Windows 登录密码以重置隐私屏密码";
    cui.pszCaptionText = L"Windows 安全中心";
    cui.hbmBanner = NULL;

    ULONG authPackage = 0;
    LPVOID outCredBuffer = NULL;
    ULONG outCredSize = 0;
    BOOL save = FALSE;

    // CREDUIWIN_GENERIC: 显示通用凭据UI
    // CREDUIWIN_SECURE_PROMPT: 深色背景安全提示
    // CREDUIWIN_CHECKBOX: 显示"记住密码"复选框(可选)
    DWORD flags = CREDUIWIN_GENERIC | CREDUIWIN_SECURE_PROMPT;

    DWORD result = CredUIPromptForWindowsCredentialsW(
        &cui,
        0,  // dwAuthError
        &authPackage,
        NULL,  // pvInAuthBuffer
        0,     // ulInAuthBufferSize
        &outCredBuffer,
        &outCredSize,
        &save,
        flags
    );

    if (result != NO_ERROR || !outCredBuffer) {
        if (outCredBuffer) CoTaskMemFree(outCredBuffer);
        // 恢复隐私屏窗口 TOPMOST
        for (HWND hPrivacy : g_privacyWindows) {
            SetWindowPos(hPrivacy, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return false;
    }

    // 解包凭据获取用户名和密码
    wchar_t username[256] = {0};
    DWORD usernameLen = 256;
    wchar_t password[256] = {0};
    DWORD passwordLen = 256;

    if (!CredUnPackAuthenticationBufferW(0, outCredBuffer, outCredSize,
                                          username, &usernameLen, NULL, NULL,
                                          password, &passwordLen)) {
        CoTaskMemFree(outCredBuffer);
        // 恢复隐私屏窗口 TOPMOST
        for (HWND hPrivacy : g_privacyWindows) {
            SetWindowPos(hPrivacy, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return false;
    }

    CoTaskMemFree(outCredBuffer);

    // 验证凭据
    HANDLE hToken = NULL;
    BOOL logonOK = LogonUserW(username, NULL, password,
                              LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &hToken);

    SecureZeroMemory(password, sizeof(password));

    if (logonOK && hToken) {
        CloseHandle(hToken);
        // 恢复隐私屏窗口 TOPMOST
        for (HWND hPrivacy : g_privacyWindows) {
            SetWindowPos(hPrivacy, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return true;
    }

    DWORD err = GetLastError();
    // 1326 = 登录失败: 未知用户名或错误密码
    // 1909 = 帐户已锁定
    // 恢复隐私屏窗口 TOPMOST
    for (HWND hPrivacy : g_privacyWindows) {
        SetWindowPos(hPrivacy, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return false;  // 密码错误
}

// ========== 密码对话框 Z 序压制 ==========

// 强制 Z 序:遮罩→对话框(都在 TOPMOST)，并压制任务栏
// 独立后台线程:10ms 轮询抢焦点 + 维护 Z 序，不受主消息循环阻塞影响
void ShowHotkeyTooltip(int remaining) {
    wchar_t tip[128];
    wchar_t prefix[32], suffix[64];
    Utf8ToWide(S_HOTKEY_TIP_PREFIX, prefix, 32);
    Utf8ToWide(S_HOTKEY_TIP_SUFFIX, suffix, 64);
    wsprintfW(tip, L"%s%d%s", prefix, remaining, suffix);

    // 使用托盘气球提示
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = g_hTrayWnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 3000;

    // 转宽字符到ANSI (托盘图标用的是ANSI版本)
    char tipAnsi[256];
    WideCharToMultiByte(CP_ACP, 0, tip, -1, tipAnsi, sizeof(tipAnsi), NULL, NULL);
    lstrcpyA(nid.szInfo, tipAnsi);
    lstrcpyA(nid.szInfoTitle, "");

    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

// ========== 新增:密码验证完整流程 ==========

bool HandlePasswordFlow() {
    g_showingPasswordDialog = true;  // 在整个密码流程开始时设置标记

    bool hasPassword = !g_encryptedPassword.empty();

    if (!hasPassword) {
        // 首次使用:设置密码
        std::wstring password;
        bool forgotClicked;

        if (!ShowPasswordDialog(true, password, forgotClicked, g_privacyWindows.empty() ? NULL : g_privacyWindows[0])) {
            // 首次设置密码点"取消",继续运行隐私屏
            g_showingPasswordDialog = false;
            return false;  // 返回 false = 不退出隐私屏
        }

        // 加密并保存
        if (!EncryptPassword(password, g_encryptedPassword, g_passwordIV)) {
            g_showingPasswordDialog = false;
            return false;
        }
        SavePasswordSettings();

        // 设置密码成功后,立即弹出输入密码框验证(验证已在 dlg proc 中完成)
        std::wstring inputPassword;
        bool dummy;
        if (!ShowPasswordDialog(false, inputPassword, dummy, g_privacyWindows.empty() ? NULL : g_privacyWindows[0])) {
            // 点"取消",继续运行隐私屏
            g_showingPasswordDialog = false;
            return false;
        }

        // 验证成功
        g_showingPasswordDialog = false;
        return true;  // 退出隐私屏
    }

    // 验证密码流程(验证逻辑已移到 PasswordDlgProc 内部)
    while (true) {
        std::wstring inputPassword;
        bool forgotClicked = false;

        if (!ShowPasswordDialog(false, inputPassword, forgotClicked, g_privacyWindows.empty() ? NULL : g_privacyWindows[0])) {
            // 对话框返回 false:要么用户点了取消,要么点了忘记密码
            if (forgotClicked) {
                continue;  // 重新弹窗让用户输入
            }
            g_showingPasswordDialog = false;
            return false;  // 用户取消
        }

        // 密码正确(验证已在 dlg proc 中完成)
        g_showingPasswordDialog = false;
        return true;
    }
}  // HandlePasswordFlow 结束

LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // ========== 新增:热键处理 ==========
    if (msg == WM_HOTKEY && wParam == HOTKEY_EXIT_ID) {
        DWORD now = GetTickCount();

        // 检查是否超时
        if (g_hotkeyPressCount > 0 && (now - g_lastHotkeyTime) > HOTKEY_TIMEOUT_MS) {
            g_hotkeyPressCount = 0;
        }

        g_hotkeyPressCount++;
        g_lastHotkeyTime = now;

        if (g_hotkeyPressCount >= HOTKEY_TRIGGER_COUNT) {
            // 触发退出流程
            g_hotkeyPressCount = 0;

            if (HandlePasswordFlow()) {
                // 密码验证通过,退出程序
                DestroyWindow(hWnd);
            }
        } else {
            // 显示提示
            int remaining = HOTKEY_TRIGGER_COUNT - g_hotkeyPressCount;
            ShowHotkeyTooltip(remaining);
        }

        return 0;
    }

    // ========== 新增:热键超时重置定时器 ==========
    if (msg == WM_TIMER && wParam == TIMER_HOTKEY_RESET) {
        if (g_hotkeyPressCount > 0) {
            DWORD now = GetTickCount();
            if ((now - g_lastHotkeyTime) > HOTKEY_TIMEOUT_MS) {
                g_hotkeyPressCount = 0;
            }
        }
        return 0;
    }

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

        const char* scaleLabels[3] = {S_MENU_FIT, S_MENU_NONE, S_MENU_STRETCH};
        UINT scaleIds[3] = {ID_SCALE_FIT, ID_SCALE_NONE, ID_SCALE_STRETCH};
        for (int i = 0; i < 3; i++) {
            wchar_t tmp[16];
            Utf8ToWide(scaleLabels[i], tmp, 16);
            wchar_t item[32];
            if ((int)g_settings.scaleMode == i) {
                wsprintfW(item, L"\x25cf %s", tmp);
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
            // 关闭密码对话框(如果有)
            if (g_hPasswordDlg && IsWindow(g_hPasswordDlg)) {
                DestroyWindow(g_hPasswordDlg);
                g_hPasswordDlg = NULL;
            }
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
                SaveWallpaperSettings();
                RefreshAllWindows();
            }
            return 0;
        }

        if (id == ID_IMAGE_FILE) {
            OPENFILENAMEW ofn = { sizeof(ofn) };
            wchar_t file[MAX_PATH] = {0};

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
                    wchar_t extBuf[MAX_PATH];
                    const wchar_t* ext = wcsrchr(file, L'.');
                    if (ext) wsprintfW(extBuf, L"%s%s", g_imageCachePath, ext);
                    else extBuf[0] = 0;
                    LoadWallpaperImage(extBuf);
                    SaveWallpaperSettings();
                    RefreshAllWindows();
                }
            }
            return 0;
        }

        if (id >= ID_SCALE_FIT && id <= ID_SCALE_STRETCH) {
            g_settings.scaleMode = (ImageScaleMode)(id - ID_SCALE_FIT);
            SaveWallpaperSettings();
            RefreshAllWindows();
            return 0;
        }
    }

    if (msg == WM_DESTROY) {
        // 注销热键
        UnregisterHotKey(hWnd, HOTKEY_EXIT_ID);
        KillTimer(hWnd, TIMER_HOTKEY_RESET);

        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_TIMER && wParam == 1) {
        // 如果正在显示密码对话框,跳过置顶刷新
        if (g_showingPasswordDialog) {
            return 0;
        }
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
    (void)hPrevInstance; (void)nCmdShow;

    // 检查 --reset 参数
    if (strstr(lpCmdLine, "--reset") != NULL) {
        // 检查是否已有实例运行
        HANDLE mutex = CreateMutexA(NULL, TRUE, "PrivacyScreen_SingleInstance_Mutex");
        if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (mutex) CloseHandle(mutex);
            MessageBoxW(NULL, L"已有 DesktopPrivate 实例运行,--reset 参数不可使用。\n请先退出现有实例后再重置密码。", L"错误", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return 1;
        }

        // --reset 模式:弹出凭据验证 -> 设置密码 -> 退出
        // 不创建隐私屏窗口,只处理密码

        GetModuleDir();  // 初始化 g_configPath(不调用则为空)

        GdiplusStartupInput gsi;
        ULONG_PTR gdiplusToken;
        GdiplusStartup(&gdiplusToken, &gsi, NULL);

        // 【修复】设置高DPI感知,让密码框适配高分屏
        HMODULE hUser32Reset = LoadLibraryA("user32.dll");
        if (hUser32Reset) {
            typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
            SetProcessDpiAwarenessContextFunc pSetDpiAwarenessContext =
                (SetProcessDpiAwarenessContextFunc)GetProcAddress(hUser32Reset, "SetProcessDpiAwarenessContext");
            if (pSetDpiAwarenessContext) {
                pSetDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            }
            FreeLibrary(hUser32Reset);
        }

        LoadPasswordSettings();

        g_showingPasswordDialog = true;  // 标记正在处理密码流程

        // 弹出系统凭据对话框
        if (VerifyWindowsCredential()) {
            // 验证成功,弹出设置密码框
            std::wstring newPassword;
            bool dummy;
            if (ShowPasswordDialog(true, newPassword, dummy)) {
                // 用户点确定,设置新密码
                if (EncryptPassword(newPassword, g_encryptedPassword, g_passwordIV)) {
                    SavePasswordSettings();
                    ClearLockState();   // 新增:重置密码时一并清除失败锁定状态
                    g_showingPasswordDialog = false;
                    MessageBoxW(NULL, L"密码已重置。", L"成功", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
                }
            } else {
                // 用户点取消,放弃重置,保持原密码不变
                g_showingPasswordDialog = false;
                // 不弹提示,直接退出
            }
        } else {
            // 凭据验证失败,提示用户
            g_showingPasswordDialog = false;
            MessageBoxW(NULL, L"Windows 登录密码验证失败,密码未重置。\n请使用正确的 Windows 登录密码重试。", L"验证失败", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        }

        GdiplusShutdown(gdiplusToken);
        CloseHandle(mutex);
        return 0;
    }

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

    LoadWallpaperSettings();
    LoadPasswordSettings();
    LoadLockState();   // 新增:恢复未到期的锁定与累计失败次数,防止重启绕过

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

    // 注册热键 Ctrl-Alt-Shift-K
    RegisterHotKey(g_hTrayWnd, HOTKEY_EXIT_ID, MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'K');

    // 启动热键超时检测定时器
    SetTimer(g_hTrayWnd, TIMER_HOTKEY_RESET, 1000, NULL);

    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)hInstance);

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
