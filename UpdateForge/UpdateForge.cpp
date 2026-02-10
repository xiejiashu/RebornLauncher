#include "framework.h"
#include "UpdateForge.h"
#include "FileHash.h"

#include <json/json.h>
#include <zstd.h>

#include <shobjidl.h>
#include <commctrl.h>
#include <wincrypt.h>

#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <set>
#include <map>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <cwctype>
#include <algorithm>
#include <cstring>

using namespace std;
namespace fs = std::filesystem;

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr UINT WM_APP_PROGRESS = WM_APP + 3;
constexpr INT_PTR IDC_BTN_BROWSE = 1001;
constexpr INT_PTR IDC_CHK_ENCRYPT = 1002;
constexpr INT_PTR IDC_BTN_GENERATE = 1003;
constexpr INT_PTR IDC_BTN_JSON_BROWSE = 1004;
constexpr INT_PTR IDC_BTN_JSON_ENCRYPT = 1005;
constexpr INT_PTR IDC_BTN_JSON_DECRYPT = 1006;

constexpr char kBootstrapCryptoKey[] = "cDds!ErF9sIe6u$B";
constexpr char kVersionDatZstdDict[] = "D2Qbzy7hnmLh1zqgmDKx";

struct ProgressPayload
{
    int processed{};
    int total{};
};

struct FileTask
{
    std::wstring fullPath;
    std::wstring relPath;
    int64_t version;
};

struct FileResult
{
    std::wstring relPath;
    int64_t version;
    int64_t size;
    std::string md5;
};

static std::string NarrowACP(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0)
    {
        len = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (len <= 0)
        {
            return {};
        }
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
        return out;
    }

    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

static std::wstring GetControlText(HWND hWnd)
{
    int len = GetWindowTextLengthW(hWnd);
    if (len <= 0)
    {
        return {};
    }
    std::wstring text(static_cast<size_t>(len), L'\0');
    GetWindowTextW(hWnd, text.data(), len + 1);
    return text;
}

static std::string BytesToHexLower(const std::string& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char ch : bytes)
    {
        out.push_back(kHex[ch >> 4]);
        out.push_back(kHex[ch & 0x0F]);
    }
    return out;
}

static bool EncryptBootstrapPayloadToHex(const std::string& plain, std::string& hexOut, std::wstring& errorOut)
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HCRYPTKEY hKey = 0;
    auto cleanup = [&]() {
        if (hKey) CryptDestroyKey(hKey);
        if (hHash) CryptDestroyHash(hHash);
        if (hProv) CryptReleaseContext(hProv, 0);
    };

    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        errorOut = L"CryptAcquireContext failed";
        cleanup();
        return false;
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        errorOut = L"CryptCreateHash failed";
        cleanup();
        return false;
    }

    if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(kBootstrapCryptoKey), static_cast<DWORD>(strlen(kBootstrapCryptoKey)), 0))
    {
        errorOut = L"CryptHashData failed";
        cleanup();
        return false;
    }
    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey))
    {
        errorOut = L"CryptDeriveKey failed";
        cleanup();
        return false;
    }

    DWORD dataLen = static_cast<DWORD>(plain.size());
    DWORD bufferLen = dataLen + 32;
    std::string buffer(bufferLen, '\0');
    if (dataLen > 0)
    {
        memcpy(buffer.data(), plain.data(), dataLen);
    }

    if (!CryptEncrypt(hKey, 0, TRUE, 0, reinterpret_cast<BYTE*>(buffer.data()), &dataLen, bufferLen))
    {
        errorOut = L"CryptEncrypt failed";
        cleanup();
        return false;
    }

    cleanup();
    buffer.resize(dataLen);
    hexOut = BytesToHexLower(buffer);
    return true;
}

static bool HexToBytes(const std::wstring& text, std::string& bytesOut, std::wstring& errorOut)
{
    std::string hex;
    hex.reserve(text.size());
    for (wchar_t ch : text)
    {
        if (iswxdigit(ch))
        {
            hex.push_back(static_cast<char>(towlower(ch)));
        }
    }

    if (hex.empty())
    {
        errorOut = L"No hex data found in input.";
        return false;
    }
    if ((hex.size() % 2) != 0)
    {
        errorOut = L"Hex data length must be even.";
        return false;
    }

    std::string decoded;
    decoded.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        char pair[3] = { hex[i], hex[i + 1], 0 };
        char* endPtr = nullptr;
        const long value = strtol(pair, &endPtr, 16);
        if (endPtr == nullptr || *endPtr != '\0')
        {
            errorOut = L"Invalid hex value.";
            return false;
        }
        decoded.push_back(static_cast<char>(value & 0xFF));
    }

    bytesOut = std::move(decoded);
    return true;
}

static bool DecryptBootstrapPayloadFromHex(const std::wstring& hexText, std::string& plainOut, std::wstring& errorOut)
{
    std::string cipherBytes;
    if (!HexToBytes(hexText, cipherBytes, errorOut))
    {
        return false;
    }

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HCRYPTKEY hKey = 0;
    auto cleanup = [&]() {
        if (hKey) CryptDestroyKey(hKey);
        if (hHash) CryptDestroyHash(hHash);
        if (hProv) CryptReleaseContext(hProv, 0);
    };

    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        errorOut = L"CryptAcquireContext failed";
        cleanup();
        return false;
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
    {
        errorOut = L"CryptCreateHash failed";
        cleanup();
        return false;
    }
    if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(kBootstrapCryptoKey), static_cast<DWORD>(strlen(kBootstrapCryptoKey)), 0))
    {
        errorOut = L"CryptHashData failed";
        cleanup();
        return false;
    }
    if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey))
    {
        errorOut = L"CryptDeriveKey failed";
        cleanup();
        return false;
    }

    DWORD dataLen = static_cast<DWORD>(cipherBytes.size());
    std::string plain(cipherBytes);
    if (!CryptDecrypt(hKey, 0, TRUE, 0, reinterpret_cast<BYTE*>(plain.data()), &dataLen))
    {
        errorOut = L"CryptDecrypt failed";
        cleanup();
        return false;
    }
    plain.resize(dataLen);
    cleanup();

    plainOut = std::move(plain);
    return true;
}

static std::wstring GetSettingsIniPath()
{
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring path = modulePath;
    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        path.resize(pos + 1);
    }
    path.append(L"UpdateForge.settings.ini");
    return path;
}

static std::wstring PickFolder(HWND owner)
{
    std::wstring result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        DWORD opts = 0;
        if (SUCCEEDED(dialog->GetOptions(&opts)))
        {
            dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        if (SUCCEEDED(dialog->Show(owner)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)))
                {
                    result = psz;
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    return result;
}

static std::wstring PickJsonFile(HWND owner)
{
    std::wstring result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
    {
        DWORD opts = 0;
        if (SUCCEEDED(dialog->GetOptions(&opts)))
        {
            dialog->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        }

        COMDLG_FILTERSPEC specs[] = {
            { L"JSON files (*.json;*.txt)", L"*.json;*.txt" },
            { L"All files (*.*)", L"*.*" }
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(specs)), specs);
        dialog->SetFileTypeIndex(1);
        dialog->SetTitle(L"Select JSON file");

        if (SUCCEEDED(dialog->Show(owner)))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)))
                {
                    result = psz;
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    return result;
}

static bool ReadTextFileUtf8(const std::wstring& path, std::string& textOut, std::wstring& errorOut)
{
    std::ifstream ifs(fs::path(path), std::ios::binary);
    if (!ifs)
    {
        errorOut = L"Failed to open selected file.";
        return false;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    if (!ifs.good() && !ifs.eof())
    {
        errorOut = L"Failed to read selected file.";
        return false;
    }

    std::string content = oss.str();
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
    {
        content.erase(0, 3);
    }

    if (content.empty())
    {
        errorOut = L"Selected file is empty.";
        return false;
    }

    textOut = std::move(content);
    return true;
}

static std::string CompressVersionDatZstd(const std::string& plain)
{
    if (plain.empty()) {
        return {};
    }

    ZSTD_CDict* cdict = ZSTD_createCDict(kVersionDatZstdDict, strlen(kVersionDatZstdDict), 3);
    if (!cdict) {
        return {};
    }

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (!cctx) {
        ZSTD_freeCDict(cdict);
        return {};
    }

    const size_t bound = ZSTD_compressBound(plain.size());
    std::string compressed(bound, '\0');
    const size_t compressedSize = ZSTD_compress_usingCDict(
        cctx,
        compressed.data(),
        compressed.size(),
        plain.data(),
        plain.size(),
        cdict);

    ZSTD_freeCCtx(cctx);
    ZSTD_freeCDict(cdict);

    if (ZSTD_isError(compressedSize) != 0) {
        return {};
    }

    compressed.resize(compressedSize);
    return compressed;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    UpdateForgeApp app(hInstance);
    int ret = app.Run(nCmdShow);

    CoUninitialize();
    return ret;
}

UpdateForgeApp::UpdateForgeApp(HINSTANCE hInst)
    : m_hInst(hInst)
{
}

int UpdateForgeApp::Run(int nCmdShow)
{
    InitWindow(nCmdShow);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void UpdateForgeApp::InitWindow(int nCmdShow)
{
    const wchar_t* CLASS_NAME = L"UpdateForgeMainWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = UpdateForgeApp::WndProc;
    wc.hInstance = m_hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassExW(&wc);

    const std::wstring iniPath = GetSettingsIniPath();
    int winWidth = static_cast<int>(GetPrivateProfileIntW(L"ui", L"window_width", 760, iniPath.c_str()));
    int winHeight = static_cast<int>(GetPrivateProfileIntW(L"ui", L"window_height", 540, iniPath.c_str()));
    winWidth = (std::max)(760, winWidth);
    winHeight = (std::max)(540, winHeight);

    m_hWnd = CreateWindowExW(0, CLASS_NAME, L"UpdateForge Version Builder", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, winWidth, winHeight, nullptr, nullptr, m_hInst, this);

    CreateControls();

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
}

void UpdateForgeApp::CreateControls()
{
    HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    int x = 16;
    int y = 18;
    m_lblPath = CreateWindowExW(0, L"STATIC", L"更新目录/Folder:", WS_CHILD | WS_VISIBLE, x, y, 90, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_editPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x + 95, y - 2, 420, 24, m_hWnd, nullptr, m_hInst, nullptr);
    m_btnBrowse = CreateWindowExW(0, L"BUTTON", L"浏览/Browse", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x + 525, y - 2, 90, 24, m_hWnd, reinterpret_cast<HMENU>(IDC_BTN_BROWSE), m_hInst, nullptr);
    y += 36;
    m_lblKey = CreateWindowExW(0, L"STATIC", L"密钥/Key:", WS_CHILD | WS_VISIBLE, x, y, 90, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_editKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x + 95, y - 2, 220, 24, m_hWnd, nullptr, m_hInst, nullptr);
    m_chkEncrypt = CreateWindowExW(0, L"BUTTON", L"Enable ZSTD(dict)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x + 330, y - 2, 140, 24, m_hWnd, reinterpret_cast<HMENU>(IDC_CHK_ENCRYPT), m_hInst, nullptr);
    m_btnGenerate = CreateWindowExW(0, L"BUTTON", L"生成Version.dat/Generate", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x + 480, y - 2, 160, 24, m_hWnd, reinterpret_cast<HMENU>(IDC_BTN_GENERATE), m_hInst, nullptr);
    y += 34;
    m_lblUrlInput = CreateWindowExW(0, L"STATIC", L"JSON File:", WS_CHILD | WS_VISIBLE, x, y, 90, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_editUrlInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x + 95, y - 2, 420, 24, m_hWnd, nullptr, m_hInst, nullptr);
    m_btnJsonBrowse = CreateWindowExW(0, L"BUTTON", L"Browse JSON", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x + 525, y - 2, 120, 24, m_hWnd, reinterpret_cast<HMENU>(IDC_BTN_JSON_BROWSE), m_hInst, nullptr);
    y += 32;
    m_lblUrlOutput = CreateWindowExW(0, L"STATIC", L"Payload:", WS_CHILD | WS_VISIBLE, x, y, 90, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_btnEncryptUrl = CreateWindowExW(0, L"BUTTON", L"Encrypt JSON", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x + 95, y - 2, 130, 24, m_hWnd, reinterpret_cast<HMENU>(IDC_BTN_JSON_ENCRYPT), m_hInst, nullptr);
    m_btnDecryptPayload = CreateWindowExW(0, L"BUTTON", L"Decrypt Text", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x + 235, y - 2, 130, 24, m_hWnd, reinterpret_cast<HMENU>(IDC_BTN_JSON_DECRYPT), m_hInst, nullptr);
    y += 34;
    m_editUrlOutput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL,
        x + 95, y - 2, 560, 84, m_hWnd, nullptr, m_hInst, nullptr);
    y += 88;
    m_lblLog = CreateWindowExW(0, L"STATIC", L"日志/Log:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_editLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        x, y + 20, 700, 360, m_hWnd, nullptr, m_hInst, nullptr);
    HWND controls[] = {
        m_lblPath, m_editPath, m_btnBrowse,
        m_lblKey, m_editKey, m_chkEncrypt, m_btnGenerate,
        m_lblUrlInput, m_editUrlInput, m_btnJsonBrowse, m_lblUrlOutput, m_btnEncryptUrl, m_btnDecryptPayload, m_editUrlOutput,
        m_lblLog, m_editLog
    };
    for (HWND h : controls)
    {
        SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    }
    m_statusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, m_hWnd, nullptr, m_hInst, nullptr);
    m_statusProgress = CreateWindowExW(
        0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        0, 0, 0, 0, m_statusBar, nullptr, m_hInst, nullptr);
    m_statusProgressText = CreateWindowExW(
        WS_EX_TRANSPARENT, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0, m_statusBar, nullptr, m_hInst, nullptr);

    SendMessageW(m_statusProgress, PBM_SETRANGE32, 0, 100);
    SendMessageW(m_statusProgressText, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    UpdateStatusText(L"\u72b6\u6001: \u5c31\u7eea / Status: Ready");
    UpdateProgress(0, 0);

    LoadCachedSettings();

    RECT rc{};
    GetClientRect(m_hWnd, &rc);
    LayoutControls(rc.right - rc.left, rc.bottom - rc.top);
}

void UpdateForgeApp::LoadCachedSettings()
{
    const std::wstring iniPath = GetSettingsIniPath();
    wchar_t pathBuf[4096]{};
    wchar_t keyBuf[4096]{};
    GetPrivateProfileStringW(L"ui", L"update_dir", L"", pathBuf, static_cast<DWORD>(std::size(pathBuf)), iniPath.c_str());
    GetPrivateProfileStringW(L"ui", L"encrypt_key", L"", keyBuf, static_cast<DWORD>(std::size(keyBuf)), iniPath.c_str());

    std::wstring updateDir = pathBuf;
    if (updateDir.empty())
    {
        wchar_t cwd[MAX_PATH]{};
        if (GetCurrentDirectoryW(MAX_PATH, cwd))
        {
            updateDir = cwd;
            updateDir.append(L"\\Update");
        }
    }

    SetWindowTextW(m_editPath, updateDir.c_str());
    SetWindowTextW(m_editKey, keyBuf);
    const int encryptEnabled = GetPrivateProfileIntW(L"ui", L"encrypt_enabled", 0, iniPath.c_str());
    SendMessageW(m_chkEncrypt, BM_SETCHECK, encryptEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SyncEncryptUiState();
}

void UpdateForgeApp::SaveCachedSettings()
{
    const std::wstring iniPath = GetSettingsIniPath();
    wchar_t pathBuf[4096]{};
    wchar_t keyBuf[4096]{};
    GetWindowTextW(m_editPath, pathBuf, static_cast<int>(std::size(pathBuf)));
    GetWindowTextW(m_editKey, keyBuf, static_cast<int>(std::size(keyBuf)));
    WritePrivateProfileStringW(L"ui", L"update_dir", pathBuf, iniPath.c_str());
    WritePrivateProfileStringW(L"ui", L"encrypt_key", keyBuf, iniPath.c_str());
    const bool encryptEnabled = (SendMessageW(m_chkEncrypt, BM_GETCHECK, 0, 0) == BST_CHECKED);
    WritePrivateProfileStringW(L"ui", L"encrypt_enabled", encryptEnabled ? L"1" : L"0", iniPath.c_str());

    if (!m_hWnd || !IsWindow(m_hWnd))
    {
        return;
    }

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(m_hWnd, &placement))
    {
        return;
    }

    int width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
    int height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
    width = (std::max)(width, 760);
    height = (std::max)(height, 540);

    const std::wstring widthText = std::to_wstring(width);
    const std::wstring heightText = std::to_wstring(height);
    WritePrivateProfileStringW(L"ui", L"window_width", widthText.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"ui", L"window_height", heightText.c_str(), iniPath.c_str());
}

void UpdateForgeApp::SyncEncryptUiState()
{
    const bool encryptEnabled = (SendMessageW(m_chkEncrypt, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const BOOL keyEditable = (!m_isBusy && encryptEnabled) ? TRUE : FALSE;
    EnableWindow(m_editKey, keyEditable);
    SendMessageW(m_editKey, EM_SETREADONLY, keyEditable ? FALSE : TRUE, 0);
}

void UpdateForgeApp::LayoutControls(int width, int height)
{
    const int margin = 16;
    const int labelWidth = 90;
    const int rowHeight = 24;
    const int spacing = 8;
    const int browseWidth = 110;
    const int checkWidth = 170;
    const int generateWidth = 220;
    const int jsonBrowseWidth = 120;
    const int statusHeight = 24;

    int clientW = (std::max)(width, 760);
    int clientH = (std::max)(height, 540);

    int y = 18;
    const int inputLeft = margin + labelWidth + 5;
    const int rightEdge = clientW - margin;

    MoveWindow(m_lblPath, margin, y + 2, labelWidth, 20, TRUE);
    int pathEditWidth = rightEdge - browseWidth - spacing - inputLeft;
    pathEditWidth = (std::max)(pathEditWidth, 120);
    MoveWindow(m_editPath, inputLeft, y, pathEditWidth, rowHeight, TRUE);
    MoveWindow(m_btnBrowse, inputLeft + pathEditWidth + spacing, y, browseWidth, rowHeight, TRUE);

    y += 36;
    MoveWindow(m_lblKey, margin, y + 2, labelWidth, 20, TRUE);
    int generateX = rightEdge - generateWidth;
    int checkX = generateX - spacing - checkWidth;
    int keyEditWidth = checkX - spacing - inputLeft;
    keyEditWidth = (std::max)(keyEditWidth, 120);
    MoveWindow(m_editKey, inputLeft, y, keyEditWidth, rowHeight, TRUE);
    MoveWindow(m_chkEncrypt, checkX, y, checkWidth, rowHeight, TRUE);
    MoveWindow(m_btnGenerate, generateX, y, generateWidth, rowHeight, TRUE);
    y += 34;
    MoveWindow(m_lblUrlInput, margin, y + 2, labelWidth, 20, TRUE);
    int jsonInputWidth = rightEdge - inputLeft - spacing - jsonBrowseWidth;
    jsonInputWidth = (std::max)(jsonInputWidth, 140);
    MoveWindow(m_editUrlInput, inputLeft, y, jsonInputWidth, rowHeight, TRUE);
    MoveWindow(m_btnJsonBrowse, inputLeft + jsonInputWidth + spacing, y, jsonBrowseWidth, rowHeight, TRUE);
    y += 32;
    MoveWindow(m_lblUrlOutput, margin, y + 2, labelWidth, 20, TRUE);
    MoveWindow(m_btnEncryptUrl, inputLeft, y, 130, rowHeight, TRUE);
    MoveWindow(m_btnDecryptPayload, inputLeft + 130 + spacing, y, 130, rowHeight, TRUE);
    y += 34;
    const int payloadHeight = 96;
    MoveWindow(m_editUrlOutput, inputLeft, y, rightEdge - inputLeft, payloadHeight, TRUE);
    y += payloadHeight + 8;
    MoveWindow(m_lblLog, margin, y, 120, 20, TRUE);
    y += 20;
    int logHeight = (std::max)(120, clientH - y - margin - statusHeight - 4);
    MoveWindow(m_editLog, margin, y, clientW - margin * 2, logHeight, TRUE);

    if (m_statusBar)
    {
        MoveWindow(m_statusBar, 0, clientH - statusHeight, clientW, statusHeight, TRUE);
        SendMessageW(m_statusBar, WM_SIZE, 0, 0);

        int parts[] = { 260, -1 };
        SendMessageW(m_statusBar, SB_SETPARTS, static_cast<WPARAM>(std::size(parts)), reinterpret_cast<LPARAM>(parts));

        RECT rcPart{};
        SendMessageW(m_statusBar, SB_GETRECT, 1, reinterpret_cast<LPARAM>(&rcPart));
        const int inset = 4;
        int barX = rcPart.left + inset;
        int barY = rcPart.top + inset;
        int barW = (std::max)(20, static_cast<int>((rcPart.right - rcPart.left) - inset * 2));
        int barH = (std::max)(12, static_cast<int>((rcPart.bottom - rcPart.top) - inset * 2));
        MoveWindow(m_statusProgress, barX, barY, barW, barH, TRUE);
        MoveWindow(m_statusProgressText, barX, barY, barW, barH, TRUE);
    }
}
void UpdateForgeApp::OnBrowse()
{
    auto path = PickFolder(m_hWnd);
    if (!path.empty())
    {
        SetWindowTextW(m_editPath, path.c_str());
        SaveCachedSettings();
    }
}

void UpdateForgeApp::OnBrowseJsonFile()
{
    auto path = PickJsonFile(m_hWnd);
    if (!path.empty())
    {
        SetWindowTextW(m_editUrlInput, path.c_str());
    }
}

void UpdateForgeApp::OnEncryptJsonPayload()
{
    const std::wstring jsonPath = GetControlText(m_editUrlInput);
    if (jsonPath.empty())
    {
        AppendLogAsync(L"Please select a JSON file first.");
        return;
    }
    std::string plain;
    std::wstring error;
    if (!ReadTextFileUtf8(jsonPath, plain, error))
    {
        AppendLogAsync(L"Read JSON failed: " + error);
        return;
    }
    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string parseErrors;
    std::istringstream iss(plain);
    if (!Json::parseFromStream(readerBuilder, iss, &root, &parseErrors))
    {
        AppendLogAsync(L"Selected file is not valid JSON.");
        return;
    }
    std::string hexCipher;
    if (!EncryptBootstrapPayloadToHex(plain, hexCipher, error))
    {
        AppendLogAsync(L"JSON encryption failed: " + error);
        return;
    }
    SetWindowTextW(m_editUrlOutput, Utf8ToWide(hexCipher).c_str());
    AppendLogAsync(L"JSON payload encrypted. Hex output is shown in Payload box.");
}
void UpdateForgeApp::OnDecryptJsonPayload()
{
    const std::wstring payloadText = GetControlText(m_editUrlOutput);
    if (payloadText.empty())
    {
        AppendLogAsync(L"Payload box is empty.");
        return;
    }
    std::string plain;
    std::wstring error;
    if (!DecryptBootstrapPayloadFromHex(payloadText, plain, error))
    {
        AppendLogAsync(L"JSON decryption failed: " + error);
        return;
    }
    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string parseErrors;
    std::istringstream iss(plain);
    if (Json::parseFromStream(readerBuilder, iss, &root, &parseErrors))
    {
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "  ";
        plain = Json::writeString(writerBuilder, root);
    }
    SetWindowTextW(m_editUrlOutput, Utf8ToWide(plain).c_str());
    AppendLogAsync(L"Payload decrypted. JSON is shown in Payload box.");
}
void UpdateForgeApp::OnGenerate()
{
    if (m_hWorker)
    {
        AppendLogAsync(L"任务正在执行，请稍候。 / A task is already running. Please wait.");
        return;
    }
    wchar_t pathBuf[MAX_PATH]{};
    GetWindowTextW(m_editPath, pathBuf, MAX_PATH);
    std::wstring root = pathBuf;
    if (root.empty())
    {
        AppendLogAsync(L"请选择更新目录。 / Please choose an update folder.");
        return;
    }
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
    {
        AppendLogAsync(L"所选目录不存在或无法访问。 / The selected folder does not exist or is not accessible.");
        return;
    }
    wchar_t keyBuf[256]{};
    GetWindowTextW(m_editKey, keyBuf, 256);
    std::wstring key = keyBuf;
    SaveCachedSettings();
    bool encrypt = (SendMessageW(m_chkEncrypt, BM_GETCHECK, 0, 0) == BST_CHECKED);
    UpdateStatusText(L"\u72b6\u6001: \u751f\u6210\u4e2d... / Status: Generating...");
    UpdateProgress(0, 0);
    SetBusy(true);
    m_hWorker = reinterpret_cast<HANDLE>(1);
    RunWorker(std::move(root), std::move(key), encrypt);
}
void UpdateForgeApp::RunWorker(std::wstring root, std::wstring key, bool encrypt)
{
    UNREFERENCED_PARAMETER(key);
    std::thread([this, root = std::move(root), encrypt]() {
        auto log = [this](const std::wstring& text) { AppendLogAsync(text); };
        std::map<std::wstring, FileTask> latest;
        std::set<std::wstring> runtimeCandidates;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec); it != fs::recursive_directory_iterator(); ++it)
        {
            if (ec)
            {
                log(L"遍历目录失败，错误码= / Failed to enumerate directory. error=" + std::to_wstring(ec.value()));
                break;
            }
            if (!it->is_regular_file())
                continue;
            auto rel = fs::relative(it->path(), root, ec);
            if (ec)
                continue;
            std::wstring relPath = rel.native();
            size_t pos = relPath.find_first_of(L"\\/");
            if (pos == std::wstring::npos)
                continue;
            std::wstring stamp = relPath.substr(0, pos);
            int64_t ver = _wtoll(stamp.c_str());
            if (ver == 0)
                continue;
            std::wstring page = relPath.substr(pos + 1);
            if (page.empty())
                continue;
            std::wstring ext = fs::path(page).extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".exe" || ext == L".dll" || ext == L".wz" || ext == L".ini" || ext == L".acm")
            {
                runtimeCandidates.insert(page);
            }
            auto found = latest.find(page);
            if (found == latest.end() || found->second.version < ver)
            {
                latest[page] = FileTask{ it->path().wstring(), page, ver };
            }
        }
        std::vector<FileTask> tasks;
        tasks.reserve(latest.size());
        for (auto& kv : latest)
        {
            tasks.push_back(kv.second);
        }
        log(L"待处理文件数 / Files to process: " + std::to_wstring(tasks.size()));
        const int totalTasks = static_cast<int>(tasks.size());
        AppendProgressAsync(0, totalTasks);
        std::vector<FileResult> results;
        results.reserve(tasks.size());
        std::set<std::wstring> runtimeList;
        unsigned int threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 4;
        threads *= 2;
        if (threads > tasks.size())
            threads = static_cast<unsigned int>((std::max<size_t>)(1, tasks.size()));
        std::atomic<size_t> index{ 0 };
        std::atomic<int> processed{ 0 };
        std::mutex resMutex;
        auto workerFn = [&]() {
            while (true)
            {
                size_t i = index.fetch_add(1);
                if (i >= tasks.size())
                    break;
                const auto& task = tasks[i];
                log(L"处理文件 / Processing: " + task.relPath);
                std::error_code fec;
                auto fsize = fs::file_size(task.fullPath, fec);
                if (!fec)
                {
                    std::string md5;
                    try
                    {
                        md5 = FileHash::file_md5(task.fullPath);
                    }
                    catch (...)
                    {
                        log(L"计算 MD5 失败 / Failed to calculate MD5: " + task.relPath);
                    }

                    if (!md5.empty())
                    {
                        std::lock_guard<std::mutex> lk(resMutex);
                        results.push_back(FileResult{ task.relPath, task.version, static_cast<int64_t>(fsize), md5 });
                        if (runtimeCandidates.count(task.relPath))
                        {
                            runtimeList.insert(task.relPath);
                        }
                    }
                }
                else
                {
                    log(L"读取文件大小失败 / Failed to read file size: " + task.relPath);
                }

                int done = processed.fetch_add(1) + 1;
                AppendProgressAsync(done, totalTasks);
            }
        };
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (unsigned int i = 0; i < threads; ++i)
        {
            pool.emplace_back(workerFn);
        }
        for (auto& t : pool) t.join();
        if (results.empty())
        {
            PostMessageW(m_hWnd, WM_APP_DONE, FALSE, reinterpret_cast<LPARAM>(new std::wstring(L"未生成数据。 / No data generated.")));
            return;
        }
        int64_t latestTime = 0;
        Json::Value rootJson;
        for (auto& r : results)
        {
            Json::Value item;
            item["md5"] = r.md5;
            item["time"] = Json::Int64(r.version);
            item["size"] = Json::Int64(r.size);
            item["page"] = WideToUtf8(r.relPath);
            rootJson["file"].append(item);
            if (r.version > latestTime)
                latestTime = r.version;
        }
        rootJson["time"] = Json::Int64(latestTime);
        for (auto& run : runtimeList)
        {
            rootJson["runtime"].append(WideToUtf8(run));
        }
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::string json = Json::writeString(builder, rootJson);
        std::wstring outPath = root + L"\\Version.dat";
        bool success = true;
        std::wstring msg;
        try
        {
            if (encrypt)
            {
                auto cipher = CompressVersionDatZstd(json);
                if (cipher.empty()) {
                    throw std::runtime_error("Failed to compress Version.dat");
                }
                std::ofstream ofs(outPath, std::ios::binary);
                ofs.write(cipher.data(), static_cast<std::streamsize>(cipher.size()));
            }
            else
            {
                std::ofstream ofs(outPath, std::ios::binary);
                ofs.write(json.data(), static_cast<std::streamsize>(json.size()));
            }
            msg = L"已生成 / Generated -> " + outPath;
            if (encrypt)
                msg.append(L" (zstd-dict)");
            else
                msg.append(L" (plain)");
        }
        catch (...)
        {
            success = false;
            msg = L"写入 Version.dat 失败 / Failed to write Version.dat: " + outPath;
        }
        PostMessageW(m_hWnd, WM_APP_DONE, success ? TRUE : FALSE, reinterpret_cast<LPARAM>(new std::wstring(msg)));
    }).detach();
}
void UpdateForgeApp::SetBusy(bool busy)
{
    m_isBusy = busy;
    EnableWindow(m_btnBrowse, !busy);
    EnableWindow(m_btnGenerate, !busy);
    EnableWindow(m_editPath, !busy);
    EnableWindow(m_chkEncrypt, !busy);
    EnableWindow(m_editUrlInput, !busy);
    EnableWindow(m_btnJsonBrowse, !busy);
    EnableWindow(m_btnEncryptUrl, !busy);
    EnableWindow(m_btnDecryptPayload, !busy);
    SyncEncryptUiState();
}

void UpdateForgeApp::Log(const std::wstring& text)
{
    int len = GetWindowTextLengthW(m_editLog);
    SendMessageW(m_editLog, EM_SETSEL, len, len);
    SendMessageW(m_editLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(m_editLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r\n"));
}

void UpdateForgeApp::AppendLogAsync(const std::wstring& text)
{
    auto* payload = new std::wstring(text);
    PostMessageW(m_hWnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(payload));
}

void UpdateForgeApp::UpdateStatusText(const std::wstring& text)
{
    if (m_statusBar)
    {
        SendMessageW(m_statusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
}

void UpdateForgeApp::UpdateProgress(int processed, int total)
{
    m_processedCount = (std::max)(0, processed);
    m_totalCount = (std::max)(0, total);

    int progressMax = (std::max)(1, m_totalCount);
    int progressPos = (std::min)(m_processedCount, progressMax);
    SendMessageW(m_statusProgress, PBM_SETRANGE32, 0, progressMax);
    SendMessageW(m_statusProgress, PBM_SETPOS, progressPos, 0);

    std::wstring progressText = L"\u5df2\u5904\u7406 " + std::to_wstring(m_processedCount)
        + L" / \u603b\u5171 " + std::to_wstring(m_totalCount);
    SetWindowTextW(m_statusProgressText, progressText.c_str());
}

void UpdateForgeApp::AppendProgressAsync(int processed, int total)
{
    auto* payload = new ProgressPayload{};
    payload->processed = processed;
    payload->total = total;
    PostMessageW(m_hWnd, WM_APP_PROGRESS, 0, reinterpret_cast<LPARAM>(payload));
}

void UpdateForgeApp::OnWorkerFinished(bool success, const std::wstring& message)
{
    SetBusy(false);
    m_hWorker = nullptr;
    UpdateStatusText(L"\u72b6\u6001: \u5c31\u7eea / Status: Ready");
    AppendLogAsync(message);
    if (success)
    {
        AppendLogAsync(L"完成。 / Done.");
    }
}
LRESULT CALLBACK UpdateForgeApp::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    UpdateForgeApp* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<UpdateForgeApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<UpdateForgeApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
    {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg)
    {
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 760;
        mmi->ptMinTrackSize.y = 540;
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            self->LayoutControls(LOWORD(lParam), HIWORD(lParam));
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_BROWSE:
            self->OnBrowse();
            break;
        case IDC_CHK_ENCRYPT:
            self->SyncEncryptUiState();
            self->SaveCachedSettings();
            break;
        case IDC_BTN_GENERATE:
            self->OnGenerate();
            break;
        case IDC_BTN_JSON_BROWSE:
            self->OnBrowseJsonFile();
            break;
        case IDC_BTN_JSON_ENCRYPT:
            self->OnEncryptJsonPayload();
            break;
        case IDC_BTN_JSON_DECRYPT:
            self->OnDecryptJsonPayload();
            break;
        default:
            break;
        }
        break;
    case WM_APP_LOG:
    {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        if (text)
        {
            self->Log(*text);
            delete text;
        }
        break;
    }
    case WM_APP_PROGRESS:
    {
        auto* payload = reinterpret_cast<ProgressPayload*>(lParam);
        if (payload)
        {
            self->UpdateProgress(payload->processed, payload->total);
            delete payload;
        }
        break;
    }
    case WM_APP_DONE:
    {
        auto* text = reinterpret_cast<std::wstring*>(lParam);
        bool ok = wParam != 0;
        if (text)
        {
            self->OnWorkerFinished(ok, *text);
            delete text;
        }
        break;
    }
    case WM_DESTROY:
        self->SaveCachedSettings();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return 0;
}

