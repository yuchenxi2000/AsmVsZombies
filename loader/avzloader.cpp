#include <Windows.h>
#include <stdint.h>
#include <cwchar>
#include <d3d.h>
#include <avz_asm.h>
#include <iostream>

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath(path);
    size_t pos = exePath.find_last_of(L"\\/");
    return exePath.substr(0, pos);
}

std::wstring pvzDir;
std::wstring modDir;

struct MonitorParam {
    std::wstring targetDir;
    HANDLE hStopEvent;
    HANDLE hChangeEvent;
};

// mods目录监控线程，实现热加载
DWORD WINAPI MonitorThreadProc(LPVOID lpParam) {
    auto * params = static_cast<MonitorParam *>(lpParam);
    HANDLE hStopEvent = params->hStopEvent;
    HANDLE hChangeEvent = params->hChangeEvent;

    HANDLE hDir = CreateFileW(params->targetDir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (hDir == INVALID_HANDLE_VALUE) {
        MessageBoxW(NULL, L"Failed to monitor mods folder!", L"OK", 0);
        return 1;
    }
    size_t buffer_size = 65536;
    char * buffer = new char[buffer_size];
    DWORD bytesReturned = 0;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent) {
        std::cerr << "Failed to create overlapped event" << std::endl;
        CloseHandle(hDir);
        delete [] buffer;
        return 1;
    }

    HANDLE waitHandles[2] = {hStopEvent, overlapped.hEvent};

    while (true) {
        BOOL bSuccess = ReadDirectoryChangesW(hDir, buffer, buffer_size, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION, &bytesReturned, &overlapped, NULL);
        DWORD lastError = GetLastError();
        if (!bSuccess && lastError != ERROR_IO_PENDING) {
            std::cerr << "Failed to call ReadDirectoryChangesW with error code: " << lastError << std::endl;
            break;
        }
        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            // Stop thread
            break;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            // File changed
            GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE);
            SetEvent(hChangeEvent);
            ResetEvent(overlapped.hEvent);
        } else {
            // error
            break;
        }
    }
    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    delete [] buffer;
    return 0;
}

typedef void (__cdecl *FuncType)();
typedef void (__cdecl *FuncEntryType)(HMODULE);
typedef int (__cdecl *FuncLoopType)();

#define CHECK_FUNC(FuncName) \
if (func##FuncName == 0) { \
    hasUnloadedFunc = true; \
    message += L" "#FuncName; \
}

class Mod {
public:
    std::wstring modName;
    HMODULE hMod;
    FILETIME lastWriteTime;
    FuncEntryType funcEntry;
    FuncType funcAtExit;
    FuncLoopType funcBeforeGameLoop;
    FuncLoopType funcAfterGameLoop;
    FuncType funcBeforeDrawEveryTick;
    FuncType funcDrawEveryTick;
    FuncType funcAfterDrawEveryTick;
    Mod(const std::wstring & modName, HMODULE hMod, FILETIME lastWriteTime) : modName(modName), hMod(hMod), lastWriteTime(lastWriteTime) {
        funcEntry = (FuncEntryType)GetProcAddress(hMod, "Entry");
        funcAtExit = (FuncType)GetProcAddress(hMod, "AtExit");
        funcBeforeGameLoop = (FuncLoopType)GetProcAddress(hMod, "BeforeGameLoop");
        funcAfterGameLoop = (FuncLoopType)GetProcAddress(hMod, "AfterGameLoop");
        funcBeforeDrawEveryTick = (FuncType)GetProcAddress(hMod, "BeforeDrawEveryTick");
        funcDrawEveryTick = (FuncType)GetProcAddress(hMod, "DrawEveryTick");
        funcAfterDrawEveryTick = (FuncType)GetProcAddress(hMod, "AfterDrawEveryTick");
    }
    bool CheckFunc() {
        std::wstring message = L"Unloaded func in mod ";
        message += modName;
        message += L":";
        bool hasUnloadedFunc = false;
        CHECK_FUNC(Entry)
        CHECK_FUNC(AtExit)
        CHECK_FUNC(BeforeGameLoop)
        CHECK_FUNC(AfterGameLoop)
        CHECK_FUNC(BeforeDrawEveryTick)
        CHECK_FUNC(DrawEveryTick)
        CHECK_FUNC(AfterDrawEveryTick)
        if (hasUnloadedFunc) {
            MessageBoxW(NULL, message.c_str(), L"OK", 0);
        }
        return !hasUnloadedFunc;
    }
};

std::vector<Mod> mod_list;

void InstallDrawHook();
void UninstallDrawHook();
bool IsOpen3dAcceleration();

void LoadAllMods() {
    // load all mods
    pvzDir = GetExeDir();
    modDir = pvzDir + L"\\mods";
    std::wstring searchPath = modDir + L"\\*.dll";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        std::vector<Mod> new_mod_list;
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring fullPath = modDir + L"\\" + findData.cFileName;
            FILETIME lastWriteTime = findData.ftLastWriteTime;
            std::wstring modName(findData.cFileName);
            bool already_loaded = false;
            // is the mod loaded?
            for (auto & mod : mod_list) {
                if (mod.modName == modName) {
                    // is the mod modified?
                    if (CompareFileTime(&mod.lastWriteTime, &lastWriteTime) != 0) {
                        // modified, unload this first
                        // 貌似Windows不允许更新被打开的文件，因此卸载脚本只有两种方式：
                        // 1. 重命名（必须改后缀，比如a.dll改成a.dll.disabled，不然会被重新加载回来）
                        // 2. 移出mods目录
                        // 这块代码好像永远不会被执行，但以防万一就留着了
                        mod.funcAtExit();
                        FreeLibrary(mod.hMod);
                        // MessageBoxW(NULL, (std::wstring(L"Unload mod 1 ") + mod.modName).c_str(), L"OK", 0);
                    } else {
                        new_mod_list.push_back(mod);
                        already_loaded = true;
                    }
                    break;
                }
            }
            if (!already_loaded) {
                // load this mod
                HMODULE hMod = LoadLibraryW(fullPath.c_str());
                if (hMod) {
                    Mod mod(modName, hMod, lastWriteTime);
                    if (mod.CheckFunc()) {
                        mod.funcEntry(hMod);
                        new_mod_list.push_back(mod);
                        // MessageBoxW(NULL, (std::wstring(L"Load mod ") + findData.cFileName).c_str(), L"OK", 0);
                    }
                } else {
                    MessageBoxW(NULL, (std::wstring(L"Failed to load ") + findData.cFileName + L" with error " + std::to_wstring(GetLastError())).c_str(), L"OK", 0);
                }
            }
        } while (FindNextFileW(hFind, &findData));
        // unload old mods
        for (auto & mod : mod_list) {
            bool found = false;
            for (auto & new_mod : new_mod_list) {
                if (new_mod.modName == mod.modName) {
                    found = true;
                }
            }
            if (!found) {
                mod.funcAtExit();
                FreeLibrary(mod.hMod);
                // MessageBoxW(NULL, (std::wstring(L"Unload mod 2 ") + mod.modName).c_str(), L"OK", 0);
            }
        }
        mod_list = new_mod_list;
    }
}

HANDLE hStopEvent = 0;
HANDLE hChangeEvent = 0;
HANDLE hThread = 0;

bool hasDrawHook = false;
bool mod_loaded = false;
extern "C" __declspec(dllexport) void __cdecl __AScriptHook() {
    if (!mod_loaded) {
        // load mods
        LoadAllMods();
        mod_loaded = true;
        // start monitor thread
        hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        hChangeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        MonitorParam params{modDir, hStopEvent, hChangeEvent};
        hThread = CreateThread(NULL, 0, MonitorThreadProc, &params, 0, NULL);
        if (!hThread) {
            MessageBoxW(NULL, L"Failed to create monitor thread!", L"OK", 0);
        }
    } else {
        static int countDown = 0;  // 加个冷却时间，以防文件更新太频繁，导致频繁重加载脚本
        const int countDownMax = 50;
        // manage mods
        DWORD waitResult = WaitForSingleObject(hChangeEvent, 0);
        if (waitResult == WAIT_OBJECT_0) {
            if (countDown <= 0) {
                // reload mods
                LoadAllMods();
                countDown = countDownMax;
                ResetEvent(hChangeEvent);  // 必须reset，不然只能热加载一次
            }
        }
        if (countDown > 0) {
            countDown--;
        }
    }

    // install draw hook
    if (IsOpen3dAcceleration()) {
        if (!hasDrawHook) {
            InstallDrawHook();
            hasDrawHook = true;
        }
    } else {
        if (hasDrawHook) {
            UninstallDrawHook();
            hasDrawHook = false;
        }
    }
    bool should_ret = false;
    for (auto & mod : mod_list) {
        if (mod.funcBeforeGameLoop()) {
            should_ret = true;
        }
    }
    if (should_ret) {
        return;
    }
    AAsm::GameTotalLoop();
    for (auto & mod : mod_list) {
        mod.funcAfterGameLoop();
    }
}

void __AInstallHook() {
    DWORD temp;
    // 54BACD 地址 call 了这个虚函数
    VirtualProtect((void*)0x400000, 0x35E000, PAGE_EXECUTE_READWRITE, &temp);
    *(uint32_t*)0x667bc0 = (uint32_t)&__AScriptHook;
}

void __AUninstallHook() {
    DWORD temp;
    VirtualProtect((void*)0x400000, 0x35E000, PAGE_EXECUTE_READWRITE, &temp);
    *(uint32_t*)0x667bc0 = 0x452650;
}

struct __AD3dInfo {
    static IDirect3DDevice7* device;
    static IDirectDraw7* ddraw;
};

IDirect3DDevice7* __AD3dInfo::device = 0;
IDirectDraw7* __AD3dInfo::ddraw = 0;

bool IsOpen3dAcceleration() {
    // 如果要改动这个函数里的代码请咨询零度
    auto p2 = AGetPvzBase()->MPtr<APvzStruct>(0x36C);
    if (!p2)
        return false;
    auto p3 = p2->MPtr<APvzStruct>(0x30);
    if (!p3)
        return false;
    auto surface = p3->MPtr<IDirectDrawSurface7>(0x14);
    __AD3dInfo::device = p3->MPtr<IDirect3DDevice7>(0x20);
    __AD3dInfo::ddraw = p3->MPtr<IDirectDraw7>(0x10);
    auto d3d = p3->MPtr<IDirect3D7>(0x1C);

    if (surface == nullptr
        || __AD3dInfo::device == nullptr
        || __AD3dInfo::ddraw == nullptr
        || d3d == nullptr) {
        return false;
    }
    return true;
}

bool AsmDraw() {
    for (auto & mod : mod_list) {
        mod.funcBeforeDrawEveryTick();
    }

    int ret;
    asm volatile(
        "movl 0x6a9ec0, %%ecx;"
        "movl $0x54c650, %%edx;"
        "call *%%edx;"
        "movl %%eax, %[ret];"
        : [ret] "=rm"(ret)
        :
        : "esp", "eax", "ecx", "edx");

    if (ret) {
        for (auto & mod : mod_list) {
            mod.funcDrawEveryTick();
        }
        asm volatile(
            "pushl $0;"
            "movl 0x6a9ec0, %%ecx;"
            "movl $0x54bae0, %%edx;"
            "call *%%edx;"
            :
            :
            : "esp", "eax", "ecx", "edx");
    }

    __asm__ __volatile__(
        "movl %[ret], %%eax;"
        :
        : [ret] "rm"(ret)
        :);
    
    for (auto & mod : mod_list) {
        mod.funcAfterDrawEveryTick();
    }
    
    return ret;
}

void InstallDrawHook() {
    // InstallDrawHook
    *(uint16_t*)0x54C8CD = 0x5890;
    *(uint32_t*)0x667D0C = (uint32_t)&AsmDraw;
    *(uint32_t*)0x671578 = (uint32_t)&AsmDraw;
    *(uint32_t*)0x676968 = (uint32_t)&AsmDraw;
}

void UninstallDrawHook() {
    // UninstallDrawHook
    *(uint16_t*)0x54C8CD = 0xD0FF;
    *(uint32_t*)0x667D0C = 0x54C650;
    *(uint32_t*)0x671578 = 0x54C650;
    *(uint32_t*)0x676968 = 0x54C650;
}

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        // attach to process
        // return FALSE to fail DLL load
        // install hook
        __AInstallHook();
        break;

    case DLL_PROCESS_DETACH:
        // detach from process
        for (auto & mod : mod_list) {
            mod.funcAtExit();
        }
        // uninstall hooks
        if (hasDrawHook) {
            UninstallDrawHook();
            hasDrawHook = false;
        }
        __AUninstallHook();
        // stop thread
        SetEvent(hStopEvent);
        WaitForSingleObject(hThread, 1000);
        if (hStopEvent != INVALID_HANDLE_VALUE) {
            CloseHandle(hStopEvent);
        }
        if (hChangeEvent != INVALID_HANDLE_VALUE) {
            CloseHandle(hChangeEvent);
        }
        if (hThread != INVALID_HANDLE_VALUE) {
            CloseHandle(hThread);
        }
        break;

    case DLL_THREAD_ATTACH:
        // attach to thread
        break;

    case DLL_THREAD_DETACH:
        // detach from thread
        break;
    }
    return TRUE; // succesful
}
