#include "framework.h"
#include "UpdateForge.h"
#include "FileHash.h"

#include <json/json.h>
#include <cryptopp/sha.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>

#include <shobjidl.h>
#include <commctrl.h>

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
#include <cwctype>
#include <algorithm>

using namespace std;
namespace fs = std::filesystem;

constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_DONE = WM_APP + 2;
constexpr UINT WM_APP_PROGRESS = WM_APP + 3;

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

static std::string EncryptData(const std::string& plain, const std::wstring& keyText)
{
    using namespace CryptoPP;

    std::string keyBytes = NarrowACP(keyText);
    if (keyBytes.empty()) {
        keyBytes = "update-forge-default-key";
    }
    SHA256 sha;
    std::string digest(32, 0);
    sha.CalculateDigest(reinterpret_cast<CryptoPP::byte*>(digest.data()), reinterpret_cast<const CryptoPP::byte*>(keyBytes.data()), keyBytes.size());

    CryptoPP::byte iv[AES::BLOCKSIZE]{};
    memcpy(iv, digest.data(), AES::BLOCKSIZE);

    std::string cipher;
    CBC_Mode<AES>::Encryption enc(reinterpret_cast<const CryptoPP::byte*>(digest.data()), digest.size(), iv);
    StringSource ss(plain, true, new StreamTransformationFilter(enc, new StringSink(cipher)));
    return cipher;
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
    wc.lpfnWndProc = UpdateForgeApp::WndProc;
    wc.hInstance = m_hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    m_hWnd = CreateWindowExW(0, CLASS_NAME, L"更新构建器 / UpdateForge Version Builder", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 540, nullptr, nullptr, m_hInst, this);

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
        x + 525, y - 2, 90, 24, m_hWnd, reinterpret_cast<HMENU>(1), m_hInst, nullptr);
    y += 36;
    m_lblKey = CreateWindowExW(0, L"STATIC", L"密钥/Key:", WS_CHILD | WS_VISIBLE, x, y, 90, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_editKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x + 95, y - 2, 220, 24, m_hWnd, nullptr, m_hInst, nullptr);
    m_chkEncrypt = CreateWindowExW(0, L"BUTTON", L"启用加密/Encrypt", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        x + 330, y - 2, 140, 24, m_hWnd, reinterpret_cast<HMENU>(2), m_hInst, nullptr);
    m_btnGenerate = CreateWindowExW(0, L"BUTTON", L"生成Version.dat/Generate", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x + 480, y - 2, 160, 24, m_hWnd, reinterpret_cast<HMENU>(3), m_hInst, nullptr);
    y += 34;
    m_lblLog = CreateWindowExW(0, L"STATIC", L"日志/Log:", WS_CHILD | WS_VISIBLE, x, y, 80, 20, m_hWnd, nullptr, m_hInst, nullptr);
    m_editLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        x, y + 20, 700, 360, m_hWnd, nullptr, m_hInst, nullptr);
    HWND controls[] = {
        m_lblPath, m_editPath, m_btnBrowse,
        m_lblKey, m_editKey, m_chkEncrypt, m_btnGenerate,
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

    // Default to "<cwd>\\Update" to reduce manual input for common usage.
    wchar_t cwd[MAX_PATH]{};
    if (GetCurrentDirectoryW(MAX_PATH, cwd))
    {
        std::wstring guess = cwd;
        guess.append(L"\\Update");
        SetWindowTextW(m_editPath, guess.c_str());
    }

    RECT rc{};
    GetClientRect(m_hWnd, &rc);
    LayoutControls(rc.right - rc.left, rc.bottom - rc.top);
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
    }
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
    bool encrypt = (SendMessageW(m_chkEncrypt, BM_GETCHECK, 0, 0) == BST_CHECKED);
    UpdateStatusText(L"\u72b6\u6001: \u751f\u6210\u4e2d... / Status: Generating...");
    UpdateProgress(0, 0);
    SetBusy(true);
    m_hWorker = reinterpret_cast<HANDLE>(1);
    RunWorker(std::move(root), std::move(key), encrypt);
}
void UpdateForgeApp::RunWorker(std::wstring root, std::wstring key, bool encrypt)
{
    std::thread([this, root = std::move(root), key = std::move(key), encrypt]() {
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
            item["page"] = NarrowACP(r.relPath);
            rootJson["file"].append(item);
            if (r.version > latestTime)
                latestTime = r.version;
        }
        rootJson["time"] = Json::Int64(latestTime);
        for (auto& run : runtimeList)
        {
            rootJson["runtime"].append(NarrowACP(run));
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
                auto cipher = EncryptData(json, key);
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
                msg.append(L"（已加密 / encrypted）");
            else
                msg.append(L"（明文 / plain）");
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
    EnableWindow(m_btnBrowse, !busy);
    EnableWindow(m_btnGenerate, !busy);
    EnableWindow(m_editPath, !busy);
    EnableWindow(m_editKey, !busy);
    EnableWindow(m_chkEncrypt, !busy);
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
        case 1:
            self->OnBrowse();
            break;
        case 2:
            // checkbox handled automatically
            break;
        case 3:
            self->OnGenerate();
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
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return 0;
}

