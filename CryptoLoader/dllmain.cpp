#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <fileapi.h>
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <libloaderapi.h>
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <process.h>
#include <detours/detours.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

constexpr uintptr_t kTargetAddr = 0x5081A080;
constexpr int kLauncherPort = 12345;
constexpr const char* kDownloadPath = "/download";
constexpr const char* kVersionMapMappingName = "MapleFireReborn.VersionFileMd5Map";
// 5 minutes
constexpr DWORD kVersionMapRefreshIntervalMs =5 * 60 * 1000;
// Prevent repeated network requests for the same file in a hot loop.
constexpr DWORD kDownloadRequestCooldownMs = 3000;
// Current directory very rarely changes at runtime; cache it for hot hook path.
constexpr DWORD kCurrentDirRefreshIntervalMs = 2000;

#if !defined(_M_IX86)
#error CryptoLoader must be built as Win32 (x86).
#endif

using NameSpaceSub5081A080 = int(__fastcall*)(
    void* this_ptr,
    void* edx,
    const CHAR* lpFileName,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    DWORD dwShareMode,
    DWORD dwDesiredAccess,
    _SECURITY_ATTRIBUTES* lpSecurityAttributes,
    HANDLE hTemplateFile);

static NameSpaceSub5081A080 g_sub_5081A080 =
    reinterpret_cast<NameSpaceSub5081A080>(kTargetAddr);

// Full path (normalized lowercase Windows path) -> expected file MD5 from launcher.
static std::map<std::string, std::string> g_mapFiles;
static std::mutex g_mapFilesMutex;
static DWORD g_lastMapRefreshTick = 0;
static std::unordered_map<std::string, DWORD> g_downloadRequestTicks;
static std::mutex g_downloadRequestTicksMutex;

struct FileMetadataCacheEntry {
    bool exists = false;
    ULONGLONG fileSize = 0;
    FILETIME lastWriteTime{};
    std::string expectedMd5;
    bool md5Match = false;
};

static std::unordered_map<std::string, FileMetadataCacheEntry> g_fileCheckCache;
static std::mutex g_fileCheckCacheMutex;
static std::string g_currentDirectoryKey;
static std::string g_currentDirectoryPrefix;
static std::mutex g_currentDirectoryMutex;
static DWORD g_lastCurrentDirRefreshTick = 0;

namespace {

std::string ToUpperAscii(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string ToLowerAscii(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string NormalizePathKey(std::filesystem::path path)
{
    path = path.lexically_normal();
    std::string key = path.generic_string();
    std::replace(key.begin(), key.end(), '/', '\\');
    return ToLowerAscii(key);
}

std::string BuildHex(const BYTE* data, DWORD size)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.resize(static_cast<size_t>(size) * 2);
    for (DWORD i = 0; i < size; ++i) {
        out[static_cast<size_t>(i) * 2] = kHex[data[i] >> 4];
        out[static_cast<size_t>(i) * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

std::string ComputeFileMd5(const std::filesystem::path& filePath)
{
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) {
        return {};
    }

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return {};
    }
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return {};
    }

    char buffer[4096];
    while (ifs.good()) {
        ifs.read(buffer, sizeof(buffer));
        const std::streamsize readSize = ifs.gcount();
        if (readSize > 0) {
            if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(buffer), static_cast<DWORD>(readSize), 0)) {
                CryptDestroyHash(hHash);
                CryptReleaseContext(hProv, 0);
                return {};
            }
        }
    }

    DWORD hashSize = 0;
    DWORD sizeBytes = sizeof(hashSize);
    if (!CryptGetHashParam(hHash, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hashSize), &sizeBytes, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return {};
    }

    BYTE hash[16] = {};
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashSize, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return {};
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return BuildHex(hash, hashSize);
}

std::string BuildDownloadPage(const std::filesystem::path& inputPath, const std::filesystem::path& absolutePath)
{
    std::error_code ec;
    const std::filesystem::path currentDir = std::filesystem::current_path(ec);
    if (!ec) {
        std::filesystem::path rel = std::filesystem::relative(absolutePath, currentDir, ec);
        if (!ec && !rel.empty()) {
            const std::string relText = rel.generic_string();
            if (relText.rfind("..", 0) != 0) {
                return relText;
            }
        }
    }
    return inputPath.generic_string();
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide;
    wide.resize(static_cast<size_t>(size));
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), size) <= 0) {
        return {};
    }
    return wide;
}

void RefreshCurrentDirectoryCacheIfNeeded()
{
    const DWORD now = GetTickCount();
    {
        std::lock_guard<std::mutex> lock(g_currentDirectoryMutex);
        if (!g_currentDirectoryKey.empty() &&
            static_cast<DWORD>(now - g_lastCurrentDirRefreshTick) < kCurrentDirRefreshIntervalMs) {
            return;
        }
        g_lastCurrentDirRefreshTick = now;
    }

    std::error_code ec;
    const std::filesystem::path currentDir = std::filesystem::current_path(ec);
    std::string key;
    std::string prefix;
    if (!ec) {
        key = NormalizePathKey(currentDir.lexically_normal());
        if (!key.empty() && key.back() == '\\') {
            key.pop_back();
        }
        if (!key.empty()) {
            prefix = key + "\\";
        }
    }

    std::lock_guard<std::mutex> lock(g_currentDirectoryMutex);
    g_currentDirectoryKey = std::move(key);
    g_currentDirectoryPrefix = std::move(prefix);
}

bool IsPathInCurrentDirectoryKey(const std::string& pathKey)
{
    if (pathKey.empty()) {
        return false;
    }

    RefreshCurrentDirectoryCacheIfNeeded();

    std::lock_guard<std::mutex> lock(g_currentDirectoryMutex);
    if (g_currentDirectoryKey.empty()) {
        return false;
    }
    return pathKey == g_currentDirectoryKey || pathKey.rfind(g_currentDirectoryPrefix, 0) == 0;
}

struct FileMetadataSnapshot {
    bool exists = false;
    ULONGLONG fileSize = 0;
    FILETIME lastWriteTime{};
};

bool FileTimeEquals(const FILETIME& lhs, const FILETIME& rhs)
{
    return lhs.dwLowDateTime == rhs.dwLowDateTime &&
        lhs.dwHighDateTime == rhs.dwHighDateTime;
}

bool ReadFileMetadataFast(const std::filesystem::path& absolutePath, FileMetadataSnapshot& metadata)
{
    WIN32_FILE_ATTRIBUTE_DATA attrs{};
    const std::wstring filePath = absolutePath.wstring();
    if (GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &attrs)) {
        metadata.exists = true;
        metadata.fileSize = (static_cast<ULONGLONG>(attrs.nFileSizeHigh) << 32) | attrs.nFileSizeLow;
        metadata.lastWriteTime = attrs.ftLastWriteTime;
        return true;
    }

    const DWORD lastError = GetLastError();
    if (lastError == ERROR_FILE_NOT_FOUND ||
        lastError == ERROR_PATH_NOT_FOUND ||
        lastError == ERROR_INVALID_NAME) {
        metadata.exists = false;
        metadata.fileSize = 0;
        metadata.lastWriteTime = {};
        return true;
    }
    return false;
}

bool TryGetCachedMd5Result(
    const std::string& key,
    const std::string& expectedMd5,
    const FileMetadataSnapshot& metadata,
    bool& needDownload)
{
    std::lock_guard<std::mutex> lock(g_fileCheckCacheMutex);
    const auto it = g_fileCheckCache.find(key);
    if (it == g_fileCheckCache.end()) {
        return false;
    }

    const FileMetadataCacheEntry& entry = it->second;
    if (entry.expectedMd5 != expectedMd5 ||
        entry.exists != metadata.exists ||
        entry.fileSize != metadata.fileSize ||
        !FileTimeEquals(entry.lastWriteTime, metadata.lastWriteTime)) {
        return false;
    }

    needDownload = !entry.md5Match;
    return true;
}

void UpdateCachedMd5Result(
    const std::string& key,
    const std::string& expectedMd5,
    const FileMetadataSnapshot& metadata,
    bool md5Match)
{
    FileMetadataCacheEntry entry;
    entry.exists = metadata.exists;
    entry.fileSize = metadata.fileSize;
    entry.lastWriteTime = metadata.lastWriteTime;
    entry.expectedMd5 = expectedMd5;
    entry.md5Match = md5Match;

    std::lock_guard<std::mutex> lock(g_fileCheckCacheMutex);
    g_fileCheckCache[key] = std::move(entry);
}

bool ShouldRequestDownloadNow(const std::string& key)
{
    const DWORD now = GetTickCount();
    std::lock_guard<std::mutex> lock(g_downloadRequestTicksMutex);
    DWORD& lastTick = g_downloadRequestTicks[key];
    if (lastTick != 0 && static_cast<DWORD>(now - lastTick) < kDownloadRequestCooldownMs) {
        return false;
    }
    lastTick = now;
    return true;
}

bool RequestDownloadFromLauncher(const std::string& page)
{
    if (page.empty()) {
        return false;
    }

    auto UrlEncode = [](const std::string& value) {
        std::ostringstream oss;
        oss << std::hex << std::uppercase;
        for (unsigned char ch : value) {
            if ((ch >= 'A' && ch <= 'Z') ||
                (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/') {
                oss << static_cast<char>(ch);
            }
            else {
                oss << '%' << static_cast<int>(ch >> 4) << static_cast<int>(ch & 0x0F);
            }
        }
        return oss.str();
    };

    const std::string encodedPage = UrlEncode(page);
    const std::string requestPath = std::string(kDownloadPath) + "?page=" + encodedPage
        + "&pid=" + std::to_string(_getpid());
    const std::wstring requestPathW = Utf8ToWide(requestPath);
    if (requestPathW.empty()) {
        return false;
    }

    HINTERNET hSession = WinHttpOpen(
        L"CryptoLoader/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"localhost", kLauncherPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        requestPathW.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX)) {
            ok = (statusCode == 200);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

unsigned __stdcall DownloadRequestThreadProc(void* context)
{
    std::unique_ptr<std::string> page(static_cast<std::string*>(context));
    if (page && !page->empty()) {
        RequestDownloadFromLauncher(*page);
    }
    return 0;
}

void RequestDownloadFromLauncherAsync(std::string page)
{
    if (page.empty()) {
        return;
    }

    std::unique_ptr<std::string> pagePtr = std::make_unique<std::string>(std::move(page));
    const uintptr_t threadHandle = _beginthreadex(nullptr, 0, &DownloadRequestThreadProc, pagePtr.get(), 0, nullptr);
    if (threadHandle == 0) {
        RequestDownloadFromLauncher(*pagePtr);
        return;
    }

    pagePtr.release();
    CloseHandle(reinterpret_cast<HANDLE>(threadHandle));
}

bool ParseVersionMapPayload(const std::string& payload, std::map<std::string, std::string>& outMap)
{
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const size_t tabPos = line.find('\t');
        if (tabPos == std::string::npos || tabPos == 0 || tabPos + 1 >= line.size()) {
            continue;
        }

        const std::string normalizedKey = NormalizePathKey(std::filesystem::u8path(line.substr(0, tabPos)));
        const std::string md5 = ToUpperAscii(line.substr(tabPos + 1));
        if (!normalizedKey.empty() && !md5.empty()) {
            outMap[normalizedKey] = md5;
        }
    }
    return !outMap.empty();
}

bool RefreshVersionMapFromSharedMemory()
{
    HANDLE hMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, kVersionMapMappingName);
    if (!hMapping) {
        return false;
    }

    void* view = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(hMapping);
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    std::string payload;
    if (VirtualQuery(view, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.RegionSize > 0) {
        const char* text = static_cast<const char*>(view);
        size_t len = 0;
        while (len < mbi.RegionSize && text[len] != '\0') {
            ++len;
        }
        payload.assign(text, len);
    }

    UnmapViewOfFile(view);
    CloseHandle(hMapping);

    if (payload.empty()) {
        return false;
    }

    std::map<std::string, std::string> parsedMap;
    if (!ParseVersionMapPayload(payload, parsedMap)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mapFilesMutex);
    g_mapFiles.swap(parsedMap);
    return true;
}

void RefreshVersionMapCacheIfNeeded()
{
    const DWORD now = GetTickCount();
    bool shouldRefresh = false;
    {
        std::lock_guard<std::mutex> lock(g_mapFilesMutex);
        if (g_mapFiles.empty() || now - g_lastMapRefreshTick >= kVersionMapRefreshIntervalMs) {
            g_lastMapRefreshTick = now;
            shouldRefresh = true;
        }
    }

    if (shouldRefresh) {
        RefreshVersionMapFromSharedMemory();
    }
}

std::string FindExpectedMd5ByKey(const std::string& key)
{
    if (key.empty()) {
        return {};
    }

    std::lock_guard<std::mutex> lock(g_mapFilesMutex);
    auto it = g_mapFiles.find(key);
    if (it == g_mapFiles.end()) {
        return {};
    }
    return it->second;
}

void HandleHookedFileCheck(const CHAR* lpFileName)
{
    if (lpFileName != nullptr && lpFileName[0] != '\0') {
        try {
            const std::filesystem::path inputPath = std::filesystem::u8path(lpFileName);
            std::filesystem::path absolutePath = inputPath;
            if (!absolutePath.is_absolute()) {
                absolutePath = std::filesystem::current_path() / absolutePath;
            }
            absolutePath = absolutePath.lexically_normal();

            const std::string normalizedKey = NormalizePathKey(absolutePath);
            if (IsPathInCurrentDirectoryKey(normalizedKey)) {

                RefreshVersionMapCacheIfNeeded();
                const std::string expectedMd5 = FindExpectedMd5ByKey(normalizedKey);

                FileMetadataSnapshot metadata;
                bool metadataOk = ReadFileMetadataFast(absolutePath, metadata);
                if (!metadataOk) {
                    std::error_code ec;
                    metadata.exists = std::filesystem::exists(absolutePath, ec) && !ec;
                    if (!metadata.exists) {
                        metadata.fileSize = 0;
                        metadata.lastWriteTime = {};
                    }
                }

                bool needDownload = !metadata.exists;
                if (metadata.exists && !expectedMd5.empty()) {
                    bool cacheHit = false;
                    if (metadataOk) {
                        cacheHit = TryGetCachedMd5Result(normalizedKey, expectedMd5, metadata, needDownload);
                    }

                    if (!cacheHit) {
                        const std::string localMd5 = ToUpperAscii(ComputeFileMd5(absolutePath));
                        if (!localMd5.empty()) {
                            needDownload = localMd5 != expectedMd5;
                            if (metadataOk) {
                                UpdateCachedMd5Result(normalizedKey, expectedMd5, metadata, !needDownload);
                            }
                        }
                    }
                }

                if (needDownload) {
                    const std::string page = BuildDownloadPage(inputPath, absolutePath);
                    if (ShouldRequestDownloadNow(normalizedKey)) {
                        RequestDownloadFromLauncherAsync(page);
                    }
                }
            }
        }
        catch (...) {
            // Swallow all exceptions in hook path to avoid breaking game file I/O.
        }
    }
}

} // namespace


// HOOK 一下GetFileAttributesA
using GETFILEATTRIBUTESA_FN = DWORD(WINAPI*) (LPCSTR lpFileName);
static GETFILEATTRIBUTESA_FN g_originalGetFileAttributesA = GetFileAttributesA;

DWORD WINAPI HookedGetFileAttributesA(LPCSTR lpFileName)
{
    // 如果后缀名是.img，就处理一下
    if (lpFileName != nullptr && lpFileName[0] != '\0') {
        std::string fileNameStr(lpFileName);
        const size_t dotPos = fileNameStr.rfind('.');
        if (dotPos != std::string::npos) {
            std::string ext = fileNameStr.substr(dotPos);
            for (char& ch : ext) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (ext == ".img") {
                HandleHookedFileCheck(lpFileName);
            }
        }
    }

    return g_originalGetFileAttributesA(lpFileName);
}

int __fastcall Hook_sub_5081A080(
    void* this_ptr,
    void* edx,
    const CHAR* lpFileName,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    DWORD dwShareMode,
    DWORD dwDesiredAccess,
    _SECURITY_ATTRIBUTES* lpSecurityAttributes,
    HANDLE hTemplateFile)
{
    HandleHookedFileCheck(lpFileName);
    return g_sub_5081A080(this_ptr,
        edx,
        lpFileName,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        dwShareMode,
        dwDesiredAccess,
        lpSecurityAttributes,
        hTemplateFile);
}

bool SetHook(bool attach, void** ptrTarget, void* ptrDetour)
{
    if (DetourTransactionBegin() != NO_ERROR)
    {
        return false;
    }

    HANDLE pCurThread = GetCurrentThread();

    if (DetourUpdateThread(pCurThread) == NO_ERROR)
    {
        auto pDetourFunc = attach ? DetourAttach : DetourDetach;

        if (pDetourFunc(ptrTarget, ptrDetour) == NO_ERROR)
        {
            if (DetourTransactionCommit() == NO_ERROR)
            {
                return true;
            }
        }
    }

    DetourTransactionAbort();
    return false;
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD  ul_reason_for_call,
                      LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        CloseHandle(CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            do {
                /*HMODULE hNameSpaceMoudel = GetModuleHandleA("NameSpace.dll");
                if (hNameSpaceMoudel != nullptr)
                {
                    SetHook(true,
                        reinterpret_cast<void**>(&g_sub_5081A080),
                        reinterpret_cast<void*>(&Hook_sub_5081A080));
					break;
                }*/
                if(SetHook(true,
                        reinterpret_cast<void**>(&g_originalGetFileAttributesA),
                        reinterpret_cast<void*>(&HookedGetFileAttributesA)))
                {
                    break;
                }
				Sleep(0);
            } while (true);
			return 0;
		}, nullptr, 0, nullptr));

    }break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        SetHook(false,
                reinterpret_cast<void**>(&g_originalGetFileAttributesA),
                reinterpret_cast<void*>(&HookedGetFileAttributesA));
        break;
    }
    return TRUE;
}


// ����һ������
extern "C" __declspec(dllexport) void CryptoLoader()
{
    // ʲô������
}
