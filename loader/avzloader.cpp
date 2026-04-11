#include <Windows.h>
#include <stdint.h>
#include <cwchar>
#include <d3d.h>
#include <avz_asm.h>
#include <iostream>
#include <toml++/toml.hpp>
#include <algorithm>

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

class RunConf {
public:
    std::vector<std::string> users;
    std::vector<int> levels;
    bool exclude_user;
    bool exclude_level;
    RunConf() : exclude_user(true), exclude_level(true) {}
    bool ShouldRun(const std::string & userName, int levelID) {
        auto p1 = std::find(users.begin(), users.end(), userName);
        if ((p1 == users.end()) ^ exclude_user) return false;
        auto p2 = std::find(levels.begin(), levels.end(), levelID);
        if ((p2 == levels.end()) ^ exclude_level) return false;
        return true;
    }
};

template <class T>
void TomlGetValueOrList(toml::node_view<toml::node> node, std::vector<T> & dest_list) {
    if (auto p = node.value<T>()) {
        dest_list.push_back(*p);
    } else if (auto p = node.as_array()) {
        for (auto & el : *p) {
            if (auto v = el.value<T>()) {
                dest_list.push_back(*v);
            }
        }
    }
}

class ModConf {
public:
    std::vector<RunConf> run_conf_list;
    ModConf() {}
    ModConf(std::wstring & conf_path) {
#ifdef DEBUG
        std::wcout << L"load config file " << conf_path << std::endl;
#endif
        // does file exist?
        DWORD attr = GetFileAttributesW(conf_path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            return;
        }
#ifdef DEBUG
        std::cout << "start parsing" << std::endl;
#endif
        // parse config file
        toml::parse_result config = toml::parse_file(conf_path);
        if (toml::array* arr = config["run"].as_array()) {
            for (auto & tab : *arr) {
                RunConf run_conf;
                // user
                run_conf.exclude_user = !tab.at_path("user.include");
                TomlGetValueOrList(tab.at_path("user.include"), run_conf.users);
                TomlGetValueOrList(tab.at_path("user.exclude"), run_conf.users);
                // level
                run_conf.exclude_level = !tab.at_path("level.include");
                TomlGetValueOrList(tab.at_path("level.include"), run_conf.levels);
                TomlGetValueOrList(tab.at_path("level.exclude"), run_conf.levels);
                // add conf
                run_conf_list.push_back(run_conf);
            }
        }
#ifdef DEBUG
        // debug
        PrintConf();
#endif
    }
    bool ShouldRun(const std::string & userName, int levelID) {
        if (run_conf_list.empty()) {
            return true;
        }
        for (auto & run_conf : run_conf_list) {
            if (run_conf.ShouldRun(userName, levelID)) {
                return true;
            }
        }
        return false;
    }
    void PrintConf() {
        for (auto & run_conf : run_conf_list) {
            std::cout << "user: exclude  = " << run_conf.exclude_user << std::endl;
            std::cout << "      name arr =";
            for (auto & name : run_conf.users) {
                std::cout << " " << name;
            }
            std::cout << std::endl;
            std::cout << "level: exclude   = " << run_conf.exclude_level << std::endl;
            std::cout << "       level arr =";
            for (auto & lvl : run_conf.levels) {
                std::cout << " " << lvl;
            }
            std::cout << std::endl;
        }
    }
};

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
    bool enabled;
    // mod conf
    ModConf conf;
    // exported funcs
    FuncEntryType funcModInit;
    FuncType funcModFinalize;
    FuncLoopType funcBeforeGameLoop;
    FuncLoopType funcAfterGameLoop;
    FuncType funcBeforeDrawEveryTick;
    FuncType funcDrawEveryTick;
    FuncType funcAfterDrawEveryTick;
    void InitFuncs() {
        funcModInit = (FuncEntryType)GetProcAddress(hMod, "ModInit");
        funcModFinalize = (FuncType)GetProcAddress(hMod, "ModFinalize");
        funcBeforeGameLoop = (FuncLoopType)GetProcAddress(hMod, "BeforeGameLoop");
        funcAfterGameLoop = (FuncLoopType)GetProcAddress(hMod, "AfterGameLoop");
        funcBeforeDrawEveryTick = (FuncType)GetProcAddress(hMod, "BeforeDrawEveryTick");
        funcDrawEveryTick = (FuncType)GetProcAddress(hMod, "DrawEveryTick");
        funcAfterDrawEveryTick = (FuncType)GetProcAddress(hMod, "AfterDrawEveryTick");
    }
    Mod(const std::wstring & modName, HMODULE hMod, FILETIME lastWriteTime) : modName(modName), hMod(hMod), lastWriteTime(lastWriteTime), enabled(true) {
        InitFuncs();
    }
    Mod(const std::wstring & modName, HMODULE hMod, FILETIME lastWriteTime, std::wstring & conf_path) : modName(modName), hMod(hMod), lastWriteTime(lastWriteTime), enabled(true), conf(conf_path) {
        InitFuncs();
    }
    bool CheckFunc() {
        std::wstring message = L"Unloaded func in mod ";
        message += modName;
        message += L":";
        bool hasUnloadedFunc = false;
        CHECK_FUNC(ModInit)
        CHECK_FUNC(ModFinalize)
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
    void SwitchEnabled(const std::string & userName, int levelID) {
        enabled = conf.ShouldRun(userName, levelID);
    }
};

std::vector<Mod> mod_list;

void InstallDrawHook();
void UninstallDrawHook();
bool IsOpen3dAcceleration();

std::wstring GetConfFileName(std::wstring & dllName) {
    size_t pos = dllName.find_last_of(L".");
    return dllName.substr(0, pos) + L".toml";
}

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
                        mod.funcModFinalize();
                        FreeLibrary(mod.hMod);
                        // MessageBoxW(NULL, (std::wstring(L"Unload mod 1 ") + mod.modName).c_str(), L"OK", 0);
                    } else {
                        // reload config
                        std::wstring configFullPath = modDir + L"\\" + GetConfFileName(modName);
                        mod.conf = ModConf(configFullPath);
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
                    // find config file (DLL name without suffix + ".toml")
                    std::wstring configFullPath = modDir + L"\\" + GetConfFileName(modName);
                    Mod mod(modName, hMod, lastWriteTime, configFullPath);
                    if (mod.CheckFunc()) {
                        mod.funcModInit(hMod);
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
                mod.funcModFinalize();
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
extern "C" void __cdecl __AScriptHook() {
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

    // should the mod run on this level and this user?
    std::string userName(AGetUserName(), AGetUserNameLen());
    int levelID = AGetPvzBase()->LevelId();
    int gameUi = AGetPvzBase()->GameUi();
    if (gameUi == 3 || gameUi == 2) {
        for (auto & mod : mod_list) {
            mod.SwitchEnabled(userName, levelID);
        }
    }

    // game loop
    bool should_ret = false;
    for (auto & mod : mod_list) {
        if (mod.enabled) {
            if (mod.funcBeforeGameLoop()) {
                should_ret = true;
            }
        }
    }
    if (should_ret) {
        return;
    }
    AAsm::GameTotalLoop();
    for (auto & mod : mod_list) {
        if (mod.enabled) {
            mod.funcAfterGameLoop();
        }
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

    case DLL_PROCESS_DETACH:
        // detach from process
        for (auto & mod : mod_list) {
            mod.funcModFinalize();
        }
        // uninstall hooks
        if (hasDrawHook) {
            UninstallDrawHook();
            hasDrawHook = false;
        }
        __AUninstallHook();
        // stop thread
        SetEvent(hStopEvent);
        WaitForSingleObject(hThread, 100);
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
