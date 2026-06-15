// Injector — classic LoadLibrary + CreateRemoteThread DLL injector.
//
// Usage:  Injector.exe [process.exe] [path\to\mod.dll]
// Default: OPWS.exe  and  .\GlyphSwap.dll
//
// Waits up to ~2 minutes for the target process, so you can start it before
// OR after launching the game.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

static DWORD FindPid(const wchar_t* exeName)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring proc = (argc > 1) ? argv[1] : L"OPWS.exe";
    std::wstring dll  = (argc > 2) ? argv[2] : L"GlyphSwap.dll";

    // Resolve the DLL to an absolute path. LoadLibrary in the target's context
    // would otherwise resolve a relative path against the GAME's working dir.
    wchar_t full[MAX_PATH]{};
    if (GetFullPathNameW(dll.c_str(), MAX_PATH, full, nullptr)) dll = full;

    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::wcout << L"[!] DLL not found: " << dll << L"\n";
        return 1;
    }

    std::wcout << L"[*] Waiting for " << proc << L" ...\n";
    DWORD pid = 0;
    for (int i = 0; i < 240 && !(pid = FindPid(proc.c_str())); ++i)
        Sleep(500);                                   // up to ~120 s
    if (!pid) { std::wcout << L"[!] Process not found.\n"; return 1; }

    std::wcout << L"[*] Found PID " << pid << L". Injecting:\n    " << dll << L"\n";

    HANDLE h = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION  | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!h) { std::wcout << L"[!] OpenProcess failed: " << GetLastError() << L"\n"; return 1; }

    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { std::wcout << L"[!] VirtualAllocEx failed.\n"; CloseHandle(h); return 1; }

    if (!WriteProcessMemory(h, remote, dll.c_str(), bytes, nullptr))
    {
        std::wcout << L"[!] WriteProcessMemory failed.\n";
        VirtualFreeEx(h, remote, 0, MEM_RELEASE); CloseHandle(h); return 1;
    }

    // kernel32.dll loads at the same address in every process on a given boot,
    // so the local address of LoadLibraryW is valid in the target too.
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLib)
    {
        std::wcout << L"[!] Could not resolve LoadLibraryW.\n";
        VirtualFreeEx(h, remote, 0, MEM_RELEASE); CloseHandle(h); return 1;
    }

    HANDLE th = CreateRemoteThread(h, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th)
    {
        std::wcout << L"[!] CreateRemoteThread failed: " << GetLastError() << L"\n";
        VirtualFreeEx(h, remote, 0, MEM_RELEASE); CloseHandle(h); return 1;
    }

    WaitForSingleObject(th, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeThread(th, &exitCode);                 // low 32 bits of the HMODULE
    std::wcout << (exitCode ? L"[+] Injected successfully.\n"
                            : L"[!] LoadLibrary returned 0 - injection likely failed.\n");

    VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(h);
    return 0;
}
