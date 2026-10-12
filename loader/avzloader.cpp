#include <Windows.h>
#include <stdint.h>
#include <cwchar>
#include <d3d.h>
#include <avz_asm.h>
#include <avz_pvz_struct.h>
#include <iostream>
#include <toml++/toml.hpp>
#include <algorithm>
#include <mutex>

// #define DEBUG

#ifdef DEBUG
#include <io.h>
#endif

// user related stuff
char * AGetUserName() {
    return AMVal<char *>(0x6A9EC0, 0x82c, 0x4);
}

int AGetUserNameLen() {
    return AMRef<int>(0x6A9EC0, 0x82c, 0x14);
}

int AGetUserNameLenMax() {
    return AMRef<int>(0x6A9EC0, 0x82c, 0x18);
}

int AGetUserSwitchCnt() {
    return AMRef<int>(0x6A9EC0, 0x82c, 0x1c);
}

int AGetUserIdx() {
    return AMRef<int>(0x6A9EC0, 0x82c, 0x20);
}

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath(path);
    size_t pos = exePath.find_last_of(L"\\/");
    return exePath.substr(0, pos);
}

std::wstring pvzDir;
std::wstring modDir;

#include "mod_manager.h"
#include "directory_monitor.h"

ModManager modManager;
MonitorParam monitorParams;
HANDLE monitorThread = nullptr;
bool mod_loaded = false;

extern "C" __declspec(dllexport) void __cdecl RegisterMod(HMODULE module) {
    modManager.AddToRegList(module);
}
extern "C" __declspec(dllexport) void __cdecl UnregisterMod(HMODULE module) {
    modManager.AddToUnRegList(module);
}
extern "C" __declspec(dllexport) void __cdecl SetAdvancedPaused(int paused) {
    modManager.SetAdvancedPaused(paused);
}
extern "C" __declspec(dllexport) void __cdecl SetUpdateWindow(int update) {
    modManager.SetUpdateWindow(update);
}

void PollModDirectory() {
    static unsigned observed = 0, processed = 0;
    static ULONGLONG changedAt = 0, scannedAt = 0;
    ULONGLONG now = GetTickCount64();
    unsigned generation = monitorParams.generation.load();
    if (generation != observed) {
        observed = generation;
        changedAt = now;
    }
    if (!scannedAt || (observed != processed && now - changedAt >= 200 && now - scannedAt >= 200)
        || now - scannedAt >= 2000) {
        if (modManager.LoadAllMods())
            processed = generation;
        scannedAt = now;
    }
}

void InstallDrawHook();
void UninstallDrawHook();
bool IsOpen3dAcceleration();

// Copy-Paste来的，为了不引入多的函数
bool AGameIsPaused() {
    if (!AGetPvzBase()->MainObject())
        return false;
    return AGetMainObject()->GamePaused() || AGetPvzBase()->MouseWindow()->TopWindow() != nullptr;
}
// 不知道这个painter有什么用，为简便就删了
// APainter _rectPainter;
// int _pvzWidth = 0;
// int _pvzHeight = 0;
// AAliveFilter<AZombie> aAliveZombieFilter;
void UpdateAdvancedPause() {
    // _rectPainter.Draw(ARect(0, 0, _pvzWidth, _pvzHeight));
    AAsm::UpdateCursorObjectAndPreview();
    --AGetMainObject()->GlobalClock();
    --AGetPvzBase()->MjClock();
    int levelId = AGetPvzBase()->LevelId(); // 关卡序号
    if (AAsm::PUZZLE_I_ZOMBIE_1 <= levelId && levelId <= AAsm::PUZZLE_I_ZOMBIE_ENDLESS) {
        // 刷新卡片数组
        AAsm::RefreshAllSeedPackets();
        // 因为 IZE 模式的僵尸渲染位置有问题，
        // 所以需要更新僵尸位置
        AZombie * zombieArr = AGetMainObject()->ZombieArray();
        for (int i = 0; i < AGetMainObject()->ZombieCountMax(); i++) {
            AZombie & zombie = zombieArr[i];
            if (!zombie.IsDisappeared() && !zombie.IsDead() && zombie.AtWave() >= 0) {
                zombie.MRef<int>(0x8) = zombie.Abscissa();
                zombie.MRef<int>(0xC) = zombie.Ordinate();
            }
        }
    }
}

extern "C" __declspec(dllexport) void __cdecl LoaderRunTotal() {
    if (!mod_loaded)
        return;
    PollModDirectory();
    auto base = AGetPvzBase();
    bool hasUser = base->MPtr<APvzStruct>(0x82c) != nullptr;
    std::string user = hasUser ? std::string(AGetUserName(), AGetUserNameLen()) : std::string();
    modManager.Manage(user, hasUser ? AGetUserIdx() : -1, base->GameUi(), base->LevelId());
    modManager.callModRunTotal();
    modManager.SyncControllerState();
    if (modManager.isAdvancedPaused() && base->MainObject() && base->GameUi() == 3) {
        UpdateAdvancedPause();
    }
    modManager.Reconcile();
}

bool hasDrawHook = false;
extern "C" void __cdecl __AScriptHook() {
    // manage mods
    if (!mod_loaded) {
        // set folder
        pvzDir = GetExeDir();
        modDir = pvzDir + L"\\mods";
        // load mods
        CreateDirectoryW(modDir.c_str(), nullptr);
        modManager.directory = modDir;
        modManager.applyPause = [](int paused) { *(uint16_t*)0x41600E = paused ? 0x2AEB : 0xFD8B; };
        monitorParams.targetDir = modDir;
        monitorParams.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (monitorParams.stopEvent)
            monitorThread = CreateThread(nullptr, 0, MonitorThreadProc, &monitorParams, 0, nullptr);
        // Periodic scans also cover watcher failure.
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

    // game loop
    LoaderRunTotal();
    if (!modManager.isUpdateWindow())
        return;
    AAsm::GameTotalLoop();
    while (modManager.isSkipTick() && AGetPvzBase()->MainObject()) {
        LoaderRunTotal();
        if (modManager.isAdvancedPaused())
            return;
        if (AGameIsPaused()) // 防止游戏暂停时开启跳帧发生死锁
            return;
        if (modManager.BlockTimeArrived())
            return;
        if (!AGetPvzBase()->MainObject() || AGetPvzBase()->GameUi() != 3)
            return;
        AGetPvzBase()->MjClock() += 1;
        AAsm::GameFightLoop();
        AAsm::ClearObjectMemory();
        AAsm::CheckFightExit();
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
    ModManager::DispatchGuard frame(modManager);
    modManager.callBeforeDrawEveryTick();

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
        modManager.callDrawEveryTick();
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
    
    modManager.callAfterDrawEveryTick();
    
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
    case DLL_PROCESS_ATTACH: {
        // attach to process
        // return FALSE to fail DLL load
        // install hook
        // Hook targets and watcher code remain valid until process exit.
        HMODULE pinned;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&__AScriptHook), &pinned);
        __AInstallHook();
#ifdef DEBUG
        // 打开一个终端，显示std::cout结果
        if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (::AllocConsole()) {
                FILE* unused;
                if (freopen_s(&unused, "CONOUT$", "w", stdout)) {
                    _dup2(_fileno(stdout), 1);
                }
                if (freopen_s(&unused, "CONOUT$", "w", stderr)) {
                    _dup2(_fileno(stdout), 2);
                }
                std::ios::sync_with_stdio();
            }
        }
#endif
        break;
    }
    case DLL_PROCESS_DETACH:
        // Never wait for the watcher or invoke author code under the loader lock.
        if (monitorParams.stopEvent)
            SetEvent(monitorParams.stopEvent);
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
