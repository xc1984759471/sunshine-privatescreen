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
wchar_t g_configPath[MAX_PATH] = {0};
wchar_t g_imageCachePath[MAX_PATH] = {0};

// ========== 新增：热键连按计数 ==========
int g_hotkeyPressCount = 0;
DWORD g_lastHotkeyTime = 0;
const int HOTKEY_TRIGGER_COUNT = 5;
const DWORD HOTKEY_TIMEOUT_MS = 5000;

// ========== 新增：密码相关 ==========
std::vector<BYTE> g_encryptedPassword;
std::vector<BYTE> g_passwordIV;

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
    
    // 【安全增强】配置文件存储在 %APPDATA%\DesktopPrivate\ 而非 exe 同目录
    // 防止用户误删或恶意篡改
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        wsprintfW(g_configPath, L"%s\\DesktopPrivate\\DesktopPrivate.ini", appDataPath);
        wsprintfW(g_imageCachePath, L"%s\\DesktopPrivate\\DesktopPrivatePic", appDataPath);
        
        // 确保 DesktopPrivate 目录存在
        CreateDirectoryW(appDataPath, NULL);  // 可能已存在，忽略错误
        wchar_t dirPath[MAX_PATH];
        wsprintfW(dirPath, L"%s\\DesktopPrivate", appDataPath);
        CreateDirectoryW(dirPath, NULL);  // 可能已存在，忽略错误
    } else {
        // 回退到 exe 同目录
        wsprintfW(g_configPath, L"%sDesktopPrivate.ini", g_exePath);
        wsprintfW(g_imageCachePath, L"%sDesktopPrivatePic", g_exePath);
    }
}

void SaveSettings() {
    // 修复：用 UTF-16LE 写入，避免 fwprintf + ccs=UTF-8 模式下的 wchar_t 截断 bug
    // (UTF-8 模式会将 wchar_t 高字节 \0 错误处理，导致只写入一个字符)
    FILE* f = _wfopen(g_configPath, L"w, ccs=UTF-16LE");
    if (!f) return;
    
    // 先输出 BOM (UTF-16LE)
    BYTE bom[] = {0xFF, 0xFE};
    fwrite(bom, 1, 2, f);
    
    fwprintf(f, L"[Wallpaper]\n");
    fwprintf(f, L"Mode=%d\n", g_settings.mode == MODE_COLOR ? 0 : 1);
    fwprintf(f, L"Color=%06X\n", (unsigned int)g_settings.color);
    fwprintf(f, L"ScaleMode=%d\n", (int)g_settings.scaleMode);
    fwprintf(f, L"ImagePath=%s\n", g_settings.imagePath.c_str());
    
    // 新增：保存加密密码
    if (!g_encryptedPassword.empty() && !g_passwordIV.empty()) {
        fwprintf(f, L"\n[Security]\n");
        
        // 将IV和密文合并后转base64
        std::vector<BYTE> combined;
        combined.insert(combined.end(), g_passwordIV.begin(), g_passwordIV.end());
        combined.insert(combined.end(), g_encryptedPassword.begin(), g_encryptedPassword.end());
        
        // Base64编码
        DWORD base64Len = 0;
        CryptBinaryToStringW((BYTE*)combined.data(), (DWORD)combined.size(), 
                             CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);
        if (base64Len > 0) {
            wchar_t* base64Str = new wchar_t[base64Len];
            CryptBinaryToStringW((BYTE*)combined.data(), (DWORD)combined.size(), 
                                 CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, base64Str, &base64Len);
            fwprintf(f, L"EncryptedPassword=%ls\n", base64Str);
            delete[] base64Str;
        }
    }
    
    fclose(f);
    
    // 【安全增强】设置配置文件为隐藏属性，防止用户误删或篡改
    SetFileAttributesW(g_configPath, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
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

void LoadSettings() {
    g_settings.mode = MODE_COLOR;
    g_settings.color = RGB(0, 0, 0);
    g_settings.scaleMode = SCALE_FIT;
    g_settings.imagePath.clear();
    g_encryptedPassword.clear();
    g_passwordIV.clear();

    FILE* f = _wfopen(g_configPath, L"r, ccs=UTF-16LE");
    if (!f) return;

    wchar_t line[2048];
    bool inSecurity = false;
    
    while (fgetws(line, 2048, f)) {
        wchar_t* eq = wcschr(line, L'=');
        if (!eq) {
            // 检查节名
            if (wcscmp(line, L"[Security]\n") == 0 || wcscmp(line, L"[Security]") == 0) {
                inSecurity = true;
            } else if (line[0] == L'[') {
                inSecurity = false;
            }
            continue;
        }
        
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
        else if (inSecurity && wcscmp(key, L"EncryptedPassword") == 0 && val[0]) {
            // Base64解码
            std::vector<BYTE> combined = Base64Decode(val);
            if (combined.size() >= 16) {
                // 前16字节是IV，剩余是密文
                g_passwordIV.assign(combined.begin(), combined.begin() + 16);
                g_encryptedPassword.assign(combined.begin() + 16, combined.end());
            }
        }
    }
    fclose(f);
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

// ========== 新增：AES加密相关函数 ==========

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
    // 我们传一份拷贝给 BCryptEncrypt，不动原始 iv
    std::vector<BYTE> ivForEncrypt = iv;  // 拷贝
    
    status = BCryptEncrypt(hKey, plaintext.data(), (ULONG)plaintext.size(), NULL, 
                          ivForEncrypt.data(), (ULONG)ivForEncrypt.size(), ciphertext.data(), cipherLen, &resultLen, 0);
    
    // iv 保持原值不变，不需要恢复
    
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
    
    DWORD plaintextLen = 1024, resultLen = 0;  // 直接分配足够大，避免探测问题
    
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

// ========== 新增：密码对话框 ==========

#define ID_PASSWORD_EDIT    4001
#define ID_CONFIRM_EDIT     4002
#define ID_OK_BUTTON        4003
#define ID_CANCEL_BUTTON    4004
#define ID_FORGOT_LINK      4005

struct PasswordDialogData {
    bool isSetupMode;  // true=设置密码, false=验证密码
    std::wstring password;
    std::wstring confirmPassword;
    bool forgotClicked;
    bool success;
    bool showForgotHint;   // 退出前弹出"忘记密码"提示
    bool showVerifyError;  // 验证密码时，退出前弹出"密码错误"提示
};

HWND g_hPasswordDlg = NULL;  // 当前密码对话框句柄（防止重复创建）
PasswordDialogData* g_pwdData = NULL;

LRESULT CALLBACK PasswordDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFont = NULL;
    static HFONT hLinkFont = NULL;  // 链接字体（蓝色下划线）
    static HWND hEdit1 = NULL, hEdit2 = NULL, hBtnOK = NULL, hBtnCancel = NULL, hLink = NULL;
    
    switch (msg) {
        case WM_CREATE: {
            PasswordDialogData* data = (PasswordDialogData*)((CREATESTRUCT*)lParam)->lpCreateParams;
            g_pwdData = data;
            
            // 【高分屏适配】获取 DPI 缩放比例
            HDC hdcDpi = GetDC(NULL);
            int dpi = GetDeviceCaps(hdcDpi, LOGPIXELSX);
            ReleaseDC(NULL, hdcDpi);
            float scale = dpi / 96.0f;
            if (scale < 1.0f) scale = 1.0f;
            if (scale > 2.5f) scale = 2.5f;
            
            // 创建字体（按 DPI 缩放）
            int fontSize = (int)(16 * scale);
            hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            
            // 创建链接字体（蓝色下划线）
            hLinkFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
            
            wchar_t buf[128];
            int y = (int)(20 * scale);
            int labelH = (int)(20 * scale);
            int editH = (int)(28 * scale);
            int editW = (int)(260 * scale);
            int editX = (int)(20 * scale);
            int gap1 = (int)(25 * scale);
            int gap2 = (int)(35 * scale);
            int gap3 = (int)(30 * scale);
            
            if (data->isSetupMode) {
                // 设置密码模式：两个输入框
                Utf8ToWide(S_PASSWORD_LABEL, buf, 128);
                CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD, editX, y, editW, labelH, hWnd, NULL, g_hInstance, NULL);
                y += gap1;
                hEdit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_VISIBLE | WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL,
                                        editX, y, editW, editH, hWnd, (HMENU)ID_PASSWORD_EDIT, g_hInstance, NULL);
                SendMessageW(hEdit1, EM_SETLIMITTEXT, 128, 0);
                
                y += gap2;
                Utf8ToWide(S_CONFIRM_LABEL, buf, 128);
                CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD, editX, y, editW, labelH, hWnd, NULL, g_hInstance, NULL);
                y += gap1;
                hEdit2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_VISIBLE | WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL,
                                        editX, y, editW, editH, hWnd, (HMENU)ID_CONFIRM_EDIT, g_hInstance, NULL);
            } else {
                // 验证密码模式：一个输入框+忘记密码链接
                Utf8ToWide(S_PASSWORD_LABEL, buf, 128);
                CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD, editX, y, editW, labelH, hWnd, NULL, g_hInstance, NULL);
                y += gap1;
                hEdit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_VISIBLE | WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL,
                                        editX, y, editW, editH, hWnd, (HMENU)ID_PASSWORD_EDIT, g_hInstance, NULL);
                SendMessageW(hEdit1, EM_SETLIMITTEXT, 128, 0);

                y += gap3;
                Utf8ToWide(S_FORGOT_PASSWORD, buf, 128);
                hLink = CreateWindowW(L"STATIC", buf, WS_VISIBLE | WS_CHILD | SS_NOTIFY,
                                     editX, y, (int)(100 * scale), labelH, hWnd, (HMENU)ID_FORGOT_LINK, g_hInstance, NULL);
                SendMessageW(hLink, WM_SETFONT, (WPARAM)hLinkFont, TRUE);
            }
            
            // 按钮
            y = data->isSetupMode ? (int)(140 * scale) : (int)(120 * scale);
            int btnW = (int)(90 * scale);
            int btnH = (int)(32 * scale);
            int btnGap = (int)(20 * scale);
            int totalBtnW = btnW * 2 + btnGap;
            int btnX = ((int)(420 * scale) - totalBtnW) / 2;  // 居中
            Utf8ToWide(S_OK_BTN, buf, 128);
            hBtnOK = CreateWindowW(L"BUTTON", buf, WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                                  btnX, y, btnW, btnH, hWnd, (HMENU)ID_OK_BUTTON, g_hInstance, NULL);
            Utf8ToWide(S_CANCEL, buf, 128);
            hBtnCancel = CreateWindowW(L"BUTTON", buf, WS_VISIBLE | WS_CHILD,
                                      btnX + btnW + btnGap, y, btnW, btnH, hWnd, (HMENU)ID_CANCEL_BUTTON, g_hInstance, NULL);
            
            // 设置字体
            if (hFont) {
                EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
                    SendMessageW(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
                    return TRUE;
                }, (LPARAM)hFont);
            }
            
            // 设置焦点到第一个编辑框
            SetFocus(hEdit1);
            return 0;
        }
        
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            
            // 为"忘记密码"链接设置蓝色文字
            if (hCtrl == hLink) {
                SetTextColor(hdc, RGB(0, 0, 255));  // 蓝色
            }
            
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        }
        
        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            
            if (id == ID_OK_BUTTON || id == ID_PASSWORD_EDIT && HIWORD(wParam) == 0) {
                // 确定按钮或Enter键
                wchar_t pwd1[256] = {0}, pwd2[256] = {0};
                GetWindowTextW(hEdit1, pwd1, 256);
                int pwdLen = (int)wcslen(pwd1);

                if (pwdLen == 0) {
                    wchar_t msg[128];
                    Utf8ToWide(S_PASSWORD_EMPTY, msg, 128);
                    MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING);
                    SetFocus(hEdit1);
                    return 0;
                }
                if (pwdLen < 8) {
                    wchar_t msg[128];
                    Utf8ToWide(S_PASSWORD_TOO_SHORT, msg, 128);
                    MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING);
                    SetFocus(hEdit1);
                    return 0;
                }
                if (pwdLen > 128) {
                    wchar_t msg[128];
                    Utf8ToWide(S_PASSWORD_TOO_LONG, msg, 128);
                    MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING);
                    SetFocus(hEdit1);
                    return 0;
                }

                if (g_pwdData->isSetupMode) {
                    GetWindowTextW(hEdit2, pwd2, 256);
                    if (wcscmp(pwd1, pwd2) != 0) {
                        wchar_t msg[128];
                        Utf8ToWide(S_PASSWORD_MISMATCH, msg, 128);
                        MessageBoxW(hWnd, msg, NULL, MB_OK | MB_ICONWARNING);
                        SetWindowTextW(hEdit1, L"");
                        SetWindowTextW(hEdit2, L"");
                        SetFocus(hEdit1);
                        return 0;
                    }
                    g_pwdData->password = pwd1;
                } else {
                    // 验证模式：在此处直接验证密码（失败则弹出错误提示，留在对话框中继续）
                    std::wstring storedPassword;
                    if (DecryptPassword(g_encryptedPassword, g_passwordIV, storedPassword)) {
                        if (wcscmp(pwd1, storedPassword.c_str()) == 0) {
                            g_pwdData->password = pwd1;
                            g_pwdData->success = true;
                            DestroyWindow(hWnd);
                            return 0;
                        }
                    }
                    // 验证失败：弹错误提示作为模态子窗口，清空密码框继续输入
                    wchar_t msg[128];
                    Utf8ToWide(S_PASSWORD_ERROR, msg, 128);
                    MessageBoxW(hWnd, msg, L"错误", MB_OK | MB_ICONERROR);
                    SetWindowTextW(hEdit1, L"");
                    SetFocus(hEdit1);
                    return 0;
                }
                
                g_pwdData->success = true;
                DestroyWindow(hWnd);
                return 0;
            }
            
            if (id == ID_CANCEL_BUTTON) {
                g_pwdData->success = false;
                DestroyWindow(hWnd);
                return 0;
            }
            
            if (id == ID_FORGOT_LINK || (hLink && (HWND)lParam == hLink)) {
                // 【关键修复】不销毁对话框、不改变成功标志。
                // 直接以对话框为父窗口弹出一个模式提示框，关闭后对话框仍在，
                // 避免被 50ms 隐私屏置顶定时器压住重建后的新对话框。
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                wchar_t msg[1024];
                wsprintfW(msg,
                    L"为了保证隐私信息安全，请在远程连接期间通过托盘图标手动退出，\n然后打开 PowerShell 运行以下命令:\n\n& \"%s\" --reset",
                    exePath);
                MessageBoxW(hWnd, msg, L"重置密码", MB_OK | MB_ICONINFORMATION);
                // 【修复】提示框关闭后，焦点归还到输入框（dlg 仍然存在）
                SetFocus(hEdit1);
                return 0;
            }
            
            break;
        }
        
        case WM_DESTROY: {
            if (hFont) { DeleteObject(hFont); hFont = NULL; }
            if (hLinkFont) { DeleteObject(hLinkFont); hLinkFont = NULL; }
            g_hPasswordDlg = NULL;
            g_pwdData = NULL;
            // 不要调用 PostQuitMessage，这会退出整个应用
            return 0;
        }
        
        case WM_KEYDOWN: {
            if (wParam == VK_RETURN) {
                // 回车键 = 点击确定按钮
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_OK_BUTTON, 0), 0);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                // ESC键 = 点击取消按钮
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_CANCEL_BUTTON, 0), 0);
                return 0;
            }
            break;
        }
    }
    
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 显示密码对话框
bool ShowPasswordDialog(bool isSetupMode, std::wstring& outPassword, bool& outForgotClicked) {
    // 防止重复创建
    if (g_hPasswordDlg && IsWindow(g_hPasswordDlg)) {
        SetForegroundWindow(g_hPasswordDlg);
        return false;
    }
    
    // 【调试】写入日志文件
    FILE* log = fopen("C:\\Users\\ab152\\.qclaw\\workspace\\dialog_debug.log", "a");
    if (log) {
        fprintf(log, "=== ShowPasswordDialog called, isSetupMode=%d ===\n", isSetupMode);
        fclose(log);
    }
    
    // 标记已由 HandlePasswordFlow 设置，这里不再重复设置
    
    PasswordDialogData data;
    data.isSetupMode = isSetupMode;
    data.forgotClicked = false;
    data.success = false;
    data.showForgotHint = false;
    data.showVerifyError = false;
    
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = PasswordDlgProc;
    wc.hInstance = g_hInstance;
    wc.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"PasswordDlgClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);
    
    wchar_t title[64];
    Utf8ToWide(isSetupMode ? S_SET_PASSWORD_TITLE : S_INPUT_PASSWORD_TITLE, title, 64);
    
    // 【修复】临时将隐私屏窗口降到密码对话框之下
    // 但仍保持在其他窗口之上（通过将密码框置于隐私屏之上实现）
    // 方法：先将密码框创建出来，再调整 Z 序
    
    // 【高分屏适配】根据系统 DPI 缩放窗口尺寸
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    float scale = dpi / 96.0f;  // 96 DPI 为 100%
    
    // 防止缩放过小或过大
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 2.5f) scale = 2.5f;
    
    int width = (int)(420 * scale);
    int height = isSetupMode ? (int)(230 * scale) : (int)(200 * scale);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;
    
    HWND hDlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
                                L"PasswordDlgClass", title,
                                WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                x, y, width, height,
                                NULL, NULL, g_hInstance, &data);
    
    g_hPasswordDlg = hDlg;  // 记录句柄
    
    // 【调试】检查窗口创建结果
    log = fopen("C:\\Users\\ab152\\.qclaw\\workspace\\dialog_debug.log", "a");
    if (log) {
        fprintf(log, "CreateWindowExW result: hDlg=%p, GetLastError=%lu\n", hDlg, GetLastError());
        fclose(log);
    }
    
    if (!hDlg) return false;
    
    // 【修复】临时降低隐私屏窗口层级（取消 TOPMOST），让对话框能显示在上面
    for (HWND hPrivacy : g_privacyWindows) {
        SetWindowPos(hPrivacy, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    
    // 将对话框设为 TOPMOST 并置于最前
    SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hDlg);
    SetActiveWindow(hDlg);
    SetFocus(hDlg);
    
    // 标准模态对话框消息循环
    // 使用 EnableWindow 禁用父窗口（如果有），这里 NULL 表示无父窗口
    EnableWindow(NULL, FALSE);
    
    MSG msg;
    int loopCount = 0;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        loopCount++;
        if (loopCount <= 10 || loopCount % 100 == 0) {
            log = fopen("C:\\Users\\ab152\\.qclaw\\workspace\\dialog_debug.log", "a");
            if (log) {
                fprintf(log, "Loop %d: IsWindow=%d, msg.message=%u\n", loopCount, IsWindow(hDlg), msg.message);
                fclose(log);
            }
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    EnableWindow(NULL, TRUE);
    
    // 【修复】保持 g_showingPasswordDialog = true 到返回调用者，避免调用者
    // 准备重弹窗口的空窗期内 50ms 定时器把隐私屏置顶、压住新窗口。
    // 调用者负责在最后 return 前设回 false。
    
    // 【修复】恢复隐私屏窗口的 Z 序（保持在最上层）
    for (HWND h : g_privacyWindows) {
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    
    UnregisterClassW(L"PasswordDlgClass", g_hInstance);
    
    log = fopen("C:\\Users\\ab152\\.qclaw\\workspace\\dialog_debug.log", "a");
    if (log) {
        fprintf(log, "Dialog finished, success=%d, password.length=%zu\n", data.success, data.password.length());
        fclose(log);
    }
    
    outPassword = data.password;
    outForgotClicked = data.forgotClicked;
    return data.success;
}

// ========== 新增：Windows凭据验证（系统样式） ==========

bool VerifyWindowsCredential() {
    // 标记已由 HandlePasswordFlow 设置，这里不再重复设置
    
    // 【修复】临时降低隐私屏窗口层级，让系统凭据对话框显示在上面
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
    // CREDUIWIN_CHECKBOX: 显示"记住密码"复选框（可选）
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

// ========== 新增：热键提示气泡 ==========

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

// ========== 新增：密码验证完整流程 ==========

bool HandlePasswordFlow() {
    g_showingPasswordDialog = true;  // 在整个密码流程开始时设置标记
    
    bool hasPassword = !g_encryptedPassword.empty();
    
    if (!hasPassword) {
        // 首次使用：设置密码
        std::wstring password;
        bool forgotClicked;
        
        if (!ShowPasswordDialog(true, password, forgotClicked)) {
            // 首次设置密码点"取消"，继续运行隐私屏
            g_showingPasswordDialog = false;
            return false;  // 返回 false = 不退出隐私屏
        }
        
        // 加密并保存
        if (!EncryptPassword(password, g_encryptedPassword, g_passwordIV)) {
            g_showingPasswordDialog = false;
            return false;
        }
        SaveSettings();
        
        // 设置密码成功后，立即弹出输入密码框验证（验证已在 dlg proc 中完成）
        std::wstring inputPassword;
        bool dummy;
        if (!ShowPasswordDialog(false, inputPassword, dummy)) {
            // 点"取消"，继续运行隐私屏
            g_showingPasswordDialog = false;
            return false;
        }
        
        // 验证成功
        g_showingPasswordDialog = false;
        return true;  // 退出隐私屏
    }
    
    // 验证密码流程（验证逻辑已移到 PasswordDlgProc 内部）
    while (true) {
        std::wstring inputPassword;
        bool forgotClicked = false;
        
        if (!ShowPasswordDialog(false, inputPassword, forgotClicked)) {
            // 对话框返回 false：要么用户点了取消，要么点了忘记密码
            if (forgotClicked) {
                continue;  // 重新弹窗让用户输入
            }
            g_showingPasswordDialog = false;
            return false;  // 用户取消
        }
        
        // 密码正确（验证已在 dlg proc 中完成）
        g_showingPasswordDialog = false;
        return true;
    }
}  // HandlePasswordFlow 结束

LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // ========== 新增：热键处理 ==========
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
                // 密码验证通过，退出程序
                DestroyWindow(hWnd);
            }
        } else {
            // 显示提示
            int remaining = HOTKEY_TRIGGER_COUNT - g_hotkeyPressCount;
            ShowHotkeyTooltip(remaining);
        }
        
        return 0;
    }
    
    // ========== 新增：热键超时重置定时器 ==========
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
            // 关闭密码对话框（如果有）
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
                SaveSettings();
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
        // 注销热键
        UnregisterHotKey(hWnd, HOTKEY_EXIT_ID);
        KillTimer(hWnd, TIMER_HOTKEY_RESET);
        
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_TIMER && wParam == 1) {
        // 如果正在显示密码对话框，跳过置顶刷新
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
            MessageBoxW(NULL, L"已有 DesktopPrivate 实例运行，--reset 参数不可使用。\n请先退出现有实例后再重置密码。", L"错误", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return 1;
        }
        
        // --reset 模式：弹出凭据验证 -> 设置密码 -> 退出
        // 不创建隐私屏窗口，只处理密码
        
        GdiplusStartupInput gsi;
        ULONG_PTR gdiplusToken;
        GdiplusStartup(&gdiplusToken, &gsi, NULL);
        
        // 【修复】设置高DPI感知，让密码框适配高分屏
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
        
        LoadSettings();
        
        g_showingPasswordDialog = true;  // 标记正在处理密码流程
        
        // 弹出系统凭据对话框
        if (VerifyWindowsCredential()) {
            // 验证成功，弹出设置密码框
            std::wstring newPassword;
            bool dummy;
            if (ShowPasswordDialog(true, newPassword, dummy)) {
                // 用户点确定，设置新密码
                if (EncryptPassword(newPassword, g_encryptedPassword, g_passwordIV)) {
                    SaveSettings();
                    g_showingPasswordDialog = false;
                    MessageBoxW(NULL, L"密码已重置。", L"成功", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
                }
            } else {
                // 用户点取消，放弃重置，保持原密码不变
                g_showingPasswordDialog = false;
                // 不弹提示，直接退出
            }
        } else {
            // 凭据验证失败，提示用户
            g_showingPasswordDialog = false;
            MessageBoxW(NULL, L"Windows 登录密码验证失败，密码未重置。\n请使用正确的 Windows 登录密码重试。", L"验证失败", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
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
