#include <libloaderapi.h>
#include <windows.h>
#include <detours/detours.h>

// int __thiscall NameSpace.sub_5081A080(
//         _DWORD *this,
//         const CHAR *lpFileName,
//         DWORD dwCreationDisposition,
//         DWORD dwFlagsAndAttributes,
//         DWORD dwShareMode,
//         DWORD dwDesiredAccess,
//         struct _SECURITY_ATTRIBUTES *lpSecurityAttributes,
//         HANDLE hTemplateFile)
// {
//
//
// }

constexpr uintptr_t kTargetAddr = 0x5081A080;

#if defined(_M_IX86)
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
#else
using NameSpaceSub5081A080 = int(*)(
    void* this_ptr,
    const CHAR* lpFileName,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    DWORD dwShareMode,
    DWORD dwDesiredAccess,
    _SECURITY_ATTRIBUTES* lpSecurityAttributes,
    HANDLE hTemplateFile);
#endif

static NameSpaceSub5081A080 g_sub_5081A080 =
    reinterpret_cast<NameSpaceSub5081A080>(kTargetAddr);

#if defined(_M_IX86)
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
#else
int Hook_sub_5081A080(
    void* this_ptr,
    const CHAR* lpFileName,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    DWORD dwShareMode,
    DWORD dwDesiredAccess,
    _SECURITY_ATTRIBUTES* lpSecurityAttributes,
    HANDLE hTemplateFile)
{
    return g_sub_5081A080(this_ptr,
                          lpFileName,
                          dwCreationDisposition,
                          dwFlagsAndAttributes,
                          dwShareMode,
                          dwDesiredAccess,
                          lpSecurityAttributes,
                          hTemplateFile);
}
#endif

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
        LoadLibraryA("NameSpace.dll");
        SetHook(true,
                reinterpret_cast<void**>(&g_sub_5081A080),
                reinterpret_cast<void*>(&Hook_sub_5081A080));
    }break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        SetHook(false,
                reinterpret_cast<void**>(&g_sub_5081A080),
                reinterpret_cast<void*>(&Hook_sub_5081A080));
        break;
    }
    return TRUE;
}
