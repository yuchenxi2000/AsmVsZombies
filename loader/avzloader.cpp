#include <Windows.h>
#include <stdint.h>
#include <cwchar>
#include <d3d.h>
#include <avz_asm.h>

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath(path);
    size_t pos = exePath.find_last_of(L"\\/");
    return exePath.substr(0, pos);
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
    FuncEntryType funcEntry;
    FuncType funcAtExit;
    FuncLoopType funcBeforeGameLoop;
    FuncLoopType funcAfterGameLoop;
    FuncType funcBeforeDrawEveryTick;
    FuncType funcDrawEveryTick;
    FuncType funcAfterDrawEveryTick;
    Mod(const std::wstring & modName, HMODULE hMod) : modName(modName), hMod(hMod) {
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
    std::wstring pwd = GetExeDir();
    std::wstring dirPath = pwd + L"\\mods";
    std::wstring searchPath = dirPath + L"\\*.dll";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring fullPath = dirPath + L"\\" + findData.cFileName;
            HMODULE hMod = LoadLibraryW(fullPath.c_str());
            if (hMod) {
                Mod mod(std::wstring(findData.cFileName), hMod);
                if (mod.CheckFunc()) {
                    mod.funcEntry(hMod);
                    mod_list.push_back(mod);
                }
            } else {
                MessageBoxW(NULL, (std::wstring(L"Failed to load ") + findData.cFileName + L" with error " + std::to_wstring(GetLastError())).c_str(), L"OK", 0);
            }
        } while (FindNextFileW(hFind, &findData));
    }
}

bool hasDrawHook = false;
bool mod_loaded = false;
extern "C" __declspec(dllexport) void __cdecl __AScriptHook() {
    // load mods
    if (!mod_loaded) {
        LoadAllMods();
        mod_loaded = true;
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
    // install draw hook
    // if (IsOpen3dAcceleration()) {
    //     // install draw hook
    //     if (!hasDrawHook) {
    //         InstallDrawHook();
    //         hasDrawHook = true;
    //     }
    // } else {
    //     if (hasDrawHook) {
    //         UninstallDrawHook();
    //         hasDrawHook = false;
    //     }
    // }
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

// 修改过的PvZ会先调用这个
extern "C" __declspec(dllexport) void __cdecl Entry(HINSTANCE hinstDLL) {
    // MessageBoxW(NULL, L"Loader is loaded", L"Success", 0);
    // install hook
    // __AInstallHook();
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
        if (hasDrawHook) {
            UninstallDrawHook();
            hasDrawHook = false;
        }
        __AUninstallHook();
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
